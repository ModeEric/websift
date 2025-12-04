#pragma once

#include <string>
#include <vector>
#include <regex>
#include <set>

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

