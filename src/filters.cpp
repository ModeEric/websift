#include "filters.hpp"
#include "utils.hpp"
#include <iostream>
#include <fstream>
#include <cstdlib>

// C4QualityFilter

C4QualityFilter::C4QualityFilter() 
{
    policy_substrings = {
        "terms of use",
        "privacy policy",
        "cookie policy",
        "uses cookies",
        "use of cookies",
        "use cookies"
    };
    end_punctuation = {'.', '?', '!', '"', '\''};
}

// Helper to remove citations manually: [123], [edit], [citation needed]
std::string removeCitations(const std::string& line) {
    std::string res;
    res.reserve(line.size());
    size_t n = line.size();
    for (size_t i = 0; i < n; ++i) {
        if (line[i] == '[') {
            // Check for [citation needed]
            if (i + 16 < n && line.compare(i, 17, "[citation needed]") == 0) {
                i += 16; 
                continue;
            }
            // Check for [edit]
            if (i + 5 < n && line.compare(i, 6, "[edit]") == 0) {
                i += 5;
                continue;
            }
            // Check for [\d*] (numbers)
            // Scan forward for digits then ]
            size_t j = i + 1;
            while (j < n && isdigit(line[j])) {
                j++;
            }
            if (j < n && line[j] == ']') {
                i = j;
                continue;
            }
        }
        res += line[i];
    }
    return res;
}

// Helper to analyze words without splitting
struct LineStats {
    int word_count = 0;
    bool has_long_word = false;
};

LineStats analyzeLine(const std::string& line, int max_len) {
    LineStats stats;
    size_t n = line.size();
    size_t i = 0;
    while (i < n) {
        // skip spaces
        while (i < n && isspace(line[i])) i++;
        if (i == n) break;
        
        // word start
        size_t start = i;
        while (i < n && !isspace(line[i])) i++;
        
        size_t len = i - start;
        stats.word_count++;
        if (max_len != -1 && (int)len > max_len) {
            stats.has_long_word = true;
        }
    }
    return stats;
}

FilterResult C4QualityFilter::filter(std::string& text) {
    auto lines = Utils::splitLines(text);
    std::vector<std::string> kept_lines;
    int num_sentences = 0; 

    for (auto& line : lines) {
        // Strip whitespace
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue; 
        size_t last = line.find_last_not_of(" \t");
        line = line.substr(first, (last - first + 1));

        // Analyze words first? No, we need to remove citations first as that changes words?
        // The Python code removes citations FIRST.
        
        if (remove_citations) {
             line = removeCitations(line);
             // Re-strip
             first = line.find_first_not_of(" \t");
             if (first == std::string::npos) continue;
             last = line.find_last_not_of(" \t");
             line = line.substr(first, (last - first + 1));
        }

        // Now analyze words
        LineStats stats = analyzeLine(line, max_word_length);
        
        if (stats.has_long_word) continue;
        if (stats.word_count < min_words_per_line) continue;

        // end punctuation
        if (filter_no_terminal_punct) {
            char last_char = line.back();
            bool has_end_punct = end_punctuation.count(last_char);
            bool ends_ellipsis = (line.length() >= 3 && line.substr(line.length()-3) == "...");
            
            if (!has_end_punct || ends_ellipsis) continue;
        }

        std::string line_l = Utils::toLower(line);

        // lorem ipsum
        if (filter_lorem_ipsum && line_l.find("lorem ipsum") != std::string::npos) {
            return {false, "lorem_ipsum"};
        }

        // javascript
        if (filter_javascript && line_l.find("javascript") != std::string::npos) {
            continue;
        }

        // curly bracket
        if (filter_curly_bracket && line.find('{') != std::string::npos) {
            return {false, "curly_bracket"};
        }

        // policy
        if (filter_policy) {
            bool has_policy = false;
            for (const auto& p : policy_substrings) {
                if (line_l.find(p) != std::string::npos) {
                    has_policy = true;
                    break;
                }
            }
            if (has_policy) continue;
        }

        if (min_num_sentences != -1) {
            num_sentences++;
        }
        kept_lines.push_back(line);
    }

    if (num_sentences < min_num_sentences) {
        return {false, "too_few_sentences"};
    }

    std::ostringstream oss;
    for (size_t i = 0; i < kept_lines.size(); ++i) {
        oss << kept_lines[i];
        if (i < kept_lines.size() - 1) oss << "\n";
    }
    text = oss.str();

    return {true, ""};
}

// C4ParagraphFilter
C4ParagraphFilter::C4ParagraphFilter() {}
FilterResult C4ParagraphFilter::filter(const std::string& text) {
    auto lines = Utils::splitLines(text);
    if ((int)lines.size() < min_paragraphs) {
        return {false, "< min_paragraphs"};
    }
    std::vector<size_t> lengths;
    lengths.reserve(lines.size());
    for (const auto& l : lines) lengths.push_back(l.length());
    // Only sort top 3? Partial sort is better.
    if (lengths.size() >= 3) {
        std::partial_sort(lengths.begin(), lengths.begin() + 3, lengths.end(), std::greater<size_t>());
        if (lengths[2] < (size_t)min_paragraph_len) {
            return {false, "top 3 paragraphs too short"};
        }
    } else {
        return {false, "< 3 paragraphs (logic check)"};
    }
    return {true, ""};
}

// C4BadWordsFilter
C4BadWordsFilter::C4BadWordsFilter() {
    loadBadWords();
}

void C4BadWordsFilter::loadBadWords() {
    std::ifstream f("badwords_en.txt");
    if (f.good()) {
        std::string line;
        while (std::getline(f, line)) {
            line.erase(line.find_last_not_of(" \n\r\t")+1);
            if (!line.empty()) badwords.push_back(line);
        }
    } else {
        badwords = {"porn", "xxx", "sex"}; 
    }
}

FilterResult C4BadWordsFilter::filter(const std::string& text) {
    std::string text_l = Utils::toLower(text);
    for (const auto& bw : badwords) {
        size_t pos = 0;
        while ((pos = text_l.find(bw, pos)) != std::string::npos) {
            bool left_ok = (pos == 0) || !isalnum(text_l[pos-1]);
            bool right_ok = (pos + bw.length() == text_l.length()) || !isalnum(text_l[pos + bw.length()]);
            if (left_ok && right_ok) {
                return {false, "badword: " + bw};
            }
            pos += 1;
        }
    }
    return {true, ""};
}
