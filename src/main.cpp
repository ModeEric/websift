#include "warc.hpp"
#include "filters.hpp"
#include "utils.hpp"
#include <iostream>
#include <chrono>
#include <map>
#include <iomanip>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <deque>
#include <mutex>
#include <condition_variable>

void downloadBadWords() {
    std::ifstream f("badwords_en.txt");
    if (!f.good()) {
        std::cout << "Downloading badwords list..." << std::endl;
        std::string cmd = "curl -k -s -o badwords_en.txt https://raw.githubusercontent.com/LDNOOBW/List-of-Dirty-Naughty-Obscene-and-Otherwise-Bad-Words/25e679f03d96baa721cde20db9944649e8d0a844/en";
        int ret = system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "Warning: Failed to download badwords list (code " << ret << "). Using fallback." << std::endl;
        }
    }
}

struct Args {
    std::string input_file;
    std::string csv_output_file;
    int limit = -1;
    int threads = 1;
    size_t queue_depth = 1024;
};

Args parseArgs(int argc, char** argv) {
    Args args;
    // Defaults
    args.input_file = "CC-MAIN-20251119093413-20251119123413-00999.warc.gz";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--csv-output" && i + 1 < argc) {
            args.csv_output_file = argv[++i];
        } else if (arg == "--limit" && i + 1 < argc) {
            args.limit = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            args.threads = std::stoi(argv[++i]);
        } else if (arg == "--queue-depth" && i + 1 < argc) {
            args.queue_depth = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg[0] != '-') {
            args.input_file = arg;
        }
    }
    return args;
}

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

    bool push(T&& item) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_push_.wait(lock, [&] { return closed_ || queue_.size() < capacity_; });
        if (closed_) return false;
        queue_.push_back(std::move(item));
        cv_pop_.notify_one();
        return true;
    }

    bool pop(T& out) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_pop_.wait(lock, [&] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        cv_push_.notify_one();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mu_);
        closed_ = true;
        cv_pop_.notify_all();
        cv_push_.notify_all();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_push_;
    std::condition_variable cv_pop_;
    std::deque<T> queue_;
    size_t capacity_;
    bool closed_ = false;
};

