#include "api_autocomplete.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace cord19 {

void AutocompleteIndex::clear() {
    nodes_.clear();
    terms_.clear();
    scores_.clear();
    max_top_ = 10;
}

bool AutocompleteIndex::empty() const {
    return terms_.empty();
}

std::string AutocompleteIndex::normalize_token(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char uc : s) {
        if (std::isalnum(uc)) out.push_back((char)std::tolower(uc));
    }
    return out;
}

void AutocompleteIndex::update_top(std::vector<Cand>& top, const Cand& c) const {
    // De-duplicate by term_index
    for (auto& existing : top) {
        if (existing.term_index == c.term_index) {
            // keep the larger score
            if (c.score > existing.score) existing.score = c.score;
            goto sort_and_trim;
        }
    }
    top.push_back(c);

sort_and_trim:
    std::stable_sort(top.begin(), top.end(), [&](const Cand& a, const Cand& b) {
        if (a.score != b.score) return a.score > b.score;
        // tie-break lexicographically to be stable
        return terms_[a.term_index] < terms_[b.term_index];
    });

    if (top.size() > max_top_) top.resize(max_top_);
}

void AutocompleteIndex::insert_term(uint32_t term_index) {
    const std::string& term = terms_[term_index];
    uint32_t score = scores_[term_index];

    uint32_t node = 0; // root
    // Root also keeps top list for empty prefix if you want it later
    update_top(nodes_[node].top, Cand{term_index, score});

    for (unsigned char uc : term) {
        char c = (char)uc;
        auto it = nodes_[node].next.find(c);
        if (it == nodes_[node].next.end()) {
            uint32_t new_node = (uint32_t)nodes_.size();
            nodes_.push_back(Node{});
            nodes_[node].next.emplace(c, new_node);
            node = new_node;
        } else {
            node = it->second;
        }
        update_top(nodes_[node].top, Cand{term_index, score});
    }
}

void AutocompleteIndex::build(const std::unordered_map<std::string, uint32_t>& term_to_score,
                             size_t max_candidates_per_prefix) {
    clear();
    max_top_ = std::max<size_t>(1, max_candidates_per_prefix);

    terms_.reserve(term_to_score.size());
    scores_.reserve(term_to_score.size());
    nodes_.reserve(1 + term_to_score.size() * 2);
    nodes_.push_back(Node{}); // root

    // Copy + normalize tokens (keep only alnum lowercase), drop too-short ones
    for (const auto& kv : term_to_score) {
        std::string t = normalize_token(kv.first);
        if (t.size() < 2) continue;
        terms_.push_back(std::move(t));
        scores_.push_back(kv.second);
    }

    // Sort by score then term to keep deterministic output
    std::vector<uint32_t> order(terms_.size());
    for (uint32_t i = 0; i < (uint32_t)order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (scores_[a] != scores_[b]) return scores_[a] > scores_[b];
        return terms_[a] < terms_[b];
    });

    // Rebuild in that order
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

    for (uint32_t i = 0; i < (uint32_t)terms_.size(); i++) {
        insert_term(i);
    }
}

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

std::vector<std::string> AutocompleteIndex::suggest_query(const std::string& user_input,
                                                          size_t limit) const {
    std::vector<std::string> out;
    if (empty() || limit == 0) return out;

    // Split into: base (everything before last token) and last token prefix.
    // Token chars: [a-zA-Z0-9]; everything else is a separator.
    size_t n = user_input.size();
    size_t end = n;
    while (end > 0 && !std::isalnum((unsigned char)user_input[end - 1])) end--;

    size_t start = end;
    while (start > 0 && std::isalnum((unsigned char)user_input[start - 1])) start--;

    std::string base = user_input.substr(0, start);
    std::string last = user_input.substr(start, end - start);

    std::string prefix = normalize_token(last);
    if (prefix.empty()) return out;

    uint32_t node = 0;
    if (!lookup_node(prefix, node)) return out;

    const auto& top = nodes_[node].top;
    size_t m = std::min(limit, top.size());
    out.reserve(m);

    for (size_t i = 0; i < m; i++) {
        out.push_back(base + terms_[top[i].term_index]);
    }
    return out;
}

} // namespace cord19
