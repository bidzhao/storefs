**[查看中文版](catalog_cn.md)**

# StoreFS Catalog — Full-Text & Vector Search

The **StoreFS Catalog** enables full-text and vector (semantic) search across all objects stored in the cluster. It is backed by OpenSearch, with optional LLM-powered embedding generation for hybrid search.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                   StoreFS Cluster (Go)                           │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │   Node 1     │    │   Node 2     │    │   Node 3     │       │
│  │  (Admin API) │    │              │    │              │       │
│  └──────┬───────┘    └──────────────┘    └──────────────┘       │
│         │                                                        │
│         │  HTTP /catalog/*                                       │
│         ▼                                                        │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │              Catalog Engine (catalog package)                 ││
│  │  - Full-text search (multi_match)                             ││
│  │  - k-NN vector search (embedding_0..embedding_N)              ││
│  │  - Hybrid search (Reciprocal Rank Fusion)                     ││
│  │  - Stale validation (checks object_md before returning)       ││
│  └──────────────────────────────────────────────────────────────┘│
└──────────────────────────┬───────────────────────────────────────┘
                           │
                           ▼
┌──────────────────────────────────────────────────────────────────┐
│                    OpenSearch Cluster                            │
│                                                                  │
│  Index: catalog-objects (objects)                                │
│  Index: catalog-entities (entities, future use)                  │
│                                                                  │
│  Fields: bucket_id, object_name, size, content_type,             │
│          object_type, write_time, content_text, description,     │
│          tags (nested), user_metadata (nested), meta_values,     │
│          chunk_texts, embedding_0..embedding_N (knn_vector)      │
│                                                                  │
│  - k-NN plugin (HNSW, cosine similarity)                         │
│  - Full-text search (standard analyzer)                          │
│  - Nested fields for tags & user_metadata                        │
└──────────────────────────────────────────────────────────────────┘
                           ▲
                           │
┌──────────────────────────────────────────────────────────────────┐
│                    CatalogBuilder (Go binary)                     │
│                                                                  │
│  1. Scans Doris (object_md) for unprocessed objects              │
│  2. Downloads object content from S3                             │
│  3. Extracts text (PDF, DOCX, HTML, plain text, etc.)            │
│  4. Generates embeddings via LLM API (per chunk)                 │
│  5. Indexes into OpenSearch                                      │
│  6. Runs incremental scans every N seconds                       │
│                                                                  │
│  Horizontal scaling: hash-range partition + claim-based lease    │
└──────────────────────────────────────────────────────────────────┘
```

### Key Components

| Component | Role |
|-----------|------|
| **Catalog Engine** | The built-in search engine embedded in the StoreFS server. Queries OpenSearch and handles full-text, vector, and hybrid search with stale-document validation. |
| **CatalogBuilder** | A standalone program that scans the metadata database, extracts content, generates embeddings, and indexes documents into OpenSearch. Supports horizontal scaling. |
| **OpenSearch** | Search backend. Requires the **k-NN plugin** for vector similarity search. |
| **Embedding API** | OpenAI-compatible API endpoint (e.g., OpenAI, SiliconFlow, etc.) that generates vector embeddings from text. Also used for vision/image description via chat completions. |

---

## Search Capabilities

### 1. Full-Text Search

Searches across multiple fields with weighted scoring:

| Field | Weight |
|-------|--------|
| `object_name` | 3× (highest) |
| `content_text` | 1× |
| `tags.value` | 1× |
| `meta_values` | 1× |
| `content_type` | 1× |

### 2. Vector Search (k-NN)

Uses OpenSearch's k-NN plugin with:
- **Algorithm**: HNSW (Hierarchical Navigable Small World)
- **Distance metric**: Cosine similarity
- **Fields**: `embedding_0` through `embedding_{maxChunks-1}` (up to 8 chunks by default)
- **Filter support**: Can filter by bucket IDs, tags, and object type within the k-NN query

### 3. Hybrid Search (Full-Text + Vector)

When embedding is enabled and a query text is provided, the system performs **hybrid search** using **Reciprocal Rank Fusion (RRF)**:

1. Executes a full-text search (multi_match)
2. Executes a k-NN vector search for each `embedding_N` field
3. Merges results using RRF: `score = Σ 1/(k + rank + 1)` where `k = 60`
4. Validates results against `object_md` (drops stale documents)
5. Returns top-N results sorted by RRF score

If vector search fails (e.g., embedding API unavailable), the system gracefully falls back to full-text search.

### 4. SQL-Like Search

A lightweight SQL parser translates simple queries into OpenSearch DSL:

```sql
SELECT * WHERE tag='key:value' AND size>=1000 AND name LIKE '%keyword%'
SELECT * WHERE object_type='pdf' AND time_from='2024-01-01'
SELECT * WHERE prefix='documents/' AND content_type='application/pdf'
```

Supported filter fields: `tag`, `tags`, `object_type`, `type`, `content_type`, `size`, `objects.size`, `name`, `object_name`, `prefix`, `query`, `search`, `time_from`, `time_to`. Any unrecognized field is treated as a user metadata filter.

---

## OpenSearch Configuration

### Requirements

- **OpenSearch 2.x+** (with k-NN plugin installed)
- **k-NN plugin** enabled for vector similarity search
- Memory sufficient for HNSW graph (proportional to indexed document count)

### Index Mapping

The `catalog-objects` index is created automatically by the CatalogBuilder on first run. It uses:

- **k-NN vector** type for embedding fields (HNSW with cosine similarity)
- **Nested** type for tags and user_metadata
- **Keyword** type for bucket_id, object_name, content_type, object_type
- **Text** type for content_text, description, chunk_texts, meta_values
- **Date** type for write_time (format: `yyyy-MM-dd HH:mm:ss`)

The index mapping must match the embedding **dimension** exactly. If the dimension changes, the index must be recreated (deleted and re-indexed). The CatalogBuilder handles this automatically on startup.

### Connection Test

The Admin API provides `POST /catalog/test-connection` which tests connectivity to OpenSearch using the saved config. It uses `GET /_cluster/health` (not the root `/`) to avoid false failures from OpenSearch Security plugin's auth on the root path.

---

## Embedding (LLM) Requirements

### API Compatibility

The embedding system uses the **OpenAI-compatible API format**:

```
POST {base_url}/embeddings
Authorization: Bearer {api_key}
Content-Type: application/json

{
  "input": ["text to embed"],
  "model": "text-embedding-3-small",
  "dimensions": 1024
}
```

Supported providers:
- **OpenAI** (`text-embedding-3-small`, `text-embedding-3-large`, `text-embedding-ada-002`)
- **SiliconFlow** (various models)
- Any OpenAI-compatible embedding API

### Chat (Vision) API

The CatalogBuilder also uses a chat LLM for:
- **Image description**: Sends image as base64 to `/chat/completions` with vision capability
- **Video description**: Extracts a frame, sends to vision API
- **Audio description**: Combines Whisper transcription with chat summary

The chat model is configured separately via `chat_model` in the CatalogBuilder config.

### Dimension Consistency — CRITICAL

The **embedding dimension must be consistent** across all components:

```
┌──────────────────────────────────────────────────────────────┐
│                     DIMENSION CONSISTENCY                     │
│                                                              │
│  CatalogBuilder config  ──►  OpenSearch index mapping        │
│  (embedding.dimensions)      (knn_vector.dimension)          │
│         │                                                     │
│         └──►  Embedding API must return vectors of            │
│              this exact dimension                             │
│                                                              │
│  If dimension changes:                                        │
│  1. Delete the OpenSearch index                               │
│  2. Update config                                             │
│  3. Re-run CatalogBuilder (full re-index)                     │
│                                                              │
│  The CatalogBuilder handles index recreation automatically    │
│  on startup when the dimension changes.                       │
└──────────────────────────────────────────────────────────────┘
```

**Important**: The `dimensions` parameter is sent to the embedding API. If the API does not support the `dimensions` parameter (e.g., some models return a fixed dimension), the client auto-detects this and retries without it. In that case, the configured dimension must match the model's default output dimension.

### Recommended Models

| Use Case | Model | Typical Dimension |
|----------|-------|-------------------|
| General purpose (EN) | `text-embedding-3-small` | 1536 (default), 512–1536 configurable |
| High accuracy (EN) | `text-embedding-3-large` | 3072 (default), up to 3072 |
| Multilingual | `BAAI/bge-m3` | 1024 |
| Chat / Vision | `deepseek-ai/DeepSeek-OCR`, `gpt-4o`, `claude-3-haiku` | N/A (text generation) |

---

## CatalogBuilder — Configuration & Usage

### Config File

```yaml
doris:
  host: "192.168.2.106"
  port: 9030
  database: "mydb"
  user: "root"
  password: ""

s3:
  endpoint: "http://192.168.2.106:8901"
  access_key: "admin-ak"
  secret_key: "admin-sk"
  region: "us-east-1"

opensearch:
  urls:
    - "https://192.168.2.100:9200"
  username: "admin"
  password: "your-opensearch-password"
  index_objects: "catalog-objects"
  index_entities: "catalog-entities"

embedding:
  provider: "openai"
  base_url: "https://api.siliconflow.cn/v1"
  api_key: "sk-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  model: "BAAI/bge-m3"
  dimensions: 1024
  batch_size: 20
  concurrency: 5
  rate_limit_rpm: 20
  chat_model: "deepseek-ai/DeepSeek-OCR"
  max_chunk_chars: 8000
  max_chunks: 8

builder:
  scan_batch_size: 100
  incremental_interval: 30s
  audio_workers: 2
  embed_workers: 5
  media:
    enabled: false
    whisper_binary: "/usr/local/bin/whisper-cli"
    whisper_model: "/path/to/whisper.cpp/models/ggml-small.bin"
    ffmpeg_binary: "/usr/bin/ffmpeg"
    frame_fps: 1
```

### Configuration Fields

| Section | Field | Default | Description |
|---------|-------|---------|-------------|
| `doris` | `host` | `127.0.0.1` | Doris/MySQL host |
| `doris` | `port` | `9030` | Doris/MySQL port |
| `doris` | `database` | `mydb` | Database name |
| `s3` | `endpoint` | `http://127.0.0.1:8901` | S3 endpoint for object download |
| `s3` | `access_key` | — | S3 access key (must have read access) |
| `s3` | `secret_key` | — | S3 secret key |
| `opensearch` | `urls` | `["http://127.0.0.1:9200"]` | OpenSearch URLs |
| `opensearch` | `index_objects` | `catalog-objects` | Object index name |
| `embedding` | `base_url` | `https://api.openai.com/v1` | Embedding API base URL |
| `embedding` | `model` | `text-embedding-3-small` | Embedding model name |
| `embedding` | `dimensions` | `1024` | Embedding vector dimension |
| `embedding` | `batch_size` | `20` | Batch size for embedding requests |
| `embedding` | `concurrency` | `10` | Max concurrent embedding workers |
| `embedding` | `rate_limit_rpm` | `3000` | API rate limit (requests per minute) |
| `embedding` | `chat_model` | — | Chat model for vision/description (falls back to embedding model) |
| `embedding` | `max_chunk_chars` | `8000` | Max characters per embedding chunk |
| `embedding` | `max_chunks` | `8` | Max number of chunks per document |
| `builder` | `scan_batch_size` | `100` | Objects per batch when scanning |
| `builder` | `incremental_interval` | `30s` | Interval between incremental scans |
| `builder` | `media.enabled` | `false` | Enable audio/video/image processing |
| `builder` | `media.whisper_binary` | — | Path to whisper-cli for audio transcription |
| `builder` | `media.ffmpeg_binary` | — | Path to ffmpeg for video processing |

### Usage

```bash
# Full scan (initial)
./catalogbuilder -config catalogbuilder.yaml

# Incremental only (skip full scan)
./catalogbuilder -config catalogbuilder.yaml -skip-full-scan

# Show scan status
./catalogbuilder -config catalogbuilder.yaml status

# Verbose mode (per-bucket progress)
./catalogbuilder -config catalogbuilder.yaml -verbose
```

### Scan Process

1. **Full scan**: Scans all `object_md` records where `catalog_processed = 0`. Processes each object: download → extract → embed → index.
2. **Incremental scan**: Runs every `incremental_interval` (default 30s). Scans objects with `write_time > watermark` where `catalog_processed = 0`.
3. **Horizontal scaling**: Multiple CatalogBuilder instances can run simultaneously. Each claims hash-range partitions via the `catalog_scan_claims` table. Dead instances are detected via heartbeat (`catalog_instances`) and their claims are re-assigned.

### Content Extraction

The CatalogBuilder extracts text content from various file formats:

| Format | Extractor | Notes |
|--------|-----------|-------|
| Plain text, JSON, XML, CSV, source code | `PlainTextExtractor` | Direct read, up to 1 MB |
| HTML, XHTML | `HTMLExtractor` | Strips tags, extracts text |
| PDF | `PDFExtractor` | Reads all pages |
| DOCX | `DOCXExtractor` | Reads `word/document.xml` |
| XLSX | `XLSXExtractor` | Reads all sheets |
| EPUB | `EPUBExtractor` | Reads all XHTML/HTML content |
| MOBI | `MOBIParser` | PalmDOC decompression, HTML stripping |
| Audio | `Whisper` (external) | Requires `whisper-cli` |
| Video | `FFmpeg` + `Whisper` + `Vision LLM` | Audio → Whisper, Frame → Vision API |
| Image | `Vision LLM` | Base64 → chat completions with vision |

### Media Processing Setup (Audio / Video / Image)

To enable media content processing (audio transcription, video description, image description), the CatalogBuilder requires two external tools. Enable processing by setting `builder.media.enabled: true` in the config, then configure the binary paths.

#### 1. Install Whisper (whisper-cli)

Audio transcription and video audio-track transcription require [whisper.cpp](https://github.com/ggerganov/whisper.cpp):

```bash
# Clone and build whisper.cpp
git clone https://github.com/ggerganov/whisper.cpp.git
cd whisper.cpp
make

# Download a model (e.g., the "small" multilingual model)
./models/download-ggml-model.sh small
```

After building:
- The binary is located at `build/bin/whisper-cli`
- The model file is located at `models/ggml-small.bin`

Configure in `catalogbuilder.yaml`:

```yaml
builder:
  media:
    enabled: true
    whisper_binary: "/path/to/whisper.cpp/build/bin/whisper-cli"
    whisper_model: "/path/to/whisper.cpp/models/ggml-small.bin"   # absolute path to the model file
```

> **Note**: `whisper_model` must be the **absolute path to the model file**, not just a model name. Model file size depends on the model chosen (small ≈ 466 MB, base ≈ 142 MB, tiny ≈ 75 MB).

#### 2. Install FFmpeg

Video processing (audio extraction and frame extraction) requires [FFmpeg](https://ffmpeg.org/), plus `ffprobe` (shipped with FFmpeg) to detect audio streams:

```bash
# Ubuntu / Debian
sudo apt install ffmpeg

# CentOS / RHEL
sudo yum install ffmpeg

# macOS (Homebrew)
brew install ffmpeg
```

Verify the installation:

```bash
ffmpeg -version
ffprobe -version
```

Configure in `catalogbuilder.yaml`:

```yaml
builder:
  media:
    enabled: true
    ffmpeg_binary: "/usr/bin/ffmpeg"
    frame_fps: 1   # frames extracted per second for video description
```

> **Note**: The `ffprobe` binary is auto-detected from the `ffmpeg_binary` path (replace `ffmpeg` with `ffprobe` in the path). It is used to check whether a video has an audio stream before running Whisper.

#### 3. Automatic Detection of Audio Streams

Video processing flow:
1. `ffprobe` checks whether the video has an audio track
2. If yes: FFmpeg extracts the audio stream → Whisper transcribes it
3. Regardless of audio: FFmpeg extracts a frame (`frame_fps` frames per second, scaled to 640px wide) → the vision LLM generates a description

#### 4. LLM Requirement for Images / Video Frames

Image and video-frame description uses the chat vision endpoint (`/chat/completions`). The configured `embedding.chat_model` must support **vision (image input)**. If the chat model does not support vision, descriptions will fail for images — in that case, keep `media.enabled: false` or use a vision-capable model.

---

## Admin API — Catalog Endpoints

All catalog endpoints are under `/catalog/` on the Admin API port (default 7946). Super admin role is required for configuration changes; all authenticated users can search.

| Method | Endpoint | Description | Auth |
|--------|----------|-------------|------|
| `GET` | `/catalog/config` | Get catalog configuration (OpenSearch + Embedding) | super_admin |
| `PUT` | `/catalog/config` | Update catalog configuration | super_admin |
| `POST` | `/catalog/enable` | Enable the catalog search engine | super_admin |
| `POST` | `/catalog/disable` | Disable the catalog search engine | super_admin |
| `GET` | `/catalog/stats` | Get catalog status (enabled/disabled, endpoint info) | authenticated |
| `GET` | `/catalog/objects/search` | Search objects (full-text with filters) | authenticated |
| `GET` | `/catalog/objects/detail` | Get object metadata detail | authenticated |
| `GET` | `/catalog/search` | SQL-like search | authenticated |
| `POST` | `/catalog/search/vector` | Vector similarity search | authenticated |
| `POST` | `/catalog/test-connection` | Test OpenSearch connectivity | super_admin |
| `POST` | `/catalog/test-embedding` | Test Embedding API connectivity | super_admin |

### Search Parameters

`GET /catalog/objects/search`

| Parameter | Type | Description |
|-----------|------|-------------|
| `query` | string | Full-text search query |
| `object_type` | string | Filter: `text`, `pdf`, `office`, `binary`, `image`, `audio`, `video` |
| `content_type` | string | Filter: MIME type (e.g., `application/pdf`) |
| `tags` | string | Filter: comma-separated `key:value` pairs (e.g., `project:alpha,dept:eng`) |
| `meta` | string | Filter: user metadata `key:value` pairs |
| `prefix` | string | Filter: object name prefix |
| `bucket_ids` | string | Filter: comma-separated bucket IDs |
| `size_min` | int | Minimum object size (bytes) |
| `size_max` | int | Maximum object size (bytes) |
| `time_from` | string | Time range start (RFC3339) |
| `time_to` | string | Time range end (RFC3339) |
| `limit` | int | Max results (default 20, max 100; Web UI always uses 100) |
| `from` | int | Result offset for pagination (default 0; Web UI does pagination locally, omits this parameter) |

### Access Control

Search results are automatically filtered by the user's permissions:
- **super_admin**: Can search all buckets
- **group_admin**: Can search buckets owned by their group + ACL-granted buckets
- **normal user**: Can search their own buckets + ACL-granted buckets

---

## Web Admin — Catalog Search Page

The **Catalog Search** page is available in the Web Admin console under the Catalog section.

### Features

1. **Search bar**: Full-text search with keyword input
2. **Advanced filters**: Expandable panel with:
   - Object type (All / Text / Office / PDF / Binary)
   - Tags (key:value format)
   - Prefix (path prefix filter)
   - Size range (min/max)
3. **Results table**: Shows bucket name, object name, type, size, content type, write time, and relevance score
4. **Object detail**: Click object name to view metadata
5. **Result actions**: Copy presigned URL, download, delete (permission-dependent)

### Catalog Flow

```
┌──────────┐     ┌─────────────┐     ┌─────────────┐     ┌───────────┐
│  Admin   │────►│  Catalog    │────►│  OpenSearch  │◄────│ Catalog   │
│  Web UI  │     │  Engine     │     │  Cluster     │     │ Builder   │
│          │     │ (Go/Search) │     │              │     │ (Go/Bin)  │
└──────────┘     └─────────────┘     └─────────────┘     └───────────┘
                                                                    │
                                                                    ▼
                                                             ┌───────────┐
                                                             │  StoreFS  │
                                                             │  S3 + DB  │
                                                             └───────────┘
```

1. **CatalogBuilder** scans the database for new objects, downloads them from S3, extracts content, generates embeddings, and indexes them into OpenSearch.
2. **Admin Web UI** sends search requests to the StoreFS Admin API.
3. **Catalog Engine** queries OpenSearch, applies permission filters, validates results, and returns them to the UI.
4. When a user uploads/deletes an object, the OpenSearch index is updated in real-time via the S3 API handlers.

---

## MCP Tools

The StoreFS MCP Server provides the following catalog tools (see [MCP Documentation](mcp.md) for details):

| Tool | Description |
|------|-------------|
| `storefs_catalog_stats` | Get catalog search status (enabled/disabled, OpenSearch info) |
| `storefs_catalog_enable` | Enable catalog search engine |
| `storefs_catalog_disable` | Disable catalog search engine |
| `storefs_catalog_config` | Update catalog configuration (OpenSearch + Embedding) |
| `storefs_catalog_get_config` | Get current catalog configuration |
| `storefs_catalog_search_objects` | Search objects with full-text and filters |
| `storefs_catalog_search` | SQL-like search (`SELECT * WHERE ...`) |
| `storefs_catalog_vector_search` | Vector similarity search (by vector or text) |
| `storefs_catalog_get_object_metadata` | Get object metadata (user_meta, http_meta) |
| `storefs_catalog_test_connection` | Test OpenSearch connection |
| `storefs_catalog_test_embedding` | Test Embedding API connection |

---

## Troubleshooting

### Search Returns No Results

1. Check catalog is enabled: `GET /catalog/stats`
2. Check CatalogBuilder is running and has processed objects
3. Check OpenSearch is reachable: `POST /catalog/test-connection`
4. Check `catalog_processed` flag in `object_md` — objects must be processed first

### Embedding Fails

1. Test embedding API: `POST /catalog/test-embedding`
2. Check the API key is valid and has access to the specified model
3. Verify the model supports the configured dimension (or auto-detection works)
4. Check rate limits — `rate_limit_rpm` may be too low

### Hybrid Search Not Working

1. Ensure embedding is enabled in both the StoreFS config (web admin) and CatalogBuilder config
2. Check that the embedding dimension matches between config and OpenSearch index mapping
3. If the index was created with a different dimension, delete and recreate it

### Stale Results

The Catalog Engine validates search results against `object_md` before returning. If an object was deleted from StoreFS but the OpenSearch index wasn't updated, the stale document is automatically removed from search results and deleted from the index on the next query.