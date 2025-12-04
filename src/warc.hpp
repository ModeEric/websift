#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <zlib.h>

struct WarcRecord {
    std::unordered_map<std::string, std::string> headers;
    std::string content;
    std::string type; // WARC-Type
    std::string url;  // WARC-Target-URI
    std::string id;   // WARC-Record-ID
    size_t contentLength = 0;
    bool valid = false;
};

class WarcReader {
public:
    WarcReader(const std::string& filename);
    ~WarcReader();

    bool nextRecord(WarcRecord& record);
    void close();

private:
    std::string filename;
    gzFile file;
    char buffer[8192];

    std::string readLine();
    bool readHeaders(WarcRecord& record);
    bool readContent(WarcRecord& record);
};
