#include "warc.hpp"
#include "utils.hpp"
#include <fstream>
#include <iostream>
#include <string>

struct Args {
    std::string input_file;
    std::string output_file;
    int limit = -1;
};

Args parseArgs(int argc, char** argv) {
    Args args;
    if (argc < 2) {
        std::cerr << "Usage: extract_texts <input.warc.gz> [--limit N] [--output file]" << std::endl;
        std::exit(1);
    }
    args.input_file = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--limit" && i + 1 < argc) {
            args.limit = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            args.output_file = argv[++i];
        }
    }
    return args;
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Args args = parseArgs(argc, argv);

    WarcReader reader(args.input_file);
    std::ostream* out = &std::cout;
    std::ofstream fout;
    if (!args.output_file.empty()) {
        fout.open(args.output_file);
        if (!fout.is_open()) {
            std::cerr << "Failed to open output file: " << args.output_file << std::endl;
            return 1;
        }
        out = &fout;
    }

    size_t total = 0;
    WarcRecord record;
    while (reader.nextRecord(record)) {
        if (args.limit != -1 && static_cast<int>(total) >= args.limit) break;
        if (record.type != "response") continue;

        std::string body = Utils::extractHttpBody(record.content);
        std::string text = Utils::extractText(body);

        // Emit compact JSON line
        (*out) << "{\"id\":\"" << record.id << "\",\"text\":\"";
        for (char c : text) {
            switch (c) {
                case '\\': (*out) << "\\\\"; break;
                case '\"': (*out) << "\\\""; break;
                case '\n': (*out) << "\\n"; break;
                case '\r': (*out) << "\\r"; break;
                case '\t': (*out) << "\\t"; break;
                default: (*out) << c; break;
            }
        }
        (*out) << "\"}\n";
        total++;
    }

    return 0;
}

