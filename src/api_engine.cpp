#include "api_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <queue>
#include <unordered_set>

// Include metadata, segment, IO, and text utilities
#include "api_metadata.hpp"
#include "api_segment.hpp"
#include "indexio.hpp"
#include "textutil.hpp"

namespace cord19 {

// Compute BM25 IDF value from total docs and document frequency
static float bm25_idf(uint32_t N, uint32_t df) {
    return std::log((((N - df + 0.5f) / (df + 0.5f)) + 1.0f));
}

// Reload index segments, autocomplete, metadata, and optional embeddings
bool Engine::reload() {
    std::cerr << "[reload] metadata map size: " << uid_to_meta.size() << "\n";

    // Lock engine during reload to avoid concurrent reads/writes
    std::lock_guard<std::mutex> lock(mtx);

    // Load segment names from manifest file
    seg_names = load_manifest(index_dir / "manifest.bin");
    if (seg_names.empty()) {
        // Fallback: scan segments directory if manifest is missing/empty
        fs::path segroot = index_dir / "segments";
        if (fs::exists(segroot) && fs::is_directory(segroot)) {
            seg_names.clear();
            for (auto& e : fs::directory_iterator(segroot)) {
                if (!e.is_directory()) continue;
                auto name = e.path().filename().string();
                if (name.rfind("seg_", 0) == 0) seg_names.push_back(name);
            }
            std::sort(seg_names.begin(), seg_names.end());
        }
    }

    // Stop if no segments were found
    if (seg_names.empty()) return false;

    // Load all segments into memory
    std::vector<Segment> loaded;
    loaded.reserve(seg_names.size());

    for (auto& name : seg_names) {
        Segment s;
        fs::path segdir = index_dir / "segments" / name;
        if (!load_segment(segdir, s)) {
            std::cerr << "Failed to load segment: " << segdir << "\n";
            return false;
        }
        loaded.push_back(std::move(s));
    }

    // Replace engine segments with newly loaded segments
    segments = std::move(loaded);

    // Build autocomplete index using df scores from all segment lexicons
    {
        std::unordered_map<std::string, uint32_t> term_to_score;
        term_to_score.reserve(200000);

        // Sum df across segments for each term
        for (const auto& seg : segments) {
            for (const auto& kv : seg.lex) {
                const std::string& term = kv.first;
                const LexEntry& e = kv.second;
                term_to_score[term] += e.df;
            }
        }

        // Build autocomplete trie with top 10 candidates per prefix
        ac.build(term_to_score, 10);
    }

    // Reload metadata mapping from CSV
    uid_to_meta.clear();
    load_metadata_uid_meta(index_dir / "metadata.csv", uid_to_meta);

    // Reset semantic index and load embeddings if available
    sem = SemanticIndex();
    {
        // Collect only needed terms to reduce embedding memory usage
        std::unordered_set<std::string> needed_terms;
        needed_terms.reserve(250000);
        for (const auto& seg : segments) {
            for (const auto& kv : seg.lex) needed_terms.insert(kv.first);
        }

        // Decide embedding file path from env var or common filenames
        fs::path emb_path;
        if (const char* p = std::getenv("EMBEDDINGS_PATH")) {
            emb_path = fs::path(p);
        } else {
            const fs::path candidates[] = {
                index_dir / "embeddings.vec",
                index_dir / "embeddings.txt",
                index_dir / "glove.txt",
                index_dir / "vectors.txt"
            };
            for (const auto& c : candidates) {
                if (fs::exists(c)) { emb_path = c; break; }
            }
        }

        // Load embeddings from file if it exists
        if (!emb_path.empty() && fs::exists(emb_path)) {
            bool ok = sem.load_from_text(emb_path, needed_terms);
            if (ok) {
                std::cerr << "[reload] semantic embeddings loaded: "
                          << sem.terms.size() << " terms, dim=" << sem.dim
                          << " from " << emb_path.string() << "\n";
            } else {
                std::cerr << "[reload] embeddings file found but no usable vectors loaded: "
                          << emb_path.string() << " (semantic search disabled)\n";
            }
        }
    }

    // Reload successful
    return true;
}

// Return autocomplete suggestions as JSON
json Engine::suggest(const std::string& user_input, int limit) {

    // Lock engine during suggest
    std::lock_guard<std::mutex> lock(mtx);

    // Clamp suggestion limit to 1..10
    const int L = std::max(1, std::min(limit, 10));

    // Create response JSON structure
    json out;
    out["query"] = user_input;
    out["limit"] = L;
    out["suggestions"] = json::array();

    // Return empty if autocomplete index not built
    if (ac.empty()) return out;

    // Generate suggestions and add to JSON
    auto s = ac.suggest_query(user_input, (size_t)L);
    for (const auto& t : s) out["suggestions"].push_back(t);

    return out;
}

// Helper to create cache key from query and k
std::string Engine::make_cache_key(const std::string& query, int k) {
    return query + "|" + std::to_string(k);
}

// Check if cache entry is expired (older than 24 hours)
bool Engine::is_cache_entry_expired(const CacheEntry& entry) {
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::hours>(now - entry.timestamp);
    return age >= CACHE_EXPIRY_DURATION;
}

// Get result from cache if available and not expired, update LRU
json Engine::get_from_cache(const std::string& cache_key) {
    auto it = cache.find(cache_key);
    if (it == cache.end()) {
        return json(); // empty json means not found
    }
    
    // Check if entry is expired
    if (is_cache_entry_expired(it->second)) {
        // Remove expired entry
        lru_list.erase(it->second.lru_iter);
        cache.erase(it);
        return json(); // treat as cache miss
    }
    
    // Move to front of LRU list (most recently used)
    lru_list.erase(it->second.lru_iter);
    lru_list.push_front(cache_key);
    it->second.lru_iter = lru_list.begin();
    
    // Return a copy of cached result
    json result = it->second.result;
    result["from_cache"] = true;
    return result;
}

// Put result in cache with LRU eviction (expired entries evicted first)
void Engine::put_in_cache(const std::string& cache_key, const json& result) {
    auto now = std::chrono::steady_clock::now();
    
    // Check if already in cache (shouldn't happen, but handle it)
    auto it = cache.find(cache_key);
    if (it != cache.end()) {
        // Update existing entry with new timestamp
        lru_list.erase(it->second.lru_iter);
        lru_list.push_front(cache_key);
        it->second.result = result;
        it->second.lru_iter = lru_list.begin();
        it->second.timestamp = now;
        return;
    }
    
    // Evict if cache is full - prioritize expired entries
    if (cache.size() >= MAX_CACHE_SIZE) {
        std::string key_to_evict;
        bool found_expired = false;
        
        // First, try to find an expired entry (scan from back - oldest entries)
        for (auto lru_it = lru_list.rbegin(); lru_it != lru_list.rend(); ++lru_it) {
            auto cache_it = cache.find(*lru_it);
            if (cache_it != cache.end() && is_cache_entry_expired(cache_it->second)) {
                key_to_evict = *lru_it;
                found_expired = true;
                break;
            }
        }
        
        // If no expired entry found, evict LRU (least recently used)
        if (!found_expired) {
            key_to_evict = lru_list.back();
        }
        
        // Remove the selected entry
        auto evict_it = cache.find(key_to_evict);
        if (evict_it != cache.end()) {
            lru_list.erase(evict_it->second.lru_iter);
            cache.erase(evict_it);
        }
    }
    
    // Add new entry with timestamp
    lru_list.push_front(cache_key);
    CacheEntry entry;
    entry.result = result;
    entry.lru_iter = lru_list.begin();
    entry.timestamp = now;
    cache[cache_key] = entry;
}

// Get AI overview from cache if available and not expired, update LRU
json Engine::get_ai_from_cache(const std::string& cache_key) {
    auto it = ai_cache.find(cache_key);
    if (it == ai_cache.end()) {
        return json(); // empty json means not found
    }
    
    // Check if entry is expired
    if (is_cache_entry_expired(it->second)) {
        // Remove expired entry
        ai_lru_list.erase(it->second.lru_iter);
        ai_cache.erase(it);
        return json(); // treat as cache miss
    }
    
    // Move to front of LRU list (most recently used)
    ai_lru_list.erase(it->second.lru_iter);
    ai_lru_list.push_front(cache_key);
    it->second.lru_iter = ai_lru_list.begin();
    
    // Return a copy of cached result
    json result = it->second.result;
    result["from_cache"] = true;
    return result;
}

// Put AI overview in cache with LRU eviction (expired entries evicted first)
void Engine::put_ai_in_cache(const std::string& cache_key, const json& result) {
    auto now = std::chrono::steady_clock::now();
    
    // Check if already in cache (shouldn't happen, but handle it)
    auto it = ai_cache.find(cache_key);
    if (it != ai_cache.end()) {
        // Update existing entry with new timestamp
        ai_lru_list.erase(it->second.lru_iter);
        ai_lru_list.push_front(cache_key);
        it->second.result = result;
        it->second.lru_iter = ai_lru_list.begin();
        it->second.timestamp = now;
        return;
    }
    
    // Evict if cache is full - prioritize expired entries
    if (ai_cache.size() >= MAX_AI_CACHE_SIZE) {
        std::string key_to_evict;
        bool found_expired = false;
        
        // First, try to find an expired entry (scan from back - oldest entries)
        for (auto lru_it = ai_lru_list.rbegin(); lru_it != ai_lru_list.rend(); ++lru_it) {
            auto cache_it = ai_cache.find(*lru_it);
            if (cache_it != ai_cache.end() && is_cache_entry_expired(cache_it->second)) {
                key_to_evict = *lru_it;
                found_expired = true;
                break;
            }
        }
        
        // If no expired entry found, evict LRU (least recently used)
        if (!found_expired) {
            key_to_evict = ai_lru_list.back();
        }
        
        // Remove the selected entry
        auto evict_it = ai_cache.find(key_to_evict);
        if (evict_it != ai_cache.end()) {
            ai_lru_list.erase(evict_it->second.lru_iter);
            ai_cache.erase(evict_it);
        }
    }
    
    // Add new entry with timestamp
    ai_lru_list.push_front(cache_key);
    CacheEntry entry;
    entry.result = result;
    entry.lru_iter = ai_lru_list.begin();
    entry.timestamp = now;
    ai_cache[cache_key] = entry;
}

// Run BM25 search with optional semantic expansion and return JSON results
json Engine::search(const std::string& query, int k) {

    // Lock engine during search
    std::lock_guard<std::mutex> lock(mtx);

    // Set BM25 parameters and clamp result count to 1..100
    const float k1 = 1.2f;
    const float b = 0.75f;
    const int K = std::max(1, std::min(k, 100));
    
    // Check cache first
    std::string cache_key = make_cache_key(query, K);
    json cached = get_from_cache(cache_key);
    if (!cached.is_null()) {
        // Return cached result with from_cache flag
        return cached;
    }

    // Tokenize the query string
    auto qtoks = tokenize(query);

    // Build base query terms by removing stopwords and short tokens
    std::vector<std::string> base_terms;
    base_terms.reserve(qtoks.size());
    for (auto& t : qtoks) {
        if (t.size() < 2) continue;
        if (is_stopword(t)) continue;
        base_terms.push_back(t);
    }

    // Prepare output JSON structure
    json out;
    out["query"] = query;
    out["k"] = K;
    out["segments"] = (int)segments.size();
    out["results"] = json::array();

    // Return empty if no usable terms or no segments loaded
    if (base_terms.empty() || segments.empty()) return out;

    // Expand query using embeddings if semantic search is enabled
    std::vector<std::pair<std::string, float>> qterms_w;
    if (sem.enabled) {
        qterms_w = sem.expand(base_terms,
                              /*per_term*/ 3,
                              /*global_topk*/ 5,
                              /*min_sim*/ 0.55f,
                              /*alpha*/ 0.6f,
                              /*max_total_terms*/ 40);
    } else {
        qterms_w.reserve(base_terms.size());
        for (const auto& t : base_terms) qterms_w.push_back({t, 1.0f});
    }

    // Return empty if expansion produced no terms
    if (qterms_w.empty()) return out;

    // Define a hit record to keep (score, segment, doc)
    struct Hit {
        float s;
        uint32_t segId;
        uint32_t docId;
    };

    // Use a min-heap to keep only top K hits
    auto cmp = [](const Hit& a, const Hit& b) { return a.s > b.s; };
    std::priority_queue<Hit, std::vector<Hit>, decltype(cmp)> pq(cmp);

    // Count how many docs matched across all segments
    uint64_t total_found = 0;

    // Score documents segment by segment
    for (uint32_t segId = 0; segId < (uint32_t)segments.size(); segId++) {
        auto& seg = segments[segId];

        // Store BM25 scores per docId inside this segment
        std::unordered_map<uint32_t, float> score;
        score.reserve(20000);

        // Process each weighted query term
        for (const auto& tw : qterms_w) {
            const std::string& term = tw.first;
            const float qweight = tw.second;

            // Skip term if not found in this segment lexicon
            auto it = seg.lex.find(term);
            if (it == seg.lex.end()) continue;

            const LexEntry& e = it->second;
            if (e.df == 0) continue;

            // Compute IDF using segment document count and df
            float idf = bm25_idf(seg.N, e.df);

            // Pick correct inverted file stream (barrels or single file)
            std::ifstream* invp = nullptr;
            if (seg.use_barrels) invp = &seg.inv_barrels[e.barrelId];
            else invp = &seg.inv;

            // Seek to posting list position for this term
            invp->clear();
            invp->seekg((std::streamoff)e.offset, std::ios::beg);

            // Read postings and accumulate BM25 score per doc
            for (uint32_t i = 0; i < e.count; i++) {
                uint32_t docId = read_u32(*invp);
                uint32_t tf = read_u32(*invp);

                float dl = (float)seg.docs[docId].doc_len;
                float denom = (float)tf + k1 * (1.0f - b + b * (dl / seg.avgdl));
                float s = idf * ((float)tf * (k1 + 1.0f)) / denom;
                score[docId] += qweight * s;
            }
        }

        // Push top scoring docs from this segment into global heap
        for (auto& kv : score) {
            Hit h{kv.second, segId, kv.first};
            if ((int)pq.size() < K) pq.push(h);
            else if (h.s > pq.top().s) {
                pq.pop();
                pq.push(h);
            }
        }

        // Add count of matched docs from this segment
        total_found += (uint64_t)score.size();
    }

    // Extract hits from heap into sorted list (highest score first)
    std::vector<Hit> hits;
    while (!pq.empty()) {
        hits.push_back(pq.top());
        pq.pop();
    }
    std::reverse(hits.begin(), hits.end());
    out["found"] = total_found;

    // Convert hits into JSON output entries
    for (auto& h : hits) {
        auto& d = segments[h.segId].docs[h.docId];
        json r;
        r["score"] = h.s;
        r["segment"] = seg_names[h.segId];
        r["docId"] = h.docId;
        r["cord_uid"] = d.cord_uid;
        r["title"] = d.title;
        r["json_relpath"] = d.json_relpath;

        // Attach metadata fields if available for this document
        auto it = uid_to_meta.find(d.cord_uid);
        if (it != uid_to_meta.end()) {
            std::string url = it->second.url;
            auto semi = url.find(';');
            if (semi != std::string::npos) url = url.substr(0, semi);
            if (!url.empty()) r["url"] = url;

            if (!it->second.publish_time.empty()) r["publish_time"] = it->second.publish_time;
            if (!it->second.author.empty()) r["author"] = it->second.author;
        }

        out["results"].push_back(r);
    }
    
    // Store result in cache before returning
    put_in_cache(cache_key, out);

    return out;
}

} // namespace cord19
