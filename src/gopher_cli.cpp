#include "filters.hpp"
#include <iostream>
#include <sstream>

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    std::string text = buffer.str();

    GopherQualityFilter filter;
    FilterResult res = filter.filter(text);

    std::cout << (res.keep ? "keep" : "drop") << "\t" << res.reason << std::endl;
    return 0;
}

