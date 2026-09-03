**[English](catalog.md)**

# StoreFS 目录搜索 — 全文检索与向量搜索

**StoreFS 目录搜索（Catalog）** 支持对集群中所有存储对象进行全文检索和向量语义搜索。它基于 OpenSearch 构建，并可通过 LLM 生成向量嵌入，实现混合搜索。

---

## 架构

```
┌──────────────────────────────────────────────────────────────────┐
│                    StoreFS 集群 (Go)                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │   节点 1     │    │   节点 2     │    │   节点 3     │       │
│  │  (Admin API) │    │              │    │              │       │
│  └──────┬───────┘    └──────────────┘    └──────────────┘       │
│         │                                                        │
│         │  HTTP /catalog/*                                       │
│         ▼                                                        │
│  ┌──────────────────────────────────────────────────────────────┐│
│  │            目录搜索引擎 (catalog 包)                          ││
│  │  - 全文检索 (multi_match)                                     ││
│  │  - k-NN 向量搜索 (embedding_0..embedding_N)                   ││
│  │  - 混合搜索 (RRF 融合排序)                                    ││
│  │  - 过期结果验证 (查询 object_md 表确认对象存在)               ││
│  └──────────────────────────────────────────────────────────────┘│
└──────────────────────────┬───────────────────────────────────────┘
                           │
                           ▼
┌──────────────────────────────────────────────────────────────────┐
│                     OpenSearch 集群                              │
│                                                                  │
│  索引: catalog-objects (对象数据)                                │
│  索引: catalog-entities (实体数据, 预留)                         │
│                                                                  │
│  字段: bucket_id, object_name, size, content_type,               │
│        object_type, write_time, content_text, description,       │
│        tags (嵌套), user_metadata (嵌套), meta_values,           │
│        chunk_texts, embedding_0..embedding_N (knn_vector)        │
│                                                                  │
│  - k-NN 插件 (HNSW 算法, 余弦相似度)                             │
│  - 全文检索 (standard 分析器)                                    │
│  - 嵌套字段 (tags, user_metadata)                                │
└──────────────────────────────────────────────────────────────────┘
                           ▲
                           │
┌──────────────────────────────────────────────────────────────────┐
│                  CatalogBuilder (Go 独立进程)                     │
│                                                                  │
│  1. 扫描 Doris (object_md 表) 获取未处理的对象                   │
│  2. 从 S3 下载对象内容                                           │
│  3. 提取文本内容 (PDF, DOCX, HTML, 纯文本等)                     │
│  4. 通过 LLM API 生成向量嵌入 (按 chunk 分块)                    │
│  5. 写入 OpenSearch 索引                                         │
│  6. 定期增量扫描 (每 N 秒)                                       │
│                                                                  │
│  水平扩展: 哈希范围分区 + 租约认领机制                           │
└──────────────────────────────────────────────────────────────────┘
```

### 核心组件

| 组件 | 作用 |
|------|------|
| **目录搜索引擎** | 内置在 StoreFS 服务器中的搜索引擎。负责查询 OpenSearch，支持全文、向量、混合搜索，并进行过期文档验证。 |
| **CatalogBuilder** | 独立程序，扫描元数据数据库，提取内容，生成向量嵌入，将文档写入 OpenSearch 索引。支持水平扩展。 |
| **OpenSearch** | 搜索后端。需要安装 **k-NN 插件** 以支持向量相似度搜索。 |
| **Embedding API** | 兼容 OpenAI 格式的 API 端点（如 OpenAI、SiliconFlow 等），用于从文本生成向量嵌入。也可通过聊天补全接口进行图像/视频描述生成。 |

---

## 搜索功能

### 1. 全文检索

对多个字段进行加权搜索：

| 字段 | 权重 |
|------|------|
| `object_name` | 3×（最高） |
| `content_text` | 1× |
| `tags.value` | 1× |
| `meta_values` | 1× |
| `content_type` | 1× |

### 2. 向量搜索 (k-NN)

