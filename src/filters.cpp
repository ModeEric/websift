#include "filters.hpp"
#include "utils.hpp"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string_view>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <cstring>

// In-place citation removal to avoid allocation
// Returns new length
size_t removeCitationsInPlace(char* str, size_t len) {
    size_t read = 0;
    size_t write = 0;
    while (read < len) {
        if (str[read] == '[') {
            // Check for [citation needed] (17 chars)
            if (read + 16 < len && std::string_view(str + read, 17) == "[citation needed]") {
                read += 17;
                continue;
            }
            // Check for [edit] (6 chars)
            if (read + 5 < len && std::string_view(str + read, 6) == "[edit]") {
                read += 6;
                continue;
            }
            // Check for [\d*]
            size_t j = read + 1;
            while (j < len && isdigit(static_cast<unsigned char>(str[j]))) {
                j++;
            }
            if (j < len && str[j] == ']') {
                read = j + 1;
                continue;
            }
        }
        str[write++] = str[read++];
    }
    return write;
}

struct LineStats {
    int word_count = 0;
    bool has_long_word = false;
};

// Analyze words in a string_view (single pass)
LineStats analyzeLine(std::string_view line, int max_len) {
    LineStats stats;
    size_t n = line.size();
    size_t i = 0;
    while (i < n) {
        // skip spaces
        while (i < n && isspace(static_cast<unsigned char>(line[i]))) i++;
        if (i == n) break;
        
        // word start
        size_t start = i;
        while (i < n && !isspace(static_cast<unsigned char>(line[i]))) i++;
        
        size_t len = i - start;
        stats.word_count++;
        if (max_len != -1 && (int)len > max_len) {
            stats.has_long_word = true;
        }
    }
    return stats;
}

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

    end_punct_table.fill(false);
    for (char c : {'.', '?', '!', '"', '\''}) {
        end_punct_table[static_cast<unsigned char>(c)] = true;
    }
}

