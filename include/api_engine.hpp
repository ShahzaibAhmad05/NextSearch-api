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

// Cache entry structure for LRU cache with expiry
struct CacheEntry {
    json result;
    std::list<std::string>::iterator lru_iter;
    std::chrono::steady_clock::time_point timestamp;
};

struct Engine {
    fs::path index_dir;
    std::vector<std::string> seg_names;
    std::vector<Segment> segments;

    std::unordered_map<std::string, MetaInfo> uid_to_meta;

    // Autocomplete index built from the loaded lexicon.
    AutocompleteIndex ac;

    // Optional semantic expansion index (classic word embeddings).
    // If no embeddings are loaded, search falls back to keyword BM25.
    SemanticIndex sem;

    // Search result cache: stores up to 2600 queries with LRU eviction and 24hr expiry
    // Key format: "query|k" (e.g., "covid|10")
    std::unordered_map<std::string, CacheEntry> cache;
    std::list<std::string> lru_list; // Most recently used at front
    static constexpr size_t MAX_CACHE_SIZE = 2600;
    static constexpr std::chrono::hours CACHE_EXPIRY_DURATION{24};

    // AI overview cache: stores up to 500 AI overviews with LRU eviction and 24hr expiry
    // Key format: "query|k" (e.g., "covid|10") - same as search cache
    std::unordered_map<std::string, CacheEntry> ai_cache;
    std::list<std::string> ai_lru_list; // Most recently used at front
    static constexpr size_t MAX_AI_CACHE_SIZE = 500;
    static constexpr std::chrono::hours AI_CACHE_EXPIRY_DURATION{24};

    std::mutex mtx;

    bool reload();
    json search(const std::string& query, int k);
    json suggest(const std::string& user_input, int limit);
    
    // Public cache key generator for use by AI overview and other components
    std::string make_cache_key(const std::string& query, int k);
    
    // AI overview cache helpers (public for use by ai_overview module)
    json get_ai_from_cache(const std::string& cache_key);
    void put_ai_in_cache(const std::string& cache_key, const json& result);
    
private:
    json get_from_cache(const std::string& cache_key);
    bool is_cache_entry_expired(const CacheEntry& entry);
    void put_in_cache(const std::string& cache_key, const json& result);
};

} // namespace cord19