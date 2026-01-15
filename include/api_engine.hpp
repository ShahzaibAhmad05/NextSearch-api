#pragma once

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

    std::mutex mtx;

    bool reload();
    json search(const std::string& query, int k);
    json suggest(const std::string& user_input, int limit);
    
private:
    std::string make_cache_key(const std::string& query, int k);
    json get_from_cache(const std::string& cache_key);
    void put_in_cache(const std::string& cache_key, const json& result);
};

} // namespace cord19