使用 OpenSearch 的 k-NN 插件，采用以下配置：
- **算法**: HNSW（分层可导航小世界图）
- **距离度量**: 余弦相似度
- **字段**: `embedding_0` 到 `embedding_{maxChunks-1}`（默认最多 8 个分块）
- **过滤支持**: 可在 k-NN 查询中按桶 ID、标签、对象类型进行过滤

### 3. 混合搜索（全文检索 + 向量搜索）

当启用了 Embedding 且提供了查询文本时，系统会使用 **RRF（倒数排序融合）** 进行混合搜索：

1. 执行全文检索（multi_match）
2. 对每个 `embedding_N` 字段执行 k-NN 向量搜索
3. 使用 RRF 融合结果：`score = Σ 1/(k + rank + 1)`，其中 `k = 60`
4. 验证结果（检查 `object_md` 表，过滤已删除的对象）
5. 按 RRF 分数降序返回前 N 条结果

如果向量搜索失败（如 Embedding API 不可用），系统会自动降级为纯全文检索。

### 4. SQL 模式搜索

轻量级 SQL 解析器可将简单查询转换为 OpenSearch DSL：

```sql
SELECT * WHERE tag='key:value' AND size>=1000 AND name LIKE '%keyword%'
SELECT * WHERE object_type='pdf' AND time_from='2024-01-01'
SELECT * WHERE prefix='documents/' AND content_type='application/pdf'
```

支持的过滤字段：`tag`、`tags`、`object_type`、`type`、`content_type`、`size`、`objects.size`、`name`、`object_name`、`prefix`、`query`、`search`、`time_from`、`time_to`。未识别的字段名会被当作用户元数据过滤条件。

---

## OpenSearch 配置

### 要求

- **OpenSearch 2.x 及以上**（需安装 k-NN 插件）
- **k-NN 插件** 已启用，用于向量相似度搜索
- 内存需足够容纳 HNSW 图（与被索引文档数成正比）

### 索引映射

`catalog-objects` 索引由 CatalogBuilder 在首次运行时自动创建。使用以下映射：

- **k-NN vector** 类型：用于 embedding 字段（HNSW，余弦相似度）
- **Nested** 类型：用于 tags 和 user_metadata
- **Keyword** 类型：用于 bucket_id、object_name、content_type、object_type
- **Text** 类型：用于 content_text、description、chunk_texts、meta_values
- **Date** 类型：用于 write_time（格式：`yyyy-MM-dd HH:mm:ss`）

索引映射必须与 Embedding 的 **维度（dimension）** 完全一致。如果维度发生变化，必须重建索引（删除后重新索引）。CatalogBuilder 在启动时会自动处理索引重建。

### 连接测试

Admin API 提供 `POST /catalog/test-connection` 接口，使用已保存的配置测试 OpenSearch 连通性。它使用 `GET /_cluster/health`（而非根路径 `/`）来避免 OpenSearch Security 插件对根路径的认证问题。

---

## Embedding（LLM）要求

### API 兼容性

Embedding 系统使用 **兼容 OpenAI 的 API 格式**：

```
POST {base_url}/embeddings
Authorization: Bearer {api_key}
Content-Type: application/json

{
  "input": ["需要嵌入的文本"],
  "model": "text-embedding-3-small",
  "dimensions": 1024
}
```

支持的服务商：
- **OpenAI**（`text-embedding-3-small`、`text-embedding-3-large`、`text-embedding-ada-002`）
- **SiliconFlow**（多种模型）
- 任何兼容 OpenAI 格式的 Embedding API

### 对话（视觉）API

CatalogBuilder 还使用对话 LLM 进行以下操作：
- **图像描述**：将图片以 base64 格式发送到 `/chat/completions`，使用视觉模型
- **视频描述**：提取视频帧，发送到视觉 API
- **音频描述**：结合 Whisper 转录结果和对话摘要

对话模型通过 CatalogBuilder 配置中的 `chat_model` 单独设置。

### 维度一致性 — 关键要求

**Embedding 维度必须在所有组件中保持一致**：

