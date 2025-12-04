#include "warc.hpp"
#include "filters.hpp"
#include "utils.hpp"
#include <iostream>
#include <chrono>
#include <map>
#include <iomanip>
#include <fstream>

void downloadBadWords() {
    std::ifstream f("badwords_en.txt");
    if (!f.good()) {
        std::cout << "Downloading badwords list..." << std::endl;
        // Use -k to bypass SSL issues in some environments
        std::string cmd = "curl -k -s -o badwords_en.txt https://raw.githubusercontent.com/LDNOOBW/List-of-Dirty-Naughty-Obscene-and-Otherwise-Bad-Words/25e679f03d96baa721cde20db9944649e8d0a844/en";
        int ret = system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "Warning: Failed to download badwords list (code " << ret << "). Using fallback." << std::endl;
        }
    }
}

int main(int argc, char** argv) {
    std::string filename;
    if (argc > 1) {
        filename = argv[1];
    } else {
        filename = "CC-MAIN-20251119093413-20251119123413-00999.warc.gz";
    }

    downloadBadWords();

    WarcReader reader(filename);
    C4QualityFilter qualityFilter;
    C4ParagraphFilter paragraphFilter;
    C4BadWordsFilter badWordsFilter;
    
    size_t totalDocs = 0;
    size_t keptDocs = 0;
    size_t droppedDocs = 0;
    size_t totalBytes = 0;
    
    std::map<std::string, size_t> dropReasons;

    auto startTime = std::chrono::high_resolution_clock::now();

    WarcRecord record;
    while (reader.nextRecord(record)) {
        if (record.type != "response") continue;
        
        totalDocs++;
        totalBytes += record.content.size();

        std::string body = Utils::extractHttpBody(record.content);
        std::string text = Utils::extractText(body);

        if (text.empty()) {
            droppedDocs++;
            dropReasons["empty_text"]++;
            continue;
        }

        FilterResult res = qualityFilter.filter(text);
        if (!res.keep) {
            droppedDocs++;
            dropReasons[res.reason]++;
            continue;
        }

        res = paragraphFilter.filter(text);
        if (!res.keep) {
            droppedDocs++;
            dropReasons[res.reason]++;
            continue;
        }

        res = badWordsFilter.filter(text);
        if (!res.keep) {
            droppedDocs++;
            dropReasons[res.reason]++;
            continue;
        }

        keptDocs++;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;

    std::cout << "Processing completed in " << elapsed.count() << " seconds." << std::endl;
    std::cout << "Total Docs: " << totalDocs << std::endl;
    std::cout << "Total Bytes: " << totalBytes << std::endl;
    std::cout << "Kept Docs: " << keptDocs << std::endl;
    std::cout << "Dropped Docs: " << droppedDocs << std::endl;
    std::cout << "Docs/sec: " << totalDocs / elapsed.count() << std::endl;
    std::cout << "MB/sec: " << (totalBytes / 1024.0 / 1024.0) / elapsed.count() << std::endl;

    std::cout << "\nDrop Reasons:" << std::endl;
    for (const auto& pair : dropReasons) {
        std::cout << "  " << pair.first << ": " << pair.second << std::endl;
    }

    return 0;
}
