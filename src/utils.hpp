#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <unordered_map>
#include <iostream>
#include <iomanip>

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

    // Profiling helpers
    struct TimerStats {
        double total_ms = 0;
        size_t count = 0;
    };

    class Profiler {
    public:
        static Profiler& instance() {
            static Profiler inst;
            return inst;
        }

        void start(const std::string& name) {
            start_times[name] = std::chrono::high_resolution_clock::now();
        }

        void stop(const std::string& name) {
            auto end = std::chrono::high_resolution_clock::now();
            auto it = start_times.find(name);
            if (it != start_times.end()) {
                std::chrono::duration<double, std::milli> ms = end - it->second;
                stats[name].total_ms += ms.count();
                stats[name].count++;
            }
        }

        void printStats() {
            std::cout << "\n--- Profiling Stats ---" << std::endl;
            std::cout << std::left << std::setw(25) << "Name" 
                      << std::right << std::setw(15) << "Total (ms)" 
                      << std::setw(10) << "Calls" 
                      << std::setw(15) << "Avg (ms)" << std::endl;
            std::cout << std::string(65, '-') << std::endl;
            
            for (const auto& pair : stats) {
                double avg = pair.second.count > 0 ? pair.second.total_ms / pair.second.count : 0.0;
                std::cout << std::left << std::setw(25) << pair.first 
                          << std::right << std::setw(15) << std::fixed << std::setprecision(2) << pair.second.total_ms 
                          << std::setw(10) << pair.second.count 
                          << std::setw(15) << avg << std::endl;
            }
            std::cout << std::string(65, '-') << std::endl;
        }

    private:
        std::unordered_map<std::string, std::chrono::time_point<std::chrono::high_resolution_clock>> start_times;
        std::unordered_map<std::string, TimerStats> stats;
    };

    class ScopedTimer {
    public:
        ScopedTimer(const std::string& name) : name(name) {
            Profiler::instance().start(name);
        }
        ~ScopedTimer() {
            Profiler::instance().stop(name);
        }
    private:
        std::string name;
    };
}