```
┌──────────────────────────────────────────────────────────────┐
│                      维度一致性要求                            │
│                                                              │
│  CatalogBuilder 配置  ──►  OpenSearch 索引映射                │
│  (embedding.dimensions)      (knn_vector.dimension)          │
│         │                                                     │
│         └──► Embedding API 必须返回这一精确维度的向量          │
│                                                              │
│  如果维度更改：                                                │
│  1. 删除 OpenSearch 索引                                      │
│  2. 更新配置                                                  │
│  3. 重新运行 CatalogBuilder（全量重新索引）                    │
│                                                              │
│  CatalogBuilder 在启动时会自动检测维度变化并重建索引           │
└──────────────────────────────────────────────────────────────┘
```

**重要说明**：`dimensions` 参数会发送给 Embedding API。如果 API 不支持 `dimensions` 参数（例如某些模型返回固定维度），客户端会自动检测并重试不带该参数的请求。此时，配置的维度必须与模型的默认输出维度一致。

### 推荐模型

| 使用场景 | 模型 | 典型维度 |
|----------|------|----------|
| 通用（英文） | `text-embedding-3-small` | 1536（默认），512–1536 可配置 |
| 高精度（英文） | `text-embedding-3-large` | 3072（默认），最高 3072 |
| 多语言 | `BAAI/bge-m3` | 1024 |
| 对话/视觉 | `deepseek-ai/DeepSeek-OCR`、`gpt-4o`、`claude-3-haiku` | 不适用（文本生成） |

---

## CatalogBuilder — 配置与使用

### 配置文件

```yaml
doris:
  host: "<host>"
  port: 9030
  database: "mydb"
  user: "root"
  password: ""

s3:
  endpoint: "http://<host>:8901"
  access_key: "<access key>"
  secret_key: "<secret key>"
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

### 配置字段说明

| 配置段 | 字段 | 默认值 | 说明 |
|--------|------|--------|------|
| `doris` | `host` | `127.0.0.1` | Doris/MySQL 地址 |
| `doris` | `port` | `9030` | Doris/MySQL 端口 |
| `doris` | `database` | `mydb` | 数据库名 |
| `s3` | `endpoint` | `http://127.0.0.1:8901` | S3 端点，用于下载对象 |
| `s3` | `access_key` | — | S3 访问密钥（需有读取权限） |
| `s3` | `secret_key` | — | S3 密钥 |
| `opensearch` | `urls` | `["http://127.0.0.1:9200"]` | OpenSearch 地址列表 |
| `opensearch` | `index_objects` | `catalog-objects` | 对象索引名称 |
| `embedding` | `base_url` | `https://api.openai.com/v1` | Embedding API 基础 URL |
| `embedding` | `model` | `text-embedding-3-small` | Embedding 模型名称 |
| `embedding` | `dimensions` | `1024` | 向量维度 |
| `embedding` | `batch_size` | `20` | Embedding 请求批处理大小 |
| `embedding` | `concurrency` | `10` | 最大并发 Embedding 工作线程数 |
| `embedding` | `rate_limit_rpm` | `3000` | API 速率限制（每分钟请求数） |
| `embedding` | `chat_model` | — | 对话/视觉模型（不设置则使用 Embedding 模型） |
| `embedding` | `max_chunk_chars` | `8000` | 每个 Embedding 分块的最大字符数 |
| `embedding` | `max_chunks` | `8` | 每个文档最多分块数 |
| `builder` | `scan_batch_size` | `100` | 每次扫描的批处理大小 |
| `builder` | `incremental_interval` | `30s` | 增量扫描间隔 |
| `builder` | `media.enabled` | `false` | 启用音频/视频/图像处理 |
| `builder` | `media.whisper_binary` | — | whisper-cli 路径（用于音频转写） |
| `builder` | `media.ffmpeg_binary` | — | ffmpeg 路径（用于视频处理） |

### 使用方式

