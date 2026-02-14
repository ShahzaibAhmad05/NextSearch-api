#include "api_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#include <queue>
#include <unordered_set>

// Include metadata, segment, IO, and text utilities
#include "api_metadata.hpp"

using namespace cord19;

// Destructor: save all caches before engine is destroyed
Engine::~Engine() {
    std::lock_guard<std::mutex> lock(mtx);
    
    // Save search cache if there are unsaved updates
    if (cache_updates_since_save > 0 || !cache.empty()) {
        std::cerr << "[cache] Saving search cache on shutdown...\n";
        save_cache();
    }
    
    // Save AI overview cache if there are unsaved updates
    if (ai_overview_cache_updates_since_save > 0 || !ai_overview_cache.empty()) {
        std::cerr << "[cache] Saving AI overview cache on shutdown...\n";
        save_ai_overview_cache();
    }
    
    // Save AI summary cache if there are unsaved updates
    if (ai_summary_cache_updates_since_save > 0 || !ai_summary_cache.empty()) {
        std::cerr << "[cache] Saving AI summary cache on shutdown...\n";
        save_ai_summary_cache();
    }
}
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
    metadata_csv_path = index_dir / "metadata.csv";
    load_metadata_uid_meta(metadata_csv_path, uid_to_meta);

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

    // Load all caches from disk
    load_cache();
    load_ai_overview_cache();
    load_ai_summary_cache();

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

