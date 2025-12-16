#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <cctype>

inline std::string to_lower_ascii(std::string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Very simple tokenizer: keeps [a-z0-9] runs, lowercases
inline std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    cur.reserve(32);

    for (unsigned char uc : text) {
        char c = (char)uc;
        if (std::isalnum(uc)) {
            cur.push_back((char)std::tolower(uc));
        } else {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Optional tiny stoplist (add/remove as you want)
inline bool is_stopword(const std::string& t) {
    static const std::unordered_set<std::string> sw = {
        "the","a","an","and","or","of","to","in","for","on","with","by","as",
        "is","are","was","were","be","been","it","this","that","from","at"
    };
    return sw.find(t) != sw.end();
}
