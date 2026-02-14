#pragma once

#include <chrono>
#include <filesystem>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "api_autocomplete.hpp"
#include "api_types.hpp"
#include "semantic_embedding.hpp"

namespace cord19 {

// Cache entry structure for LRU cache
struct CacheEntry {
    json result;
    std::list<std::string>::iterator lru_iter;
};

struct Engine {
    fs::path index_dir;
    std::vector<std::string> seg_names;
    std::vector<Segment> segments;

    std::unordered_map<std::string, MetaInfo> uid_to_meta;
    fs::path metadata_csv_path;  // Path to metadata.csv for on-demand reads

    // Autocomplete index built from the loaded lexicon.
    AutocompleteIndex ac;

    // Optional semantic expansion index (classic word embeddings).
    // If no embeddings are loaded, search falls back to keyword BM25.
    SemanticIndex sem;

    // Search result cache: stores up to 2600 queries with LRU eviction
    // Key format: "query|k" (e.g., "covid|10")
    std::unordered_map<std::string, CacheEntry> cache;
    std::list<std::string> lru_list; // Most recently used at front
    static constexpr size_t MAX_CACHE_SIZE = 2600;

    // AI overview cache: stores up to 500 AI overviews with LRU eviction
    // Key format: "query|k" (e.g., "covid|10") - same as search cache
    std::unordered_map<std::string, CacheEntry> ai_overview_cache;
    std::list<std::string> ai_overview_lru_list; // Most recently used at front
    static constexpr size_t MAX_AI_OVERVIEW_CACHE_SIZE = 500;

    // AI summary cache: stores up to 1000 AI summaries with LRU eviction
    // Key format: "summary|cord_uid" (e.g., "summary|abc123")
    std::unordered_map<std::string, CacheEntry> ai_summary_cache;
    std::list<std::string> ai_summary_lru_list; // Most recently used at front
    static constexpr size_t MAX_AI_SUMMARY_CACHE_SIZE = 1000;

    // Cache persistence counters (save to disk every N updates)
    size_t cache_updates_since_save = 0;
    size_t ai_overview_cache_updates_since_save = 0;
    size_t ai_summary_cache_updates_since_save = 0;
    static constexpr size_t CACHE_SAVE_INTERVAL = 1; // Save every update for immediate persistence

    std::mutex mtx;

    ~Engine(); // Destructor to save caches on shutdown
    bool reload();
    json search(const std::string& query, int k);
    json suggest(const std::string& user_input, int limit);
    
    // Public cache key generator for use by AI overview and other components
    std::string make_cache_key(const std::string& query, int k);
    
    // AI overview cache helpers (public for use by ai_overview module)
    json get_ai_overview_from_cache(const std::string& cache_key);
    void put_ai_overview_in_cache(const std::string& cache_key, const json& result);
    
    // AI summary cache helpers (public for use by ai_summary module)
    json get_ai_summary_from_cache(const std::string& cache_key);
    void put_ai_summary_in_cache(const std::string& cache_key, const json& result);
    
    // Cache persistence (save/load to JSON files)
    void save_cache();
    void load_cache();
    void save_ai_overview_cache();
    void load_ai_overview_cache();
    void save_ai_summary_cache();
    void load_ai_summary_cache();
    
private:
    json get_from_cache(const std::string& cache_key);
    void put_in_cache(const std::string& cache_key, const json& result);
};

} // namespace cord19