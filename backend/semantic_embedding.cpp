#include "semantic_embedding.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace cord19 {

// Dot product for two vectors
static inline float dot(const float* a, const float* b, int d) {
    float s = 0.0f;
    for (int i = 0; i < d; ++i) s += a[i] * b[i];
    return s;
}

// Normalize a vector to unit length
void SemanticIndex::l2_normalize(std::vector<float>& v) {
    double ss = 0.0;
    for (float x : v) ss += (double)x * (double)x;
    double n = std::sqrt(ss);
    if (n <= 0.0) return;
    for (float& x : v) x = (float)(x / n);
}

// Get pointer to stored embedding vector for a term
const float* SemanticIndex::get_vec_ptr(const std::string& term) const {
    auto it = term_to_row.find(term);
    if (it == term_to_row.end()) return nullptr;
    uint32_t row = it->second;
    return &vecs[(size_t)row * (size_t)dim];
}

// Load embeddings from text file for selected terms
bool SemanticIndex::load_from_text(const fs::path& path,
                                  const std::unordered_set<std::string>& needed_terms) {
    enabled = false;
    dim = 0;
    terms.clear();
    vecs.clear();
    term_to_row.clear();

    // Open embedding file
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string line;
    bool first_line = true;
    size_t loaded = 0;

    // Detect optional header line like "400000 300"
    auto looks_like_header = [](const std::string& s) -> bool {
        std::istringstream iss(s);
        long long a, b;
        if (!(iss >> a >> b)) return false;
        std::string extra;
        if (iss >> extra) return false;
        return a > 0 && b > 0 && b < 5000;
    };

    // Parse embedding lines
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        // Skip header line if present
        if (first_line) {
            first_line = false;
            if (looks_like_header(line)) continue;
        }

        std::istringstream iss(line);
        std::string word;
        if (!(iss >> word)) continue;

        // Filter to needed terms only
        if (!needed_terms.empty() && needed_terms.find(word) == needed_terms.end()) {
            continue;
        }

        // Read vector values
        std::vector<float> v;
        float x;
        while (iss >> x) v.push_back(x);

        if (v.size() < 10) continue;
        if (dim == 0) dim = (int)v.size();
        if ((int)v.size() != dim) continue;

        // Store normalized vector
        l2_normalize(v);

        uint32_t row = (uint32_t)terms.size();
        terms.push_back(word);
        term_to_row.emplace(word, row);
        vecs.insert(vecs.end(), v.begin(), v.end());
        loaded++;
    }

    enabled = (loaded > 0 && dim > 0);
    return enabled;
}

// Find most similar stored vectors to a query vector
std::vector<std::pair<uint32_t, float>> SemanticIndex::most_similar_to_vec(
    const float* qvec,
    int topk,
    float min_sim,
    const std::unordered_set<uint32_t>* banned_rows) const {
    std::vector<std::pair<uint32_t, float>> out;
    if (!enabled || dim <= 0 || !qvec || topk <= 0) return out;

    // Top-k heap storage
    struct Node { float sim; uint32_t row; };
    auto cmp = [](const Node& a, const Node& b) { return a.sim > b.sim; };
    std::vector<Node> heap;
    heap.reserve((size_t)topk);

    // Scan all rows and keep best matches
    const size_t nrows = terms.size();
    for (size_t r = 0; r < nrows; ++r) {
        uint32_t row = (uint32_t)r;
        if (banned_rows && banned_rows->find(row) != banned_rows->end()) continue;

        const float* v = &vecs[r * (size_t)dim];
        float sim = dot(qvec, v, dim);
        if (sim < min_sim) continue;

        if ((int)heap.size() < topk) {
            heap.push_back(Node{sim, row});
            std::push_heap(heap.begin(), heap.end(), cmp);
        } else if (sim > heap.front().sim) {
            std::pop_heap(heap.begin(), heap.end(), cmp);
            heap.back() = Node{sim, row};
            std::push_heap(heap.begin(), heap.end(), cmp);
        }
    }

    // Convert heap to sorted output
    std::sort_heap(heap.begin(), heap.end(), cmp);
    std::reverse(heap.begin(), heap.end());

    out.reserve(heap.size());
    for (const auto& n : heap) out.push_back({n.row, n.sim});
    return out;
}

// Expand query terms using nearest neighbors from embeddings
std::vector<std::pair<std::string, float>> SemanticIndex::expand(
    const std::vector<std::string>& query_terms,
    int per_term,
    int global_topk,
    float min_sim,
    float alpha,
    int max_total_terms) const {
    std::unordered_map<std::string, float> w;
    w.reserve((size_t)max_total_terms * 2);

    // Add original query terms
    for (const auto& t : query_terms) {
        if (!t.empty()) w[t] = 1.0f;
    }

    // Return base terms if semantic is disabled
    if (!enabled || dim <= 0 || query_terms.empty()) {
        std::vector<std::pair<std::string, float>> out;
        out.reserve(w.size());
        for (auto& kv : w) out.push_back(kv);
        return out;
    }

    // Build banned set for original terms
    std::unordered_set<uint32_t> banned;
    banned.reserve(query_terms.size() * 2);
    for (const auto& t : query_terms) {
        auto it = term_to_row.find(t);
        if (it != term_to_row.end()) banned.insert(it->second);
    }

    // Per-term neighbor expansion
    for (const auto& t : query_terms) {
        const float* v = get_vec_ptr(t);
        if (!v) continue;

        auto nn = most_similar_to_vec(v, per_term, min_sim, &banned);
        for (auto& [row, sim] : nn) {
            const std::string& cand = terms[row];
            float weight = std::max(0.0f, std::min(alpha, alpha * sim));
            auto it = w.find(cand);
            if (it == w.end() || weight > it->second) w[cand] = weight;
        }
    }

    // Global centroid expansion
    if (global_topk > 0) {
        std::vector<float> q((size_t)dim, 0.0f);
        int cnt = 0;

        for (const auto& t : query_terms) {
            const float* v = get_vec_ptr(t);
            if (!v) continue;
            for (int j = 0; j < dim; ++j) q[(size_t)j] += v[j];
            cnt++;
        }

        if (cnt > 0) {
            for (int j = 0; j < dim; ++j) q[(size_t)j] /= (float)cnt;
            l2_normalize(q);

            auto nn = most_similar_to_vec(q.data(), global_topk, min_sim, &banned);
            for (auto& [row, sim] : nn) {
                const std::string& cand = terms[row];
                float weight = std::max(0.0f, std::min(alpha * 0.8f, alpha * 0.8f * sim));
                auto it = w.find(cand);
                if (it == w.end() || weight > it->second) w[cand] = weight;
            }
        }
    }

    // Convert map to sorted list
    std::vector<std::pair<std::string, float>> out;
    out.reserve(w.size());
    for (auto& kv : w) out.push_back(kv);

    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    if ((int)out.size() > max_total_terms) out.resize((size_t)max_total_terms);
    return out;
}

} // namespace cord19