```bash
# 全量扫描（首次运行）
./catalogbuilder -config catalogbuilder.yaml

# 仅增量扫描（跳过全量扫描）
./catalogbuilder -config catalogbuilder.yaml -skip-full-scan

# 查看扫描进度
./catalogbuilder -config catalogbuilder.yaml status

# 详细模式（显示每个桶的进度）
./catalogbuilder -config catalogbuilder.yaml -verbose
```

### 扫描流程

1. **全量扫描**：扫描所有 `catalog_processed = 0` 的 `object_md` 记录。对每个对象执行：下载 → 提取文本 → 生成嵌入 → 写入索引。
2. **增量扫描**：每 `incremental_interval`（默认 30 秒）执行一次。扫描 `write_time > 上次标记` 且 `catalog_processed = 0` 的对象。
3. **水平扩展**：可同时运行多个 CatalogBuilder 实例。每个实例通过 `catalog_scan_claims` 表认领哈希范围分区。宕机实例通过心跳检测（`catalog_instances` 表）自动发现，其认领的任务会被重新分配。

### 内容提取

CatalogBuilder 支持从多种文件格式中提取文本内容：

| 格式 | 提取器 | 说明 |
|------|--------|------|
| 纯文本、JSON、XML、CSV、源代码 | `PlainTextExtractor` | 直接读取，最大 1 MB |
| HTML、XHTML | `HTMLExtractor` | 去除标签，提取文本 |
| PDF | `PDFExtractor` | 读取所有页面 |
| DOCX | `DOCXExtractor` | 读取 `word/document.xml` |
| XLSX | `XLSXExtractor` | 读取所有工作表 |
| EPUB | `EPUBExtractor` | 读取所有 XHTML/HTML 内容 |
| MOBI | `MOBIParser` | PalmDOC 解压，HTML 去除 |
| 音频 | `Whisper`（外部工具） | 需要 `whisper-cli` |
| 视频 | `FFmpeg` + `Whisper` + `视觉 LLM` | 音频→Whisper，帧→视觉 API |
| 图像 | `视觉 LLM` | Base64→带视觉的聊天补全 |

### 媒体处理安装（音频 / 视频 / 图像）

要启用媒体内容处理（音频转写、视频描述、图像描述），CatalogBuilder 需要两个外部工具。在配置中设置 `builder.media.enabled: true` 启用处理，并配置二进制路径。

#### 1. 安装 Whisper（whisper-cli）

