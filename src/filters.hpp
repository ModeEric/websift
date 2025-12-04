#pragma once

#include <string>
#include <vector>
#include <regex>
#include <set>
#include <unordered_set>
#include <array>

struct FilterResult {
    bool keep;
    std::string reason;
};

class C4QualityFilter {
public:
    C4QualityFilter();
    FilterResult filter(std::string& text); // Modifies text (filters lines)

private:
    bool split_paragraph = true;
    bool remove_citations = true;
    bool filter_no_terminal_punct = true;
    int min_num_sentences = 5;
    int min_words_per_line = 3;
    int max_word_length = 1000;
    bool filter_lorem_ipsum = true;
    bool filter_javascript = true;
    bool filter_curly_bracket = true;
    bool filter_policy = true;

    std::regex citation_regex;
    std::vector<std::string> policy_substrings;
    std::set<char> end_punctuation;
};

class C4ParagraphFilter {
public:
    C4ParagraphFilter();
    FilterResult filter(const std::string& text);

private:
    int min_paragraphs = 3;
    int min_paragraph_len = 200;
};

class C4BadWordsFilter {
public:
    C4BadWordsFilter();
    FilterResult filter(const std::string& text);

private:
    // For this implementation, we'll use a simple list for "en"
    std::vector<std::string> badwords;
    void loadBadWords();
};

class GopherQualityFilter {
public:
    GopherQualityFilter(
        int min_doc_words = 50,
        int max_doc_words = 100000,
        int min_avg_word_length = 3,
        int max_avg_word_length = 10,
        double max_symbol_word_ratio = 0.1,
        double max_bullet_lines_ratio = 0.9,
        double max_ellipsis_lines_ratio = 0.3,
        double max_non_alpha_words_ratio = 0.8,
        int min_stop_words = 2,
        const std::vector<std::string>& stop_words = {}
    );

    FilterResult filter(const std::string& text) const;

private:
    struct TransparentHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
        size_t operator()(const std::string& s) const { return std::hash<std::string_view>{}(s); }
    };
    struct TransparentEq {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const { return a == b; }
    };

    int min_doc_words_;
    int max_doc_words_;
    int min_avg_word_length_;
    int max_avg_word_length_;
    double max_symbol_word_ratio_;
    double max_bullet_lines_ratio_;
    double max_ellipsis_lines_ratio_;
    double max_non_alpha_words_ratio_;
    int min_stop_words_;

    static constexpr size_t kStopBucketMaxLen = 32;
    bool stop_check_enabled_ = true;
    bool using_default_stopwords_ = true;
    std::vector<std::string> stop_words_vec_;
    std::array<std::vector<std::string>, kStopBucketMaxLen + 1> stop_buckets_;
    std::vector<std::string> stop_long_;
    // Bitmask tables for fast char classification (256 bits each)
    std::array<uint64_t, 4> punctuation_mask_{}; // 4 * 64 = 256 bits
    std::array<uint64_t, 4> space_mask_{};
    std::array<uint64_t, 4> alpha_mask_{};

    inline bool isPunct(unsigned char c) const {
        return (punctuation_mask_[c >> 6] >> (c & 63)) & 1ULL;
    }
    inline bool isSpace(unsigned char c) const {
        return (space_mask_[c >> 6] >> (c & 63)) & 1ULL;
    }
    inline bool isAlpha(unsigned char c) const {
        return (alpha_mask_[c >> 6] >> (c & 63)) & 1ULL;
    }

    bool isStopWord(const char* w, size_t len) const;
    static constexpr bool kHasMinStopWordsDefault = true;
};

