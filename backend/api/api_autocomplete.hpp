#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cord19 {

// Trie-based autocomplete index.
//
// Notes:
// - Terms are assumed to be single tokens (your lexicon terms are tokens).
// - Scores rank suggestions (higher score first).
// - Each trie node stores a small "top list" so lookup is O(|prefix|).
class AutocompleteIndex {
public:
    void clear();
    bool empty() const;

    void build(const std::unordered_map<std::string, uint32_t>& term_to_score,
               size_t max_candidates_per_prefix = 10);

    // Returns full query suggestions for user_input.
    // For multi-word input, completes only the last token and preserves the prefix part.
    std::vector<std::string> suggest_query(const std::string& user_input,
                                          size_t limit = 5) const;

private:
    struct Cand {
        uint32_t term_index = 0;
        uint32_t score = 0;
    };

    struct Node {
        std::unordered_map<char, uint32_t> next;
        std::vector<Cand> top;
    };

    std::vector<Node> nodes_;
    std::vector<std::string> terms_;
    std::vector<uint32_t> scores_;
    size_t max_top_ = 10;

    void insert_term(uint32_t term_index);
    void update_top(std::vector<Cand>& top, const Cand& c) const;
    bool lookup_node(const std::string& prefix_norm, uint32_t& node_id) const;
    static std::string normalize_token(const std::string& s);
};

} // namespace cord19