音频转写和视频音轨转写需要 [whisper.cpp](https://github.com/ggerganov/whisper.cpp)：

```bash
# 克隆并编译 whisper.cpp
git clone https://github.com/ggerganov/whisper.cpp.git
cd whisper.cpp
make

# 下载模型（例如 "small" 多语言模型）
./models/download-ggml-model.sh small
```

编译完成后：
- 二进制位于 `build/bin/whisper-cli`
- 模型文件位于 `models/ggml-small.bin`

在 `catalogbuilder.yaml` 中配置：

```yaml
builder:
  media:
    enabled: true
    whisper_binary: "/path/to/whisper.cpp/build/bin/whisper-cli"
    whisper_model: "/path/to/whisper.cpp/models/ggml-small.bin"   # 模型文件的绝对路径
```

> **注意**：`whisper_model` 必须是**模型文件的绝对路径**，不只是模型名称。模型文件大小取决于所选模型（small ≈ 466 MB，base ≈ 142 MB，tiny ≈ 75 MB）。

#### 2. 安装 FFmpeg

视频处理（音轨提取和帧提取）需要 [FFmpeg](https://ffmpeg.org/)，以及检测音轨所需的 `ffprobe`（随 FFmpeg 一起安装）：

```bash
# Ubuntu / Debian
sudo apt install ffmpeg

# CentOS / RHEL
sudo yum install ffmpeg

# macOS（Homebrew）
brew install ffmpeg
```

验证安装：

```bash
ffmpeg -version
ffprobe -version
```

在 `catalogbuilder.yaml` 中配置：

```yaml
builder:
  media:
    enabled: true
    ffmpeg_binary: "/usr/bin/ffmpeg"
    frame_fps: 1   # 每秒提取的视频帧数，用于视频描述
```

> **注意**：`ffprobe` 二进制会从 `ffmpeg_binary` 路径自动检测（将路径中的 `ffmpeg` 替换为 `ffprobe`）。它用于在运行 Whisper 之前检查视频是否有音轨。

#### 3. 视频音轨自动检测

视频处理流程：
1. `ffprobe` 检查视频是否包含音轨
2. 如果有：FFmpeg 提取音轨 → Whisper 进行转写
3. 无论是否有音频：FFmpeg 提取一帧画面（`frame_fps` 帧/秒，缩放到 640px 宽）→ 视觉 LLM 生成描述

#### 4. 图像 / 视频帧的 LLM 要求

图像和视频帧描述使用对话视觉接口（`/chat/completions`）。配置的 `embedding.chat_model` 必须支持**视觉（图像输入）**。如果对话模型不支持视觉，图像的描述生成会失败——此时请保持 `media.enabled: false`，或使用支持视觉的模型。

---

## Admin API — 目录搜索接口

所有目录搜索接口位于 Admin API 端口（默认 7946）的 `/catalog/` 路径下。配置修改需要 super_admin 权限，搜索功能对所有已认证用户开放。

| 方法 | 端点 | 说明 | 权限 |
|------|------|------|------|
| `GET` | `/catalog/config` | 获取目录搜索配置（OpenSearch + Embedding） | super_admin |
| `PUT` | `/catalog/config` | 更新目录搜索配置 | super_admin |
| `POST` | `/catalog/enable` | 启用目录搜索引擎 | super_admin |
| `POST` | `/catalog/disable` | 禁用目录搜索引擎 | super_admin |
| `GET` | `/catalog/stats` | 获取目录搜索状态（启用/禁用，端点信息） | 已认证 |
| `GET` | `/catalog/objects/search` | 搜索对象（全文检索 + 过滤） | 已认证 |
| `GET` | `/catalog/objects/detail` | 获取对象元数据详情 | 已认证 |
| `GET` | `/catalog/search` | SQL 模式搜索 | 已认证 |
| `POST` | `/catalog/search/vector` | 向量相似度搜索 | 已认证 |
| `POST` | `/catalog/test-connection` | 测试 OpenSearch 连通性 | super_admin |
| `POST` | `/catalog/test-embedding` | 测试 Embedding API 连通性 | super_admin |

### 搜索参数

`GET /catalog/objects/search`

| 参数 | 类型 | 说明 |
|------|------|------|
| `query` | string | 全文检索关键词 |
| `object_type` | string | 过滤：`text`、`pdf`、`office`、`binary`、`image`、`audio`、`video` |
| `content_type` | string | 过滤：MIME 类型（如 `application/pdf`） |
| `tags` | string | 过滤：逗号分隔的 `key:value` 对（如 `project:alpha,dept:eng`） |
| `meta` | string | 过滤：用户元数据 `key:value` 对 |
| `prefix` | string | 过滤：对象名前缀 |
| `bucket_ids` | string | 过滤：逗号分隔的桶 ID |
| `size_min` | int | 最小对象大小（字节） |
| `size_max` | int | 最大对象大小（字节） |
| `time_from` | string | 时间范围起始（RFC3339） |
| `time_to` | string | 时间范围结束（RFC3339） |
| `limit` | int | 最大返回数（默认 20，最大 100，Web 前端固定使用 100） |
| `from` | int | 结果偏移量（用于分页，默认 0。Web 前端在前端本地做分页，不传此参数） |

### 访问控制

搜索结果会根据用户权限自动过滤：
- **super_admin**：可搜索所有桶
- **group_admin**：可搜索其分组拥有的桶 + ACL 授权桶
- **普通用户**：可搜索自己拥有的桶 + ACL 授权桶

---

## Web 管理界面 — 目录搜索页面

**目录搜索** 页面位于 Web 管理控制台的目录搜索（Catalog）部分。

### 功能

1. **搜索栏**：输入关键词进行全文检索
2. **高级过滤**：可展开的面板，包含：
   - 对象类型（全部/文本/办公文档/PDF/二进制）
   - 标签（key:value 格式）
   - 前缀（路径前缀过滤）
   - 大小范围（最小值/最大值）
3. **结果表格**：显示桶名称、对象名、类型、大小、内容类型、写入时间、相关性得分
4. **对象详情**：点击对象名查看元数据
5. **结果操作**：复制预签名 URL、下载、删除（根据权限）

### 目录搜索流程

```
┌──────────┐     ┌─────────────┐     ┌─────────────┐     ┌───────────┐
│ Web 管理 │────►│ 目录搜索引擎 │────►│ OpenSearch  │◄────│ Catalog  │
│ 界面     │     │ (Go/搜索)   │     │ 集群        │     │ Builder  │
│          │     │             │     │              │     │ (Go 程序) │
└──────────┘     └─────────────┘     └─────────────┘     └───────────┘
                                                                    │
                                                                    ▼
                                                             ┌───────────┐
                                                             │ StoreFS   │
                                                             │ S3 + 数据库│
                                                             └───────────┘
```

1. **CatalogBuilder** 扫描数据库获取新对象，从 S3 下载内容，提取文本，生成向量嵌入，写入 OpenSearch 索引。
2. **Web 管理界面** 将搜索请求发送到 StoreFS Admin API。
3. **目录搜索引擎** 查询 OpenSearch，应用权限过滤，验证结果，返回给界面。
4. 当用户上传/删除对象时，通过 S3 API 处理器实时更新 OpenSearch 索引。

---

## MCP 工具

StoreFS MCP 服务器提供以下目录搜索相关工具（详见 [MCP 文档](mcp_cn.md)）：

| 工具 | 说明 |
|------|------|
| `storefs_catalog_stats` | 获取目录搜索状态（启用/禁用，OpenSearch 信息） |
| `storefs_catalog_enable` | 启用目录搜索引擎 |
| `storefs_catalog_disable` | 禁用目录搜索引擎 |
| `storefs_catalog_config` | 更新目录搜索配置（OpenSearch + Embedding） |
| `storefs_catalog_get_config` | 获取当前目录搜索配置 |
| `storefs_catalog_search_objects` | 全文检索 + 过滤搜索对象 |
| `storefs_catalog_search` | SQL 模式搜索（`SELECT * WHERE ...`） |
| `storefs_catalog_vector_search` | 向量相似度搜索（按向量或文本） |
| `storefs_catalog_get_object_metadata` | 获取对象元数据（user_meta、http_meta） |
| `storefs_catalog_test_connection` | 测试 OpenSearch 连接 |
| `storefs_catalog_test_embedding` | 测试 Embedding API 连接 |

---

## 故障排查

### 搜索无结果

1. 检查目录搜索是否已启用：`GET /catalog/stats`
2. 检查 CatalogBuilder 是否正在运行且已处理对象
3. 检查 OpenSearch 是否可达：`POST /catalog/test-connection`
4. 检查 `object_md` 表中的 `catalog_processed` 标志——对象必须被处理过

### Embedding 失败

1. 测试 Embedding API：`POST /catalog/test-embedding`
2. 检查 API 密钥是否有效，且有权访问指定的模型
3. 验证模型是否支持配置的向量维度（或自动检测是否正常工作）
4. 检查速率限制——`rate_limit_rpm` 设置可能过低

### 混合搜索不工作

1. 确保在 StoreFS 配置（Web 管理界面）和 CatalogBuilder 配置中都启用了 Embedding
2. 检查 Embedding 配置的维度与 OpenSearch 索引映射是否一致
3. 如果索引创建时使用了不同的维度，请删除并重新创建索引

### 过期结果

目录搜索引擎在返回结果前会与 `object_md` 表进行验证。如果对象已从 StoreFS 删除但 OpenSearch 索引未更新，过期文档会自动从搜索结果中移除，并在下次查询时从索引中删除。