struct WorkItem {
    std::string id;
    std::string content;
};

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Args args = parseArgs(argc, argv);

    downloadBadWords();

    WarcReader reader(args.input_file);
    C4QualityFilter qualityFilter;
    C4ParagraphFilter paragraphFilter;
    C4BadWordsFilter badWordsFilter;

    std::ofstream csvOut;
    if (!args.csv_output_file.empty()) {
        csvOut.open(args.csv_output_file);
        if (csvOut.is_open()) {
            csvOut << "record_id,status,reason\n";
        } else {
            std::cerr << "Error: Could not open CSV output file " << args.csv_output_file << std::endl;
        }
    }

    std::atomic<size_t> totalDocs{0};
    std::atomic<size_t> keptDocs{0};
    std::atomic<size_t> droppedDocs{0};
    std::atomic<size_t> totalBytes{0};
    std::map<std::string, size_t> dropReasons;
    std::mutex dropMu;
    std::mutex csvMu;

    auto startTime = std::chrono::high_resolution_clock::now();

    const bool use_parallel = args.threads != 1;
    if (use_parallel) {
        unsigned int hw_threads = std::thread::hardware_concurrency();
        if (hw_threads == 0) hw_threads = 4;
        unsigned int thread_count = args.threads > 0 ? static_cast<unsigned int>(args.threads) : hw_threads;
        thread_count = std::max(1u, thread_count);

        BoundedQueue<WorkItem> queue(args.queue_depth ? args.queue_depth : 1024);
        std::vector<std::thread> workers;
        workers.reserve(thread_count);

        for (unsigned int t = 0; t < thread_count; ++t) {
            workers.emplace_back([&queue, &csvOut, &csvMu, &dropReasons, &dropMu, &totalDocs, &keptDocs, &droppedDocs, &totalBytes]() {
                C4QualityFilter qf;
                C4ParagraphFilter pf;
                C4BadWordsFilter bf;
                std::string status;
                std::string reason;

                WorkItem item;
                while (queue.pop(item)) {
                    status = "kept";
                    reason.clear();

                    bool drop = false;
                    if (item.content.empty()) {
                        drop = true;
                        reason = "empty_text";
                    }

                    if (!drop) {
                        FilterResult res = qf.filter(item.content);
                        if (!res.keep) {
                            drop = true;
                            reason = res.reason;
                        }
                    }

                    if (!drop) {
                        FilterResult res = pf.filter(item.content);
                        if (!res.keep) {
                            drop = true;
                            reason = res.reason;
                        }
                    }

                    if (!drop) {
                        FilterResult res = bf.filter(item.content);
                        if (!res.keep) {
                            drop = true;
                            reason = res.reason;
                        }
                    }

                    totalDocs.fetch_add(1, std::memory_order_relaxed);
                    totalBytes.fetch_add(item.content.size(), std::memory_order_relaxed);

                    if (drop) {
                        droppedDocs.fetch_add(1, std::memory_order_relaxed);
                        status = "dropped";
                        {
                            std::lock_guard<std::mutex> lk(dropMu);
                            dropReasons[reason]++;
                        }
                    } else {
                        keptDocs.fetch_add(1, std::memory_order_relaxed);
                    }

                    if (csvOut.is_open()) {
                        std::lock_guard<std::mutex> lk(csvMu);
                        std::string safeReason = reason;
                        if (safeReason.find(',') != std::string::npos) {
                            safeReason = "\"" + safeReason + "\"";
                        }
                        csvOut << item.id << "," << status << "," << safeReason << "\n";
                    }
                }
            });
        }

        size_t produced = 0;
        WarcRecord record;
        while (reader.nextRecord(record)) {
            if (record.type != "response") continue;
            if (args.limit != -1 && static_cast<int>(produced) >= args.limit) break;
            produced++;
            WorkItem item;
            item.id = record.id;
            {
                std::string body = Utils::extractHttpBody(record.content);
                item.content = Utils::extractText(body);
            }
            if (!queue.push(std::move(item))) break;
        }
        queue.close();
        for (auto& w : workers) w.join();
    } else {
        WarcRecord record;
        while (reader.nextRecord(record)) {
            if (args.limit != -1 && (int)totalDocs.load(std::memory_order_relaxed) >= args.limit) break;

            if (record.type != "response") continue;
            
            std::string text;
            {
                Utils::ScopedTimer t("Extraction");
                std::string body = Utils::extractHttpBody(record.content);
                text = Utils::extractText(body);
            }

            std::string status = "kept";
            std::string reason = "";

            bool drop = false;

            if (text.empty()) {
                drop = true;
                reason = "empty_text";
            }

            if (!drop) {
                Utils::ScopedTimer t("QualityFilter");
                FilterResult res = qualityFilter.filter(text);
                if (!res.keep) {
                    drop = true;
                    reason = res.reason;
                }
            }

            if (!drop) {
                Utils::ScopedTimer t("ParagraphFilter");
                FilterResult res = paragraphFilter.filter(text);
                if (!res.keep) {
                    drop = true;
                    reason = res.reason;
                }
            }

            if (!drop) {
                Utils::ScopedTimer t("BadWordsFilter");
                FilterResult res = badWordsFilter.filter(text);
                if (!res.keep) {
                    drop = true;
                    reason = res.reason;
                }
            }

            totalDocs++;
            totalBytes += text.size();

            if (drop) {
                droppedDocs++;
                dropReasons[reason]++;
                status = "dropped";
            } else {
                keptDocs++;
            }

            if (csvOut.is_open()) {
                if (reason.find(',') != std::string::npos) {
                    reason = "\"" + reason + "\"";
                }
                csvOut << record.id << "," << status << "," << reason << "\n";
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;

    size_t totalDocsCount = totalDocs.load(std::memory_order_relaxed);
    size_t totalBytesCount = totalBytes.load(std::memory_order_relaxed);
    size_t keptDocsCount = keptDocs.load(std::memory_order_relaxed);
    size_t droppedDocsCount = droppedDocs.load(std::memory_order_relaxed);

    std::cout << "Processing completed in " << elapsed.count() << " seconds." << std::endl;
    std::cout << "Total Docs: " << totalDocsCount << std::endl;
    std::cout << "Total Bytes: " << totalBytesCount << std::endl;
    std::cout << "Kept Docs: " << keptDocsCount << std::endl;
    std::cout << "Dropped Docs: " << droppedDocsCount << std::endl;
    if (elapsed.count() > 0) {
        std::cout << "Docs/sec: " << (elapsed.count() > 0 ? totalDocsCount / elapsed.count() : 0) << std::endl;
        std::cout << "MB/sec: " << (elapsed.count() > 0 ? (totalBytesCount / 1024.0 / 1024.0) / elapsed.count() : 0) << std::endl;
    }

    std::cout << "\nDrop Reasons:" << std::endl;
    for (const auto& pair : dropReasons) {
        std::cout << "  " << pair.first << ": " << pair.second << std::endl;
    }

    Utils::Profiler::instance().printStats();

    return 0;
}
