# NextSearch API

A high-performance C++ search engine API designed for large-scale document collections (e.g., CORD-19 research papers). Features BM25 ranking, autocomplete, AI-powered overviews, and lazy-loaded metadata for efficient memory usage.

---

## Overview

**NextSearch** is a scalable search engine built in C++ using:
- **Inverted index** with BM25 scoring for relevance ranking
- **Forward index** for fast document retrieval
- **Lexicon-based autocomplete** with document frequency ranking
- **Lazy metadata loading** (loads only ~16 bytes per doc at startup)
- **LRU caching** for search results and AI responses (no expiry - infinite cache)
- **Azure OpenAI integration** for AI overviews and document summaries

---

## Data Storage

### Index Files (in `INDEX_DIR/`)
- `manifest.bin` - List of segment names
- `segments/seg_XXXXXX/` - Individual index segments containing:
  - `lexicon.bin` - Term dictionary with posting list offsets
  - `postings.bin` - Document IDs and term frequencies
  - `docids.bin` - Segment-local document IDs
  - `forward.bin` - Document offsets into metadata CSV

### Metadata
- `metadata.csv` - Document metadata (title, author, abstract, URL, etc.)
  - Lazy-loaded on-demand via offset lookup
  - Format: `cord_uid,title,abstract,publish_time,authors,url,journal,source`

### Cache Files (in root directory)
- `search_cache.json` - Cached search results (max 2600 entries, LRU eviction)
- `ai_overview_cache.json` - Cached AI overviews (max 500 entries, LRU)
- `ai_summary_cache.json` - Cached AI summaries (max 1000 entries, LRU)
- `feedback.json` - User feedback (max 500 entries)
- `stats.json` - API usage statistics

### Configuration
- `.env` - Environment variables (Azure OpenAI credentials, admin auth)

---

## API Endpoints

Base URL: `http://localhost:8080`

### 1. Health Check
**GET** `/api/health`

**Response:**
```json
{
  "ok": true,
  "segments": 1
}
```

---

### 2. Search
**GET** `/api/search`

**Parameters:**
| Param | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `q` | string | ✅ Yes | - | Search query |
| `k` | int | ❌ No | 10 | Number of results (1-100) |

**Response:**
```json
{
  "query": "covid",
  "k": 10,
  "found": 12521,
  "results": [
    {
      "cord_uid": "abc123",
      "docId": 149674,
      "segment": "seg_000001",
      "score": 7.47,
      "title": "Serological Cytokine...",
      "author": "Cerbulo-Vazquez et al.",
      "publish_time": "2020-07-17",
      "url": "http://..."
    }
  ],
  "search_time_ms": 45.2,
  "total_time_ms": 47.8,
  "cached": false
}
```

---

### 3. Autocomplete Suggestions
**GET** `/api/suggest`

**Parameters:**
| Param | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `q` | string | ✅ Yes | - | Partial query to autocomplete |
| `k` | int | ❌ No | 5 | Number of suggestions |

**Response:**
```json
{
  "query": "cov",
  "suggestions": [
    {"term": "covid", "score": 12521},
    {"term": "coronavirus", "score": 8234}
  ]
}
```

---

### 4. AI Overview
**GET** `/api/ai_overview`

Generates an AI-powered overview by analyzing search results using Azure OpenAI.

**Parameters:**
| Param | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `q` | string | ✅ Yes | - | Search query |
| `k` | int | ❌ No | 10 | Number of results to analyze |

**Headers:**
| Header | Required | Description |
|--------|----------|-------------|
| `Authorization: Bearer <token>` | ❌ No | Admin JWT (skips API call limit) |

**Response:**
```json
{
  "query": "covid treatment",
  "overview": "# COVID-19 Treatment Overview\n\n...",
  "model": "gpt-4",
  "usage": {
    "prompt_tokens": 1234,
    "completion_tokens": 567
  }
}
```

---

### 5. AI Summary
**GET** `/api/ai_summary`

Generates an AI-powered summary of a specific document's abstract.

**Parameters:**
| Param | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `cord_uid` | string | ✅ Yes | - | Document unique identifier |

**Headers:**
| Header | Required | Description |
|--------|----------|-------------|
| `Authorization: Bearer <token>` | ❌ No | Admin JWT (skips API call limit) |

**Response:**
```json
{
  "cord_uid": "abc123",
  "summary": "This study investigates...",
  "cached": false
}
```

