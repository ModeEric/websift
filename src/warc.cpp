#include "warc.hpp"
#include <iostream>
#include <sstream>
#include <cstring>

WarcReader::WarcReader(const std::string& filename) : filename(filename) {
    file = gzopen(filename.c_str(), "rb");
    if (!file) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
    }
}

WarcReader::~WarcReader() {
    close();
}

void WarcReader::close() {
    if (file) {
        gzclose(file);
        file = nullptr;
    }
}

std::string WarcReader::readLine() {
    if (!file) return "";
    if (gzgets(file, buffer, sizeof(buffer)) == Z_NULL) {
        return "";
    }
    std::string line(buffer);
    // Remove trailing CRLF or LF
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

bool WarcReader::readHeaders(WarcRecord& record) {
    std::string line;
    while (true) {
        line = readLine();
        if (line.empty()) {
            // Empty line marks end of headers
            // But wait, we might hit empty lines between records. 
            // Standard says: "The WARC record content is followed by two CRLF sequences."
            // So we might see empty lines before a record starts?
            // Usually we assume we are at start of record.
            // If we are inside headers and hit empty line, it's end of headers.
            break; 
        }
        
        // Parse header "Key: Value"
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string val = line.substr(colonPos + 1);
            
            // Trim spaces
            while (!val.empty() && val.front() == ' ') val.erase(0, 1);
            
            record.headers[key] = val;
            
            if (key == "WARC-Type") record.type = val;
            if (key == "WARC-Target-URI") record.url = val;
            if (key == "Content-Length") record.contentLength = std::stoul(val);
        }
    }
    return !record.headers.empty();
}

bool WarcReader::readContent(WarcRecord& record) {
    if (record.contentLength == 0) return true;

    // Allocate buffer
    char* buf = new char[record.contentLength];
    int bytesRead = gzread(file, buf, record.contentLength);
    
    if (bytesRead < 0) {
        delete[] buf;
        return false; 
    }
    
    record.content.assign(buf, bytesRead);
    delete[] buf;

    // Consume the two CRLF after content (4 bytes)
    // Actually, sometimes it's just loose. But strictly it's \r\n\r\n.
    // We can just try to consume strictly or just let the next readLine handle empty lines.
    // Robust readers usually skip whitespace until "WARC/"
    
    return bytesRead == (int)record.contentLength;
}

bool WarcReader::nextRecord(WarcRecord& record) {
    if (!file || gzeof(file)) return false;

    // Reset record
    record = WarcRecord();
    
    // Scan for "WARC/" start
    std::string line;
    while (true) {
        line = readLine();
        if (line.empty()) {
            if (gzeof(file)) return false;
            continue;
        }
        if (line.rfind("WARC/", 0) == 0) { // Starts with WARC/
            // This is the version line
            break; 
        }
    }
    
    // We found start. Now read headers.
    if (!readHeaders(record)) return false;
    
    // Read content
    if (!readContent(record)) return false;
    
    record.valid = true;
    return true;
}

