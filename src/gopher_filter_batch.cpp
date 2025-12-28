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
    size_t docs = 0;
    size_t kept = 0;
    size_t bytes = 0;

    if (use_parallel) {
        std::vector<std::string> texts;
        texts.reserve(args.limit > 0 ? static_cast<size_t>(args.limit) : 1024);

        std::string line;
        while (true) {
            bool ok = use_gz ? readLineGz(gz, line) : readLine(fin, line);
            if (!ok) break;
            if (args.limit != -1 && static_cast<int>(texts.size()) >= args.limit) break;
            if (line.empty()) continue;
            std::string text = parseTextField(line);
            if (text.empty()) continue;

            bytes += text.size();
            texts.emplace_back(std::move(text));
        }

        docs = texts.size();

        unsigned int hw_threads = std::thread::hardware_concurrency();
        if (hw_threads == 0) hw_threads = 4;
        unsigned int thread_count = args.threads > 0 ? static_cast<unsigned int>(args.threads) : hw_threads;
        thread_count = std::max(1u, std::min<unsigned int>(thread_count, static_cast<unsigned int>(docs == 0 ? 1 : docs)));

        const size_t chunk = (docs + thread_count - 1) / thread_count;
        std::vector<std::future<size_t>> futures;
        futures.reserve(thread_count);

        for (unsigned int t = 0; t < thread_count; ++t) {
            const size_t start_idx = t * chunk;
            if (start_idx >= docs) break;
            const size_t end_idx = std::min(docs, start_idx + chunk);
            futures.emplace_back(std::async(std::launch::async, [start_idx, end_idx, &texts, &filter]() -> size_t {
                size_t kept_local = 0;
                for (size_t i = start_idx; i < end_idx; ++i) {
                    if (filter.filter(texts[i]).keep) kept_local++;
                }
                return kept_local;
            }));
        }

        for (auto& f : futures) {
            kept += f.get();
        }
    } else {
        std::string line;
        while (true) {
            bool ok = use_gz ? readLineGz(gz, line) : readLine(fin, line);
            if (!ok) break;
            if (args.limit != -1 && static_cast<int>(docs) >= args.limit) break;
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
    double docs_sec = secs > 0 ? static_cast<double>(docs) / secs : 0.0;
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
