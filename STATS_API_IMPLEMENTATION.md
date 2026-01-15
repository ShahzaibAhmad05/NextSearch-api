# Stats API Implementation

## Overview
The stats API endpoint has been implemented to return meaningful statistics about the search engine's operations. The endpoint is protected by admin authentication and provides comprehensive metrics.

## API Endpoint

### GET /api/stats

**Authentication**: Required (Admin JWT token)

**Response Format**:
```json
{
  "total_searches": 150,
  "search_cache_hits": 75,
  "search_cache_hit_rate": 0.5,
  "ai_overview_calls": 20,
  "ai_overview_cache_hits": 10,
  "ai_overview_cache_hit_rate": 0.5,
  "ai_summary_calls": 30,
  "ai_summary_cache_hits": 15,
  "ai_summary_cache_hit_rate": 0.5,
  "ai_api_calls_remaining": 9950,
  "total_feedback_count": 25,
  "last_10_feedback": [
    {
      "type": "search_quality",
      "query": "covid symptoms",
      "rating": 5,
      "comment": "Very helpful results",
      "timestamp": "2026-01-15T21:30:45.123Z"
    },
    // ... up to 10 most recent feedback entries
  ]
}
```

## Fields Description

### Search Metrics
- **total_searches**: Total number of search API calls made
- **search_cache_hits**: Number of searches served from cache
- **search_cache_hit_rate**: Percentage of cache hits (0.0 to 1.0)

### AI Overview Metrics
- **ai_overview_calls**: Total number of AI overview API calls
- **ai_overview_cache_hits**: Number of AI overviews served from cache
- **ai_overview_cache_hit_rate**: Percentage of cache hits for AI overviews

### AI Summary Metrics
- **ai_summary_calls**: Total number of AI summary API calls
- **ai_summary_cache_hits**: Number of AI summaries served from cache
- **ai_summary_cache_hit_rate**: Percentage of cache hits for AI summaries

### AI API Quota
- **ai_api_calls_remaining**: Number of AI API calls remaining before limit is reached
  - Decrements only on actual API calls to Azure OpenAI
  - Does NOT decrement on cache hits
  - Configurable via .env file (`AI_API_CALLS_LIMIT`)

### Feedback Metrics
- **total_feedback_count**: Total number of feedback entries stored
- **last_10_feedback**: Array of the 10 most recent feedback entries

## Configuration

Add to `.env` file:
```env
# AI API Calls Limit (default: 10000)
# This sets the maximum number of AI API calls allowed
# The counter decrements on each actual API call (not cache hits)
AI_API_CALLS_LIMIT=10000
```

## Implementation Details

### Components Added

1. **api_stats.hpp** - StatsTracker class with atomic counters for thread-safe stats tracking

2. **Modified Files**:
   - `api_server.cpp` - Integrated StatsTracker and updated stats endpoint
   - `api_ai_overview.cpp` - Added stats tracking for AI overview calls and cache hits
   - `api_ai_summary.cpp` - Added stats tracking for AI summary calls and cache hits
   - `api_ai_overview.hpp` - Updated signature to accept StatsTracker pointer
   - `api_ai_summary.hpp` - Updated signature to accept StatsTracker pointer

### Thread Safety
- All counters use `std::atomic<int64_t>` for thread-safe increments/decrements
- No locks required for stats updates
- FeedbackManager uses mutex for thread-safe feedback retrieval

### Stats Tracking Flow

1. **Search Endpoint** (`/api/search`):
   - Increments `total_searches` on every call
   - Increments `search_cache_hits` when result comes from cache

2. **AI Overview Endpoint** (`/api/ai_overview`):
   - Increments `ai_overview_calls` on every call
   - Increments `ai_overview_cache_hits` when result from cache
   - Decrements `ai_api_calls_remaining` only on actual API call to Azure

3. **AI Summary Endpoint** (`/api/ai_summary`):
   - Increments `ai_summary_calls` on every call
   - Increments `ai_summary_cache_hits` when result from cache
   - Decrements `ai_api_calls_remaining` only on actual API call to Azure

4. **Feedback**:
   - Retrieved from FeedbackManager (last 10 entries)

## Testing the Endpoint

### 1. Login to get admin token:
```bash
curl -X POST http://localhost:8080/api/admin/login \
  -H "Content-Type: application/json" \
  -d '{"password":"nextsearch_admin_2026"}'
```

Response:
```json
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "expires_in": 3600
}
```

### 2. Get stats (with token):
```bash
curl http://localhost:8080/api/stats \
  -H "Authorization: Bearer YOUR_TOKEN_HERE"
```

### 3. Test the full workflow:

```bash
# Make some searches
curl "http://localhost:8080/api/search?q=covid&k=10"
curl "http://localhost:8080/api/search?q=covid&k=10"  # Should hit cache
curl "http://localhost:8080/api/search?q=vaccine&k=10"

# Make AI overview calls (requires admin token)
curl "http://localhost:8080/api/ai_overview?q=covid&k=10" \
  -H "Authorization: Bearer YOUR_TOKEN"

# Make AI summary calls (requires admin token)
curl "http://localhost:8080/api/ai_summary?cord_uid=ug7v899j" \
  -H "Authorization: Bearer YOUR_TOKEN"

# Submit feedback
curl -X POST http://localhost:8080/api/feedback \
  -H "Content-Type: application/json" \
  -d '{
    "type": "search_quality",
    "query": "covid",
    "rating": 5,
    "comment": "Great results!"
  }'

# Get stats to see all metrics
curl http://localhost:8080/api/stats \
  -H "Authorization: Bearer YOUR_TOKEN"
```

## Example Output

After running the workflow above:
```json
{
  "total_searches": 3,
  "search_cache_hits": 1,
  "search_cache_hit_rate": 0.3333333333333333,
  "ai_overview_calls": 1,
  "ai_overview_cache_hits": 0,
  "ai_overview_cache_hit_rate": 0.0,
  "ai_summary_calls": 1,
  "ai_summary_cache_hits": 0,
  "ai_summary_cache_hit_rate": 0.0,
  "ai_api_calls_remaining": 9998,
  "total_feedback_count": 1,
  "last_10_feedback": [
    {
      "type": "search_quality",
      "query": "covid",
      "rating": 5,
      "comment": "Great results!",
      "timestamp": "2026-01-15T21:45:30.456Z"
    }
  ]
}
```

## Notes

- Stats are stored in memory and reset when the server restarts
- For production, consider persisting stats to a database
- The `ai_api_calls_remaining` counter helps track and limit Azure OpenAI API usage costs
- Cache hit rates help measure the effectiveness of the caching strategy
