#include "filters.hpp"
#include <zlib.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct Args {
    std::string input;
    int limit = -1;
};

Args parseArgs(int argc, char** argv) {
    Args args;
    if (argc < 2) {
        std::cerr << "Usage: gopher_filter_batch <texts.jsonl[.gz]> [--limit N]\n";
        std::exit(1);
    }
    args.input = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--limit" && i + 1 < argc) {
            args.limit = std::stoi(argv[++i]);
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

    size_t docs = 0;
    size_t bytes = 0;
    size_t kept = 0;

    auto start = std::chrono::high_resolution_clock::now();

    std::string line;
    while (true) {
        bool ok = use_gz ? readLineGz(gz, line) : readLine(fin, line);
        if (!ok) break;
        if (args.limit != -1 && static_cast<int>(docs) >= args.limit) break;
        if (line.empty()) continue;
        std::string text = parseTextField(line);
        if (text.empty()) continue;

        FilterResult res = filter.filter(text);
        if (res.keep) kept++;
        docs++;
        bytes += text.size();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    if (use_gz && gz) gzclose(gz);

    double secs = elapsed.count();
    double docs_sec = secs > 0 ? docs / secs : 0.0;
    double mb_sec = secs > 0 ? (bytes / 1024.0 / 1024.0) / secs : 0.0;

    std::cout << "{"
              << "\"docs\":" << docs << ","
              << "\"kept\":" << kept << ","
              << "\"elapsed_sec\":" << secs << ","
              << "\"docs_sec\":" << docs_sec << ","
              << "\"mb_sec\":" << mb_sec
              << "}\n";

    return 0;
}