---

### 6. Add Document
**POST** `/api/add_document`

Upload a CORD-19 zip file to create a new index segment.

**Headers:**
| Header | Required | Description |
|--------|----------|-------------|
| `Authorization: Bearer <token>` | ✅ Yes | Admin JWT token |
| `Content-Type` | ✅ Yes | `multipart/form-data` |

**Body:**
- File field: `cord_slice` (ZIP file containing `metadata.csv` and `document_parses/`)

**Response:**
```json
{
  "success": true,
  "segment_name": "seg_000002",
  "documents_added": 5000
}
```

---

### 7. Reload Index
**POST** `/api/reload`

Reloads all index segments from disk.

**Response:**
```json
{
  "reloaded": true,
  "segments": 2
}
```

---

### 8. Submit Feedback
**POST** `/api/feedback`

Submit user feedback (stored in `feedback.json`).

**Body:**
```json
{
  "type": "positive",
  "message": "Great results!",
  "query": "covid treatment",
  "other_field": "any custom data"
}
```

**Response:**
```json
{
  "success": true,
  "message": "Feedback recorded"
}
```

---

### 9. Get Stats (Admin Only)
**GET** `/api/stats`

Retrieve API usage statistics.

**Headers:**
| Header | Required | Description |
|--------|----------|-------------|
| `Authorization: Bearer <token>` | ✅ Yes | Admin JWT token |

**Response:**
```json
{
  "total_searches": 150,
  "search_cache_hits": 45,
  "ai_overview_calls": 20,
  "ai_overview_cache_hits": 5,
  "ai_summary_calls": 10,
  "ai_summary_cache_hits": 3,
  "ai_api_calls_remaining": 9950,
  "ai_api_calls_used": 50,
  "feedback_count": 12,
  "last_updated": "2026-02-14T10:30:00Z"
}
```

---

### 10. Admin Login
**POST** `/api/admin/login`

Authenticate and receive a JWT token.

**Body:**
```json
{
  "password": "your_admin_password"
}
```

**Response:**
```json
{
  "token": "eyJhbGc...",
  "expires_in": 3600
}
```

---

### 11. Admin Logout
**POST** `/api/admin/logout`

Logout (client-side token clearing).

**Response:**
```json
{
  "message": "Logged out successfully"
}
```

---

### 12. Verify Admin Token
**GET** `/api/admin/verify`

Check if JWT token is valid.

**Headers:**
| Header | Required | Description |
|--------|----------|-------------|
| `Authorization: Bearer <token>` | ✅ Yes | Admin JWT token |

**Response:**
```json
{
  "valid": true
}
```

---

## Local Setup

### Prerequisites
- C++17 compiler (g++, clang, or MSVC)
- CMake 3.15+
- OpenSSL (for JWT authentication)

### Installation

```bash
# Clone repository
git clone https://github.com/ShahzaibAhmad05/NextSearch.git
cd NextSearch

# Build
cmake -S . -B build
cmake --build build

# Create .env file
cat > .env << EOF
# Azure OpenAI Configuration (optional - for AI features)
AZURE_OPENAI_ENDPOINT=https://your-resource.openai.azure.com/
AZURE_OPENAI_API_KEY=your_api_key_here
AZURE_OPENAI_MODEL=gpt-4

# Admin Authentication (optional - for protected endpoints)
ADMIN_PASSWORD=your_secure_password
JWT_SECRET=your_secret_key_min_32_chars
JWT_EXPIRATION=3600

# AI API Limits (optional)
AI_API_CALLS_LIMIT=10000
EOF

# Run server
./build/api_server ./index 8080
```

### Dataset Setup

**Option 1: Download pre-built index**
```bash
# Place your index files in ./index/
# Structure: ./index/segments/seg_XXXXXX/
```

**Option 2: Build index from CORD-19 dataset**
```bash
# Download CORD-19 dataset
# Process with AddDocument tool
./build/AddDocument <cord19_directory>
```

### Test API

```bash
# Health check
curl http://localhost:8080/api/health

# Search
curl "http://localhost:8080/api/search?q=covid&k=10"

# Autocomplete
curl "http://localhost:8080/api/suggest?q=cov&k=5"
```

---

## Docker

```bash
# Build
docker build -t nextsearch .

# Run
docker run -p 8080:8080 -v $(pwd)/index:/app/index nextsearch
```