FilterResult C4QualityFilter::filter(std::string& text) {
    std::string result;
    result.reserve(text.size());
    
    int num_sentences = 0;
    
    // Iterate over lines using string_view
    std::string_view text_view(text);
    size_t pos = 0;
    size_t len = text_view.size();
    
    // Temporary buffer for line processing to avoid repeated allocations
    std::string line_buf; 
    line_buf.reserve(8192);

    while (pos < len) {
        // Find end of line
        size_t next_pos = text_view.find('\n', pos);
        if (next_pos == std::string_view::npos) next_pos = len;
        
        // Get line view
        size_t line_len = next_pos - pos;
        // Handle CR if present
        if (line_len > 0 && text_view[pos + line_len - 1] == '\r') {
            line_len--;
        }
        
        std::string_view line_raw = text_view.substr(pos, line_len);
        pos = next_pos + 1; // Advance past newline

        // 1. Strip whitespace
        size_t first = line_raw.find_first_not_of(" \t");
        if (first == std::string_view::npos) continue; // Empty line
        size_t last = line_raw.find_last_not_of(" \t");
        line_raw = line_raw.substr(first, (last - first + 1));

        // 2. Check max word length on raw line first!
        LineStats raw_stats = analyzeLine(line_raw, max_word_length);
        if (raw_stats.has_long_word) {
            continue;
        }
        
        // Now remove citations
        line_buf.assign(line_raw.data(), line_raw.size());
        if (remove_citations) {
            size_t new_len = removeCitationsInPlace(line_buf.data(), line_buf.size());
            line_buf.resize(new_len);
            
            // Re-strip after citation removal
            size_t f = line_buf.find_first_not_of(" \t");
            if (f == std::string::npos) continue;
            size_t l = line_buf.find_last_not_of(" \t");
            if (l < line_buf.size() - 1) line_buf.erase(l + 1);
            if (f > 0) line_buf.erase(0, f);
        }
        
        // Check min words on modified line
        LineStats final_stats = analyzeLine(line_buf, -1);
        if (final_stats.word_count < min_words_per_line) continue;
        
        // Check terminal punct
        if (filter_no_terminal_punct) {
            char last_char = line_buf.back();
            bool has_end_punct = end_punct_table[static_cast<unsigned char>(last_char)];
            bool ends_ellipsis = (line_buf.length() >= 3 && line_buf.substr(line_buf.length()-3) == "...");
            
            if (!has_end_punct || ends_ellipsis) continue;
        }
        
        // Convert to lower for substring checks
        std::string line_l = Utils::toLower(line_buf);

        // lorem ipsum
        if (filter_lorem_ipsum && line_l.find("lorem ipsum") != std::string::npos) {
            return {false, "lorem_ipsum"};
        }
        // javascript
        if (filter_javascript && line_l.find("javascript") != std::string::npos) {
            continue;
        }
        // curly bracket (check original case)
        if (filter_curly_bracket && line_buf.find('{') != std::string::npos) {
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

        // Keep line
        if (min_num_sentences != -1) {
            num_sentences++;
        }
        
        if (!result.empty()) result += '\n';
        result += line_buf;
    }

    if (num_sentences < min_num_sentences) {
        return {false, "too_few_sentences"};
    }

    text = std::move(result);
    return {true, ""};
}

// C4ParagraphFilter (Unchanged)
C4ParagraphFilter::C4ParagraphFilter() {}
FilterResult C4ParagraphFilter::filter(const std::string& text) {
    auto lines = Utils::splitLines(text);
    if ((int)lines.size() < min_paragraphs) {
        return {false, "< min_paragraphs"};
    }
    std::vector<size_t> lengths;
    lengths.reserve(lines.size());
    for (const auto& l : lines) lengths.push_back(l.length());
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

// C4BadWordsFilter (Unchanged)
C4BadWordsFilter::C4BadWordsFilter() {
    loadBadWords();
}

void C4BadWordsFilter::loadBadWords() {
    std::ifstream f("badwords_en.txt");
    badwords.clear();

    if (f.good()) {
        badwords.reserve(512);
        std::string line;
        while (std::getline(f, line)) {
            size_t last = line.find_last_not_of(" \n\r\t");
            if (last == std::string::npos) continue;
            line.erase(last + 1);
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
            bool left_ok = (pos == 0) || !isalnum(static_cast<unsigned char>(text_l[pos-1]));
            bool right_ok = (pos + bw.length() == text_l.length()) || !isalnum(static_cast<unsigned char>(text_l[pos + bw.length()]));
            if (left_ok && right_ok) {
                return {false, "badword: " + bw};
            }
            pos += 1;
        }
    }
    return {true, ""};
}

// GopherQualityFilter
GopherQualityFilter::GopherQualityFilter(
    int min_doc_words,
    int max_doc_words,
    int min_avg_word_length,
    int max_avg_word_length,
    double max_symbol_word_ratio,
    double max_bullet_lines_ratio,
    double max_ellipsis_lines_ratio,
    double max_non_alpha_words_ratio,
    int min_stop_words,
    const std::vector<std::string>& stop_words
)
    : min_doc_words_(min_doc_words),
      max_doc_words_(max_doc_words),
      min_avg_word_length_(min_avg_word_length),
      max_avg_word_length_(max_avg_word_length),
      max_symbol_word_ratio_(max_symbol_word_ratio),
      max_bullet_lines_ratio_(max_bullet_lines_ratio),
      max_ellipsis_lines_ratio_(max_ellipsis_lines_ratio),
      max_non_alpha_words_ratio_(max_non_alpha_words_ratio),
      min_stop_words_(min_stop_words) {
    static const std::vector<std::string> DEFAULT_STOP_WORDS = {
        "the", "be", "to", "of", "and", "that", "have", "with"
    };
    if (stop_words.empty()) {
        stop_words_vec_ = DEFAULT_STOP_WORDS;
        using_default_stopwords_ = true;
    } else {
        stop_words_vec_ = stop_words;
        using_default_stopwords_ = false;
    }

    const std::string punctuation = R"(!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)";
    punctuation_table_.fill(false);
    for (unsigned char c : punctuation) {
        punctuation_table_[c] = true;
    }

    space_table_.fill(false);
    for (unsigned char c : std::string(" \t\n\r\f\v")) {
        space_table_[c] = true;
    }
    alpha_table_.fill(false);
    for (unsigned char c = 'A'; c <= 'Z'; ++c) alpha_table_[c] = true;
    for (unsigned char c = 'a'; c <= 'z'; ++c) alpha_table_[c] = true;
}

bool GopherQualityFilter::isStopWord(const char* w, size_t len) const {
    if (using_default_stopwords_) {
        switch (len) {
            case 2:
                return (w[0] == 'b' && w[1] == 'e') ||
                       (w[0] == 't' && w[1] == 'o') ||
                       (w[0] == 'o' && w[1] == 'f');
            case 3:
                return (w[0] == 't' && w[1] == 'h' && w[2] == 'e') ||
                       (w[0] == 'a' && w[1] == 'n' && w[2] == 'd');
            case 4:
                return (w[0] == 't' && w[1] == 'h' && w[2] == 'a' && w[3] == 't') ||
                       (w[0] == 'h' && w[1] == 'a' && w[2] == 'v' && w[3] == 'e') ||
                       (w[0] == 'w' && w[1] == 'i' && w[2] == 't' && w[3] == 'h');
            default:
                break;
        }
    }
    for (const auto& sw : stop_words_vec_) {
        if (sw.size() != len) continue;
        if (std::memcmp(sw.data(), w, len) == 0) return true;
    }
    return false;
}

FilterResult GopherQualityFilter::filter(const std::string& text) const {
    const char* data = text.data();
    const size_t n = text.size();

    // Tokenize on whitespace (like Python word_tokenize default whitespace splitter here)
    std::vector<std::string_view> words;
    size_t i = 0;
    while (i < n) {
        while (i < n && space_table_[static_cast<unsigned char>(data[i])]) i++;
        size_t start = i;
        while (i < n && !space_table_[static_cast<unsigned char>(data[i])]) i++;
        if (start < i) {
            words.emplace_back(data + start, i - start);
        }
    }

    const size_t n_words = words.size();
    size_t n_non_symbol_words = 0;
    size_t total_non_symbol_len = 0;
    size_t words_with_alpha = 0;
    size_t stop_word_count = 0;

    for (const auto& w : words) {
        bool non_symbol = false;
        bool has_alpha = false;
        for (char ch : w) {
            unsigned char uc = static_cast<unsigned char>(ch);
            if (!punctuation_table_[uc]) non_symbol = true;
            if (alpha_table_[uc]) has_alpha = true;
        }
        if (non_symbol) {
            n_non_symbol_words++;
            total_non_symbol_len += w.size();
        }
        if (has_alpha) {
            words_with_alpha++;
        }
        if (min_stop_words_ && isStopWord(w.data(), w.size())) {
            stop_word_count++;
        }
    }

    if (n_words == 0) {
        return {false, "gopher_short_doc"};
    }

    if (min_doc_words_ && static_cast<int>(n_non_symbol_words) < min_doc_words_) {
        return {false, "gopher_short_doc"};
    }
    if (max_doc_words_ && static_cast<int>(n_non_symbol_words) > max_doc_words_) {
        return {false, "gopher_long_doc"};
    }

    if (n_non_symbol_words > 0) {
        double avg_len = static_cast<double>(total_non_symbol_len) / static_cast<double>(n_non_symbol_words);
        if (min_avg_word_length_ && avg_len < min_avg_word_length_) {
            return {false, "gopher_below_avg_threshold"};
        }
        if (max_avg_word_length_ && avg_len > max_avg_word_length_) {
            return {false, "gopher_above_avg_threshold"};
        }
    } else if (min_avg_word_length_) {
        return {false, "gopher_below_avg_threshold"};
    }

    if (max_symbol_word_ratio_) {
        size_t hash_count = std::count(text.begin(), text.end(), '#');

        size_t ellipsis_tokens = 0;
        for (size_t pos = 0; (pos = text.find("...", pos)) != std::string::npos; pos += 3) {
            ellipsis_tokens++;
        }
        for (size_t pos = 0; (pos = text.find("\xE2\x80\xA6", pos)) != std::string::npos; pos += 3) {
            ellipsis_tokens++;
        }

        double hash_ratio = static_cast<double>(hash_count) / static_cast<double>(n_words);
        if (hash_ratio > max_symbol_word_ratio_) {
            return {false, "gopher_too_many_hashes"};
        }
        double ellipsis_ratio = static_cast<double>(ellipsis_tokens) / static_cast<double>(n_words);
        if (ellipsis_ratio > max_symbol_word_ratio_) {
            return {false, "gopher_too_many_ellipsis"};
        }
    }

    // Line-based checks
    std::vector<std::string_view> lines;
    size_t line_start = 0;
    for (size_t idx = 0; idx <= n; ++idx) {
        if (idx == n || data[idx] == '\n') {
            lines.emplace_back(data + line_start, idx - line_start);
            line_start = idx + 1;
        }
    }
    const size_t line_count = lines.size();
    if (line_count == 0) {
        return {false, "gopher_short_doc"};
    }

    size_t bullet_lines = 0;
    size_t ellipsis_lines = 0;
    for (auto line : lines) {
        size_t l = 0;
        while (l < line.size() && space_table_[static_cast<unsigned char>(line[l])]) l++;
        if (l < line.size()) {
            unsigned char c0 = static_cast<unsigned char>(line[l]);
            if (c0 == '-') {
                bullet_lines++;
            } else if (line.size() - l >= 3 &&
                       c0 == 0xE2 &&
                       static_cast<unsigned char>(line[l + 1]) == 0x80 &&
                       static_cast<unsigned char>(line[l + 2]) == 0xA2) {
                bullet_lines++;
            }
        }

        size_t r = line.size();
        while (r > 0 && space_table_[static_cast<unsigned char>(line[r - 1])]) r--;
        if (r >= 3) {
            const unsigned char* tail = reinterpret_cast<const unsigned char*>(line.data() + r - 3);
            if ((tail[0] == '.' && tail[1] == '.' && tail[2] == '.') ||
                (tail[0] == 0xE2 && tail[1] == 0x80 && tail[2] == 0xA6)) {
                ellipsis_lines++;
            }
        }
    }

    if (max_bullet_lines_ratio_) {
        double ratio = static_cast<double>(bullet_lines) / static_cast<double>(line_count);
        if (ratio > max_bullet_lines_ratio_) {
            return {false, "gopher_too_many_bullets"};
        }
    }

    if (max_ellipsis_lines_ratio_) {
        double ratio = static_cast<double>(ellipsis_lines) / static_cast<double>(line_count);
        if (ratio > max_ellipsis_lines_ratio_) {
            return {false, "gopher_too_many_end_ellipsis"};
        }
    }

    if (max_non_alpha_words_ratio_) {
        double ratio = static_cast<double>(words_with_alpha) / static_cast<double>(n_words);
        if (ratio < max_non_alpha_words_ratio_) {
            return {false, "gopher_below_alpha_threshold"};
        }
    }

    if (min_stop_words_) {
        if (static_cast<int>(stop_word_count) < min_stop_words_) {
            return {false, "gopher_enough_stop_words"};
        }
    }

    return {true, ""};
}