// Get result from cache if available, update LRU
json Engine::get_from_cache(const std::string& cache_key) {
    auto it = cache.find(cache_key);
    if (it == cache.end()) {
        return json(); // empty json means not found
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

// Put result in cache with LRU eviction
void Engine::put_in_cache(const std::string& cache_key, const json& result) {
    // Check if already in cache (shouldn't happen, but handle it)
    auto it = cache.find(cache_key);
    if (it != cache.end()) {
        // Update existing entry
        lru_list.erase(it->second.lru_iter);
        lru_list.push_front(cache_key);
        it->second.result = result;
        it->second.lru_iter = lru_list.begin();
        return;
    }
    
    // Evict if cache is full - evict LRU (least recently used)
    if (cache.size() >= MAX_CACHE_SIZE) {
        std::string key_to_evict = lru_list.back();
        
        // Remove the LRU entry
        auto evict_it = cache.find(key_to_evict);
        if (evict_it != cache.end()) {
            lru_list.erase(evict_it->second.lru_iter);
            cache.erase(evict_it);
        }
    }
    
    // Add new entry
    lru_list.push_front(cache_key);
    CacheEntry entry;
    entry.result = result;
    entry.lru_iter = lru_list.begin();
    cache[cache_key] = entry;
    
    // Periodically save cache to disk (every N updates)
    cache_updates_since_save++;
    if (cache_updates_since_save >= CACHE_SAVE_INTERVAL) {
        save_cache();
        cache_updates_since_save = 0;
    }
}

// Get AI overview from cache if available, update LRU
json Engine::get_ai_overview_from_cache(const std::string& cache_key) {
    auto it = ai_overview_cache.find(cache_key);
    if (it == ai_overview_cache.end()) {
        return json(); // empty json means not found
    }
    
    // Move to front of LRU list (most recently used)
    ai_overview_lru_list.erase(it->second.lru_iter);
    ai_overview_lru_list.push_front(cache_key);
    it->second.lru_iter = ai_overview_lru_list.begin();
    
    // Return a copy of cached result
    json result = it->second.result;
    result["from_cache"] = true;
    return result;
}

// Put AI overview in cache with LRU eviction
void Engine::put_ai_overview_in_cache(const std::string& cache_key, const json& result) {
    // Check if already in cache (shouldn't happen, but handle it)
    auto it = ai_overview_cache.find(cache_key);
    if (it != ai_overview_cache.end()) {
        // Update existing entry
        ai_overview_lru_list.erase(it->second.lru_iter);
        ai_overview_lru_list.push_front(cache_key);
        it->second.result = result;
        it->second.lru_iter = ai_overview_lru_list.begin();
        return;
    }
    
    // Evict if cache is full - evict LRU (least recently used)
    if (ai_overview_cache.size() >= MAX_AI_OVERVIEW_CACHE_SIZE) {
        std::string key_to_evict = ai_overview_lru_list.back();
        
        // Remove the LRU entry
        auto evict_it = ai_overview_cache.find(key_to_evict);
        if (evict_it != ai_overview_cache.end()) {
            ai_overview_lru_list.erase(evict_it->second.lru_iter);
            ai_overview_cache.erase(evict_it);
        }
    }
    
    // Add new entry
    ai_overview_lru_list.push_front(cache_key);
    CacheEntry entry;
    entry.result = result;
    entry.lru_iter = ai_overview_lru_list.begin();
    ai_overview_cache[cache_key] = entry;
    
    // Periodically save AI overview cache to disk (every N updates)
    ai_overview_cache_updates_since_save++;
    if (ai_overview_cache_updates_since_save >= CACHE_SAVE_INTERVAL) {
        save_ai_overview_cache();
        ai_overview_cache_updates_since_save = 0;
    }
}

// Get AI summary from cache if available, update LRU
json Engine::get_ai_summary_from_cache(const std::string& cache_key) {
    auto it = ai_summary_cache.find(cache_key);
    if (it == ai_summary_cache.end()) {
        return json(); // empty json means not found
    }
    
    // Move to front of LRU list (most recently used)
    ai_summary_lru_list.erase(it->second.lru_iter);
    ai_summary_lru_list.push_front(cache_key);
    it->second.lru_iter = ai_summary_lru_list.begin();
    
    // Return a copy of cached result
    json result = it->second.result;
    result["from_cache"] = true;
    return result;
}

// Put AI summary in cache with LRU eviction
void Engine::put_ai_summary_in_cache(const std::string& cache_key, const json& result) {
    // Check if already in cache (shouldn't happen, but handle it)
    auto it = ai_summary_cache.find(cache_key);
    if (it != ai_summary_cache.end()) {
        // Update existing entry
        ai_summary_lru_list.erase(it->second.lru_iter);
        ai_summary_lru_list.push_front(cache_key);
        it->second.result = result;
        it->second.lru_iter = ai_summary_lru_list.begin();
        return;
    }
    
    // Evict if cache is full - evict LRU (least recently used)
    if (ai_summary_cache.size() >= MAX_AI_SUMMARY_CACHE_SIZE) {
        std::string key_to_evict = ai_summary_lru_list.back();
        
        // Remove the LRU entry
        auto evict_it = ai_summary_cache.find(key_to_evict);
        if (evict_it != ai_summary_cache.end()) {
            ai_summary_lru_list.erase(evict_it->second.lru_iter);
            ai_summary_cache.erase(evict_it);
        }
    }
    
    // Add new entry
    ai_summary_lru_list.push_front(cache_key);
    CacheEntry entry;
    entry.result = result;
    entry.lru_iter = ai_summary_lru_list.begin();
    ai_summary_cache[cache_key] = entry;
    
    // Periodically save AI summary cache to disk (every N updates)
    ai_summary_cache_updates_since_save++;
    if (ai_summary_cache_updates_since_save >= CACHE_SAVE_INTERVAL) {
        save_ai_summary_cache();
        ai_summary_cache_updates_since_save = 0;
    }
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

        // Fetch ALL metadata fields on-demand from file (title, url, author, etc.)
        auto it = uid_to_meta.find(d.cord_uid);
        if (it != uid_to_meta.end()) {
            // Fetch metadata on-demand from file
            MetaData meta = fetch_metadata(metadata_csv_path, it->second);
            
            // Add title from metadata (not from docs structure)
            if (!meta.title.empty()) r["title"] = meta.title;
            
            std::string url = meta.url;
            auto semi = url.find(';');
            if (semi != std::string::npos) url = url.substr(0, semi);
            if (!url.empty()) r["url"] = url;

            if (!meta.publish_time.empty()) r["publish_time"] = meta.publish_time;
            if (!meta.author.empty()) r["author"] = meta.author;
        }
        // Note: json_relpath removed - not needed in API response

        out["results"].push_back(r);
    }
    
    // Store result in cache before returning
    put_in_cache(cache_key, out);

    return out;
}

// Save search cache to JSON file
void Engine::save_cache() {
    try {
        json cache_json = json::array();
        
        // Serialize cache entries
        for (const auto& kv : cache) {
            const std::string& key = kv.first;
            const CacheEntry& entry = kv.second;
            
            json item;
            item["key"] = key;
            item["result"] = entry.result;
            
            cache_json.push_back(item);
        }
        
        // Write to file
        fs::path cache_file = "search_cache.json";
        std::ofstream ofs(cache_file);
        if (ofs.is_open()) {
            ofs << cache_json.dump(2);
            ofs.close();
            std::cerr << "[cache] Saved " << cache_json.size() << " search cache entries to " << cache_file << "\n";
        } else {
            std::cerr << "[cache] Failed to open " << cache_file << " for writing\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[cache] Error saving search cache: " << e.what() << "\n";
    }
}

// Load search cache from JSON file
void Engine::load_cache() {
    try {
        fs::path cache_file = "search_cache.json";
        
        if (!fs::exists(cache_file)) {
            std::cerr << "[cache] No search cache file found at " << cache_file << "\n";
            return;
        }
        
        std::ifstream ifs(cache_file);
        if (!ifs.is_open()) {
            std::cerr << "[cache] Failed to open " << cache_file << " for reading\n";
            return;
        }
        
        json cache_json;
        ifs >> cache_json;
        ifs.close();
        
        if (!cache_json.is_array()) {
            std::cerr << "[cache] Invalid cache file format (not an array)\n";
            return;
        }
        
        // Clear existing cache
        cache.clear();
        lru_list.clear();
        
        // Load entries
        size_t loaded = 0;
        
        for (const auto& item : cache_json) {
            if (!item.contains("key") || !item.contains("result")) {
                continue;
            }
            
            std::string key = item["key"];
            json result = item["result"];
            
            // Add to LRU list and cache
            lru_list.push_back(key);  // Add at back (older entries)
            CacheEntry entry;
            entry.result = result;
            entry.lru_iter = --lru_list.end();
            cache[key] = entry;
            loaded++;
        }
        
        std::cerr << "[cache] Loaded " << loaded << " search cache entries\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[cache] Error loading search cache: " << e.what() << "\n";
    }
}

// Save AI overview cache to JSON file
void Engine::save_ai_overview_cache() {
    try {
        json cache_json = json::array();
        
        // Serialize AI overview cache entries
        for (const auto& kv : ai_overview_cache) {
            const std::string& key = kv.first;
            const CacheEntry& entry = kv.second;
            
            json item;
            item["key"] = key;
            item["result"] = entry.result;
            
            cache_json.push_back(item);
        }
        
        // Write to file
        fs::path cache_file = "ai_overview_cache.json";
        std::ofstream ofs(cache_file);
        if (ofs.is_open()) {
            ofs << cache_json.dump(2);
            ofs.close();
            std::cerr << "[cache] Saved " << cache_json.size() << " AI overview cache entries to " << cache_file << "\n";
        } else {
            std::cerr << "[cache] Failed to open " << cache_file << " for writing\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[cache] Error saving AI overview cache: " << e.what() << "\n";
    }
}

// Load AI overview cache from JSON file
void Engine::load_ai_overview_cache() {
    try {
        fs::path cache_file = "ai_overview_cache.json";
        
        if (!fs::exists(cache_file)) {
            std::cerr << "[cache] No AI overview cache file found at " << cache_file << "\n";
            return;
        }
        
        std::ifstream ifs(cache_file);
        if (!ifs.is_open()) {
            std::cerr << "[cache] Failed to open " << cache_file << " for reading\n";
            return;
        }
        
        json cache_json;
        ifs >> cache_json;
        ifs.close();
        
        if (!cache_json.is_array()) {
            std::cerr << "[cache] Invalid AI overview cache file format (not an array)\n";
            return;
        }
        
        // Clear existing AI overview cache
        ai_overview_cache.clear();
        ai_overview_lru_list.clear();
        
        // Load entries
        size_t loaded = 0;
        
        for (const auto& item : cache_json) {
            if (!item.contains("key") || !item.contains("result")) {
                continue;
            }
            
            std::string key = item["key"];
            json result = item["result"];
            
            // Add to LRU list and cache
            ai_overview_lru_list.push_back(key);  // Add at back (older entries)
            CacheEntry entry;
            entry.result = result;
            entry.lru_iter = --ai_overview_lru_list.end();
            ai_overview_cache[key] = entry;
            loaded++;
        }
        
        std::cerr << "[cache] Loaded " << loaded << " AI overview cache entries\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[cache] Error loading AI overview cache: " << e.what() << "\n";
    }
}

// Save AI summary cache to JSON file
void Engine::save_ai_summary_cache() {
    try {
        json cache_json = json::array();
        
        // Serialize AI summary cache entries
        for (const auto& kv : ai_summary_cache) {
            const std::string& key = kv.first;
            const CacheEntry& entry = kv.second;
            
            json item;
            item["key"] = key;
            item["result"] = entry.result;
            
            cache_json.push_back(item);
        }
        
        // Write to file
        fs::path cache_file = "ai_summary_cache.json";
        std::ofstream ofs(cache_file);
        if (ofs.is_open()) {
            ofs << cache_json.dump(2);
            ofs.close();
            std::cerr << "[cache] Saved " << cache_json.size() << " AI summary cache entries to " << cache_file << "\n";
        } else {
            std::cerr << "[cache] Failed to open " << cache_file << " for writing\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[cache] Error saving AI summary cache: " << e.what() << "\n";
    }
}

// Load AI summary cache from JSON file
void Engine::load_ai_summary_cache() {
    try {
        fs::path cache_file = "ai_summary_cache.json";
        
        if (!fs::exists(cache_file)) {
            std::cerr << "[cache] No AI summary cache file found at " << cache_file << "\n";
            return;
        }
        
        std::ifstream ifs(cache_file);
        if (!ifs.is_open()) {
            std::cerr << "[cache] Failed to open " << cache_file << " for reading\n";
            return;
        }
        
        json cache_json;
        ifs >> cache_json;
        ifs.close();
        
        if (!cache_json.is_array()) {
            std::cerr << "[cache] Invalid AI summary cache file format (not an array)\n";
            return;
        }
        
        // Clear existing AI summary cache
        ai_summary_cache.clear();
        ai_summary_lru_list.clear();
        
        // Load entries
        size_t loaded = 0;
        
        for (const auto& item : cache_json) {
            if (!item.contains("key") || !item.contains("result")) {
                continue;
            }
            
            std::string key = item["key"];
            json result = item["result"];
            
            // Add to LRU list and cache
            ai_summary_lru_list.push_back(key);  // Add at back (older entries)
            CacheEntry entry;
            entry.result = result;
            entry.lru_iter = --ai_summary_lru_list.end();
            ai_summary_cache[key] = entry;
            loaded++;
        }
        
        std::cerr << "[cache] Loaded " << loaded << " AI summary cache entries\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[cache] Error loading AI summary cache: " << e.what() << "\n";
    }
}

} // namespace cord19
