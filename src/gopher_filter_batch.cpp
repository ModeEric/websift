#include "filters.hpp"
#include <zlib.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <future>
#include <thread>
#include <memory>
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>

namespace {
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
        if (queue_.empty()) {
            return false;
        }
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

struct Args {
    std::string input;
    int limit = -1;
    int threads = 1;
};

Args parseArgs(int argc, char** argv) {
    Args args;
    if (argc < 2) {
        std::cerr << "Usage: gopher_filter_batch <texts.jsonl[.gz]> [--limit N] [--threads N]\n";
        std::exit(1);
    }
    args.input = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--limit" && i + 1 < argc) {
            args.limit = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            args.threads = std::stoi(argv[++i]);
        }
    }
    return args;
}

bool isGzip(const std::string& path) {
    return path.size() >= 3 && path.substr(path.size() - 3) == ".gz";
}

bool readLine(std::istream& in, std::string& line) {
    return static_cast<bool>(std::getline(in, line));
}

bool readLineGz(gzFile file, std::string& line) {
    line.clear();
    constexpr int kChunk = 4096;
    char buf[kChunk];
    while (true) {
        char* res = gzgets(file, buf, kChunk);
        if (!res) {
            return !line.empty();
        }
        line.append(res);
        if (!line.empty() && line.back() == '\n') {
            if (!line.empty() && line[line.size() - 1] == '\n') {
                // keep newline trimmed like getline
                line.pop_back();
                if (!line.empty() && line.back() == '\r') line.pop_back();
            }
            return true;
        }
        if (gzeof(file)) {
            return true;
        }
    }
}

std::string parseTextField(const std::string& line) {
    const std::string key = "\"text\":\"";
    size_t pos = line.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    std::string out;
    out.reserve(line.size() - pos);
    bool escape = false;
    for (size_t i = pos; i < line.size(); ++i) {
        char c = line[i];
        if (escape) {
            switch (c) {
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(c); break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            out.push_back(c);
        }
    }
    return out;
}
} // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Args args = parseArgs(argc, argv);

    std::unique_ptr<std::istream> in_stream;
    std::ifstream fin;
    gzFile gz = nullptr;
    bool use_gz = isGzip(args.input);
    if (use_gz) {
        gz = gzopen(args.input.c_str(), "rb");
        if (!gz) {
            std::cerr << "Failed to open gzip file: " << args.input << "\n";
            return 1;
        }
    } else {
        fin.open(args.input);
        if (!fin.is_open()) {
            std::cerr << "Failed to open file: " << args.input << "\n";
            return 1;
        }
    }

    GopherQualityFilter filter;

    auto start = std::chrono::high_resolution_clock::now();

    const bool use_parallel = args.threads != 1;
    std::atomic<size_t> docs{0};
    std::atomic<size_t> kept{0};
    size_t bytes = 0;

    if (use_parallel) {
        unsigned int hw_threads = std::thread::hardware_concurrency();
        if (hw_threads == 0) hw_threads = 4;
        unsigned int thread_count = args.threads > 0 ? static_cast<unsigned int>(args.threads) : hw_threads;
        thread_count = std::max(1u, thread_count);

        const size_t queue_capacity = 1024;
        BoundedQueue<std::string> queue(queue_capacity);
        std::vector<std::thread> workers;
        workers.reserve(thread_count);

        for (unsigned int t = 0; t < thread_count; ++t) {
            workers.emplace_back([&queue, &filter, &kept, &docs]() {
                std::string text;
                size_t kept_local = 0;
                size_t docs_local = 0;
                while (queue.pop(text)) {
                    docs_local++;
                    if (filter.filter(text).keep) kept_local++;
                }
                kept.fetch_add(kept_local, std::memory_order_relaxed);
                docs.fetch_add(docs_local, std::memory_order_relaxed);
            });
        }

        size_t produced = 0;
        std::string line;
        while (true) {
            bool ok = use_gz ? readLineGz(gz, line) : readLine(fin, line);
            if (!ok) break;
            if (args.limit != -1 && static_cast<int>(produced) >= args.limit) break;
            if (line.empty()) continue;
            std::string text = parseTextField(line);
            if (text.empty()) continue;

            bytes += text.size();
            produced++;
            if (!queue.push(std::move(text))) break;
        }
        queue.close();
        for (auto& w : workers) w.join();
    } else {
        std::string line;
        while (true) {
            bool ok = use_gz ? readLineGz(gz, line) : readLine(fin, line);
            if (!ok) break;
            if (args.limit != -1 && static_cast<int>(docs.load(std::memory_order_relaxed)) >= args.limit) break;
            if (line.empty()) continue;
            std::string text = parseTextField(line);
            if (text.empty()) continue;

            bytes += text.size();
            FilterResult res = filter.filter(text);
            if (res.keep) kept++;
            docs++;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    if (use_gz && gz) gzclose(gz);

    std::chrono::duration<double> elapsed = end - start;

    double secs = elapsed.count();
    size_t docs_count = docs.load(std::memory_order_relaxed);
    size_t kept_count = kept.load(std::memory_order_relaxed);
    double docs_sec = secs > 0 ? static_cast<double>(docs_count) / secs : 0.0;
    double mb_sec = secs > 0 ? (bytes / 1024.0 / 1024.0) / secs : 0.0;

    std::cout << "{"
              << "\"docs\":" << docs_count << ","
              << "\"kept\":" << kept_count << ","
              << "\"elapsed_sec\":" << secs << ","
              << "\"docs_sec\":" << docs_sec << ","
              << "\"mb_sec\":" << mb_sec
              << "}\n";

    return 0;
}
