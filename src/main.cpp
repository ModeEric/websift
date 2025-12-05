#include "warc.hpp"
#include "filters.hpp"
#include "utils.hpp"
#include <iostream>
#include <chrono>
#include <map>
#include <iomanip>
#include <fstream>
#include <string>

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
        } else if (arg[0] != '-') {
            args.input_file = arg;
        }
    }
    return args;
}

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

    size_t totalDocs = 0;
    size_t keptDocs = 0;
    size_t droppedDocs = 0;
    size_t totalBytes = 0;
    
    std::map<std::string, size_t> dropReasons;

    auto startTime = std::chrono::high_resolution_clock::now();

    WarcRecord record;
    while (reader.nextRecord(record)) {
        if (args.limit != -1 && (int)totalDocs >= args.limit) break;

        if (record.type != "response") continue;
        
        totalDocs++;
        totalBytes += record.content.size();

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

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;

    std::cout << "Processing completed in " << elapsed.count() << " seconds." << std::endl;
    std::cout << "Total Docs: " << totalDocs << std::endl;
    std::cout << "Total Bytes: " << totalBytes << std::endl;
    std::cout << "Kept Docs: " << keptDocs << std::endl;
    std::cout << "Dropped Docs: " << droppedDocs << std::endl;
    if (elapsed.count() > 0) {
        std::cout << "Docs/sec: " << totalDocs / elapsed.count() << std::endl;
        std::cout << "MB/sec: " << (totalBytes / 1024.0 / 1024.0) / elapsed.count() << std::endl;
    }

    std::cout << "\nDrop Reasons:" << std::endl;
    for (const auto& pair : dropReasons) {
        std::cout << "  " << pair.first << ": " << pair.second << std::endl;
    }

    Utils::Profiler::instance().printStats();

    return 0;
}
