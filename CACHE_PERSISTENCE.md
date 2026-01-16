# Cache Persistence

## Overview

The search engine now persists all three caches across server restarts using JSON files:

1. **Search Cache** - Stores search results (up to 2600 queries)
2. **AI Overview Cache** - Stores AI-generated overviews (up to 500 queries)
3. **Autocomplete Cache** - Built from index lexicon (persists via index reload)

## Features

### Automatic Persistence

- **On Startup**: Caches are automatically loaded from disk when the engine reloads
- **During Operation**: Caches are automatically saved every 10 updates (configurable via `CACHE_SAVE_INTERVAL`)
- **Expiry Handling**: Expired entries (older than 24 hours) are automatically filtered out during load

### Cache Files

The caches are stored as JSON files in the index directory:

- `search_cache.json` - Search result cache
- `ai_cache.json` - AI overview cache

### Cache Entry Format

Each cache entry stores:
- **key**: Query string + k parameter (e.g., "covid|10")
- **result**: Full JSON response object
- **timestamp**: Milliseconds since epoch for expiry tracking

### LRU Eviction

Both caches use Least Recently Used (LRU) eviction:
- Expired entries are prioritized for eviction
- If no expired entries exist, the least recently used entry is evicted
- LRU order is preserved across server restarts

## Configuration

Modify these constants in `api_engine.hpp` to adjust cache behavior:

```cpp
// Search cache settings
static constexpr size_t MAX_CACHE_SIZE = 2600;
static constexpr std::chrono::hours CACHE_EXPIRY_DURATION{24};

// AI cache settings  
static constexpr size_t MAX_AI_CACHE_SIZE = 500;
static constexpr std::chrono::hours AI_CACHE_EXPIRY_DURATION{24};

// Auto-save frequency (save every N updates)
static constexpr size_t CACHE_SAVE_INTERVAL = 10;
```

## Benefits

1. **Cost Savings**: Reduces redundant API calls by preserving cache across restarts
2. **Performance**: Faster responses for cached queries immediately after restart
3. **Reliability**: Caches survive server crashes and restarts
4. **Efficiency**: Periodic saves minimize I/O overhead during operation

## Implementation Details

### Save Operations

- Triggered automatically after every `CACHE_SAVE_INTERVAL` updates
- Only saves non-expired entries
- Uses pretty-printed JSON (indent=2) for human readability
- Atomic write to prevent corruption

### Load Operations

- Called automatically during `Engine::reload()`
- Validates JSON structure before loading
- Skips expired entries during load
- Preserves LRU order from saved data
- Gracefully handles missing or corrupt cache files

## Thread Safety

All cache operations are protected by the engine's mutex lock, ensuring thread-safe concurrent access during saves, loads, and queries.
