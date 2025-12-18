#include "api_autocomplete.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace cord19 {

// Clear all autocomplete data and reset defaults
void AutocompleteIndex::clear() {
    nodes_.clear();
    terms_.clear();
    scores_.clear();
    max_top_ = 10;
}

// Check if autocomplete index is empty
bool AutocompleteIndex::empty() const {
    return terms_.empty();
}

// Normalize token by keeping only lowercase alphanumeric characters
std::string AutocompleteIndex::normalize_token(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char uc : s) {
        if (std::isalnum(uc)) out.push_back((char)std::tolower(uc));
    }
    return out;
}

// Update top candidate list for a prefix node
void AutocompleteIndex::update_top(std::vector<Cand>& top, const Cand& c) const {

    // Remove duplicate term entries and keep higher score
    for (auto& existing : top) {
        if (existing.term_index == c.term_index) {
            if (c.score > existing.score) existing.score = c.score;
            goto sort_and_trim;
        }
    }

    // Add new candidate if not already present
    top.push_back(c);

sort_and_trim:
    // Sort candidates by score and then alphabetically
    std::stable_sort(top.begin(), top.end(), [&](const Cand& a, const Cand& b) {
        if (a.score != b.score) return a.score > b.score;
        return terms_[a.term_index] < terms_[b.term_index];
    });

    // Keep only top N candidates
    if (top.size() > max_top_) top.resize(max_top_);
}

// Insert a term into the autocomplete trie
void AutocompleteIndex::insert_term(uint32_t term_index) {
    const std::string& term = terms_[term_index];
    uint32_t score = scores_[term_index];

    // Start from root node
    uint32_t node = 0;

    // Update root suggestions
    update_top(nodes_[node].top, Cand{term_index, score});

    // Walk through each character of the term
    for (unsigned char uc : term) {
        char c = (char)uc;
        auto it = nodes_[node].next.find(c);

        // Create new node if path does not exist
        if (it == nodes_[node].next.end()) {
            uint32_t new_node = (uint32_t)nodes_.size();
            nodes_.push_back(Node{});
            nodes_[node].next.emplace(c, new_node);
            node = new_node;
        } else {
            node = it->second;
        }

        // Update suggestions for this prefix
        update_top(nodes_[node].top, Cand{term_index, score});
    }
}

// Build autocomplete index from term-to-score map
void AutocompleteIndex::build(const std::unordered_map<std::string, uint32_t>& term_to_score,
                             size_t max_candidates_per_prefix) {

    // Reset existing data
    clear();
    max_top_ = std::max<size_t>(1, max_candidates_per_prefix);

    // Reserve memory for performance
    terms_.reserve(term_to_score.size());
    scores_.reserve(term_to_score.size());
    nodes_.reserve(1 + term_to_score.size() * 2);

    // Create root trie node
    nodes_.push_back(Node{});

    // Normalize and store valid terms
    for (const auto& kv : term_to_score) {
        std::string t = normalize_token(kv.first);
        if (t.size() < 2) continue;
        terms_.push_back(std::move(t));
        scores_.push_back(kv.second);
    }

    // Create index order sorted by score and term
    std::vector<uint32_t> order(terms_.size());
    for (uint32_t i = 0; i < (uint32_t)order.size(); i++) order[i] = i;

    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (scores_[a] != scores_[b]) return scores_[a] > scores_[b];
        return terms_[a] < terms_[b];
    });

    // Reorder terms and scores for deterministic output
    std::vector<std::string> terms2;
    std::vector<uint32_t> scores2;
    terms2.reserve(terms_.size());
    scores2.reserve(scores_.size());

    for (uint32_t idx : order) {
        terms2.push_back(std::move(terms_[idx]));
        scores2.push_back(scores_[idx]);
    }

    terms_.swap(terms2);
    scores_.swap(scores2);

    // Insert all terms into the trie
    for (uint32_t i = 0; i < (uint32_t)terms_.size(); i++) {
        insert_term(i);
    }
}

// Find trie node for a given normalized prefix
bool AutocompleteIndex::lookup_node(const std::string& prefix_norm, uint32_t& node_id) const {
    node_id = 0;
    for (unsigned char uc : prefix_norm) {
        char c = (char)uc;
        auto it = nodes_[node_id].next.find(c);
        if (it == nodes_[node_id].next.end()) return false;
        node_id = it->second;
    }
    return true;
}

// Generate autocomplete suggestions for user query
std::vector<std::string> AutocompleteIndex::suggest_query(const std::string& user_input,
                                                          size_t limit) const {
    std::vector<std::string> out;
    if (empty() || limit == 0) return out;

    // Find last token in user input
    size_t n = user_input.size();
    size_t end = n;
    while (end > 0 && !std::isalnum((unsigned char)user_input[end - 1])) end--;

    size_t start = end;
    while (start > 0 && std::isalnum((unsigned char)user_input[start - 1])) start--;

    // Split base text and last token
    std::string base = user_input.substr(0, start);
    std::string last = user_input.substr(start, end - start);

    // Normalize prefix token
    std::string prefix = normalize_token(last);
    if (prefix.empty()) return out;

    // Locate trie node for prefix
    uint32_t node = 0;
    if (!lookup_node(prefix, node)) return out;

    // Collect top suggestions
    const auto& top = nodes_[node].top;
    size_t m = std::min(limit, top.size());
    out.reserve(m);

    // Build final suggestion strings
    for (size_t i = 0; i < m; i++) {
        out.push_back(base + terms_[top[i].term_index]);
    }

    return out;
}

} // namespace cord19
