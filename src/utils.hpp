#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

namespace Utils {

    inline std::string toLower(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    // Simple HTML stripper (removes <...>)
    inline std::string extractText(const std::string& html) {
        std::string text;
        text.reserve(html.size());
        bool inTag = false;
        for (char c : html) {
            if (c == '<') {
                inTag = true;
            } else if (c == '>') {
                inTag = false;
                text += ' '; // Replace tag with space to avoid merging words
            } else if (!inTag) {
                text += c;
            }
        }
        return text;
    }

    // Extract body from HTTP response (skip headers)
    inline std::string extractHttpBody(const std::string& response) {
        size_t pos = response.find("\r\n\r\n");
        if (pos != std::string::npos) {
            return response.substr(pos + 4);
        }
        pos = response.find("\n\n");
        if (pos != std::string::npos) {
            return response.substr(pos + 2);
        }
        return response; // Fallback
    }

    inline std::vector<std::string> splitLines(const std::string& text) {
        std::vector<std::string> lines;
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        return lines;
    }
    
    inline std::vector<std::string> splitWords(const std::string& text) {
        std::vector<std::string> words;
        std::string word;
        std::stringstream ss(text);
        while (ss >> word) {
            words.push_back(word);
        }
        return words;
    }
}

