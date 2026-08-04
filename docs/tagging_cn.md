**[English](tagging.md)**

# 标签（Tagging）文档

## 概述

StoreFS 支持与 S3 兼容的桶和对象标签功能。标签是键值对，可用于分类、访问控制和成本跟踪。标签可以通过 S3 XML API 进行管理，并在复制操作和分块上传中得到支持。

## 标签约束

| 约束条件 | 桶标签 | 对象标签 |
|---------|--------|---------|
| 每个资源最大标签数 | 50 | 10 |
| 标签键长度 | 1-128 字符 | 1-128 字符 |
| 标签值长度 | 0-256 字符 | 0-256 字符 |
| 标签键字符 | Unicode 字母、数字、空格和常用符号 | 同上 |
| 标签值字符 | Unicode 字母、数字、空格和常用符号 | 同上 |

标签键和值按存储时的大小写敏感方式保留，但去重检测时使用不区分大小写的归一化（转换为小写）比较。

## 桶标签

### 获取桶标签

获取桶的所有标签。

**URL**：`GET /<bucket>?tagging`

**所需权限**：`READ`

**请求**：
```http
GET /mybucket?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<Tagging xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <TagSet>
    <Tag>
      <Key>project</Key>
      <Value>storefs</Value>
    </Tag>
    <Tag>
      <Key>owner</Key>
      <Value>admin</Value>
    </Tag>
  </TagSet>
</Tagging>
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `READ` 权限
- 401 Unauthorized：认证失败

### 设置桶标签

用提供的标签原子替换桶上的所有现有标签。

**URL**：`PUT /<bucket>?tagging`

**所需权限**：`WRITE`

**请求**：
```http
PUT /mybucket?tagging HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<Tagging xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <TagSet>
    <Tag>
      <Key>project</Key>
      <Value>storefs</Value>
    </Tag>
    <Tag>
      <Key>owner</Key>
      <Value>admin</Value>
    </Tag>
  </TagSet>
</Tagging>
```

**说明**：
- 原子替换所有现有标签 — 请求中未包含的标签将被移除
- 每个桶最多 50 个标签
- 标签键或值长度无效时，整个请求将失败并返回 `InvalidTag` 错误

**响应**：
```http
HTTP/1.1 200 OK
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `WRITE` 权限
- 400 Bad Request：`InvalidTag` — 标签键/值验证失败或标签数量过多
- 400 Bad Request：`MalformedXML` — XML 格式错误
- 401 Unauthorized：认证失败

### 删除桶标签

删除桶的所有标签。

**URL**：`DELETE /<bucket>?tagging`

**所需权限**：`WRITE`

**请求**：
```http
DELETE /mybucket?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```http
HTTP/1.1 204 No Content
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `WRITE` 权限
- 401 Unauthorized：认证失败

## 对象标签

对象标签允许您为存储的对象分配键值对（标签）。标签是版本感知的：启用版本控制时，标签按对象版本存储。

### 获取对象标签

获取对象或特定对象版本的标签。

**URL**：`GET /<bucket>/<object>?tagging[&versionId=<version_id>]`

**请求参数**：
- `versionId`（可选）：特定对象版本的 ID。省略时返回当前版本的标签。

**所需权限**：`READ`

**请求**：
```http
GET /mybucket/document.pdf?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<Tagging xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <TagSet>
    <Tag>
      <Key>department</Key>
      <Value>engineering</Value>
    </Tag>
    <Tag>
      <Key>type</Key>
      <Value>report</Value>
    </Tag>
  </TagSet>
</Tagging>
```

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：无 `READ` 权限
- 401 Unauthorized：认证失败

### 设置对象标签

用提供的标签原子替换对象上的所有现有标签。

**URL**：`PUT /<bucket>/<object>?tagging[&versionId=<version_id>]`

**请求参数**：
- `versionId`（可选）：特定对象版本的 ID。省略时标签应用于当前版本。

**所需权限**：`WRITE`

**请求**：
```http
PUT /mybucket/document.pdf?tagging HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<Tagging xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <TagSet>
    <Tag>
      <Key>department</Key>
      <Value>engineering</Value>
    </Tag>
    <Tag>
      <Key>type</Key>
      <Value>report</Value>
    </Tag>
  </TagSet>
</Tagging>
```

**说明**：
- 原子替换对象的所有现有标签
- 每个对象最多 10 个标签
- 启用版本控制时，标签应用于指定版本（如果省略 `versionId`，则应用于当前版本）
- 同一对象的不同版本可以有不同的标签

**响应**：
```http
HTTP/1.1 200 OK
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：无 `WRITE` 权限
- 400 Bad Request：`InvalidTag` — 标签键/值验证失败或标签数量过多
- 400 Bad Request：`MalformedXML` — XML 格式错误
- 401 Unauthorized：认证失败

### 删除对象标签

删除对象（或特定对象版本）的所有标签。

**URL**：`DELETE /<bucket>/<object>?tagging[&versionId=<version_id>]`

**请求参数**：
- `versionId`（可选）：特定对象版本的 ID。省略时从当前版本删除标签。

**所需权限**：`WRITE`

**请求**：
```http
DELETE /mybucket/document.pdf?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```http
HTTP/1.1 204 No Content
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：无 `WRITE` 权限
- 401 Unauthorized：认证失败

## 复制操作中的标签处理

复制对象时，可以通过以下请求头控制标签行为。

**请求头**：

| 请求头 | 说明 |
|--------|------|
| `x-amz-tagging-directive` | `COPY` — 从源对象复制标签；`REPLACE`（默认）— 从 `x-amz-tagging` 请求头设置标签 |
| `x-amz-tagging` | URL 编码的标签格式（`key1=value1&key2=value2`），在使用 `REPLACE` 指令时使用 |

**请求**（复制时替换标签）：
```http
PUT /mybucket/destination.pdf HTTP/1.1
Host: 127.0.0.1:8901
x-amz-copy-source: /mybucket/source.pdf
x-amz-tagging-directive: REPLACE
x-amz-tagging: department=engineering&type=report
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**说明**：
- `x-amz-tagging-directive: COPY` — 将源对象的标签复制到目标对象
- `x-amz-tagging-directive: REPLACE`（默认）— 从 `x-amz-tagging` 请求头设置目标对象的标签
- 如果两个请求头都不存在，目标对象将没有标签
- 通过 `x-amz-tagging` 请求头指定的标签必须 URL 编码（`key=value` 对，用 `&` 分隔）

## 分块上传中的标签处理

在初始化分块上传时，可以通过 `x-amz-tagging` 请求头指定标签。这些标签与分块上传元数据一起存储，并在分块上传完成时自动应用于最终对象。

**URL**：`POST /<bucket>/<object>?uploads`

**请求**：
```http
POST /mybucket/largefile.zip?uploads HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/zip
x-amz-tagging: project=storefs&department=engineering
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**说明**：
- 标签以 URL 编码的键值对格式指定：`key1=value1&key2=value2`
- 每次分块上传最多 10 个标签（对象标签限制）
- 标签与分块上传元数据一起存储，并在完成时应用于最终对象
- 如果未提供 `x-amz-tagging` 请求头，最终对象将没有标签

## 管理 API

管理 API 不提供专门的标签管理端点。标签管理仅通过 S3 API 进行。

## 数据库结构

### 桶标签表

```sql
CREATE TABLE IF NOT EXISTS bucket_tags (
    bucket_id            BIGINT       NOT NULL,
    tag_key              VARCHAR(128) NOT NULL,
    tag_value            VARCHAR(256) NOT NULL,
    normalized_tag_key   VARCHAR(128) NOT NULL COMMENT '小写归一化，用于不区分大小写的搜索',
    normalized_tag_value VARCHAR(256) NOT NULL COMMENT '小写归一化，用于不区分大小写的搜索',
    created_at           DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX index_bucket_tags_key (tag_key) USING BITMAP,
    INDEX index_bucket_tags_normalized_key (normalized_tag_key) USING GIN
) PRIMARY KEY (bucket_id, tag_key);
```

### 对象标签表

```sql
CREATE TABLE IF NOT EXISTS object_tags (
    bucket_id            BIGINT        NOT NULL,
    obj_name_hash        LARGEINT      NOT NULL,
    object_name          VARCHAR(1024) NOT NULL,
    version              VARCHAR(36)   NOT NULL DEFAULT 'null',
    tag_key              VARCHAR(128)  NOT NULL,
    tag_value            VARCHAR(256)  NOT NULL,
    normalized_tag_key   VARCHAR(128)  NOT NULL COMMENT '小写归一化，用于不区分大小写的搜索',
    normalized_tag_value VARCHAR(256)  NOT NULL COMMENT '小写归一化，用于不区分大小写的搜索',
    created_at           DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX index_object_tags_key (tag_key) USING BITMAP,
    INDEX index_object_tags_normalized_key (normalized_tag_key) USING GIN
) PRIMARY KEY (bucket_id, obj_name_hash, object_name, version, tag_key);
```

## 使用示例

### 设置桶标签

```bash
aws s3api put-bucket-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket \
  --tagging 'TagSet=[{Key=project,Value=storefs},{Key=owner,Value=admin}]'
```

### 获取桶标签

```bash
aws s3api get-bucket-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket
```

### 删除桶标签

```bash
aws s3api delete-bucket-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket
```

### 设置对象标签

```bash
aws s3api put-object-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket \
  --key document.pdf \
  --tagging 'TagSet=[{Key=department,Value=engineering},{Key=type,Value=report}]'
```

### 获取对象标签

```bash
aws s3api get-object-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket \
  --key document.pdf
```

### 上传时通过请求头设置标签

```bash
aws s3api put-object \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket \
  --key document.pdf \
  --body ./document.pdf \
  --tagging "department=engineering&type=report"
```

## 关键行为说明

1. **原子替换**：设置标签会替换所有现有标签。没有"添加标签"操作——每次必须提供完整的期望标签集。

2. **版本感知**：启用版本控制时，对象标签按版本存储。同一对象的不同版本可以有不同的标签。`versionId` 查询参数控制访问哪个版本的标签。

3. **复制行为**：默认情况下，复制对象不会复制其标签（REPLACE）。使用 `x-amz-tagging-directive: COPY` 从源对象复制标签。

4. **分块上传标签**：在初始化分块上传时指定的标签会被保留，并在完成时自动应用于最终对象。

5. **验证**：无效的标签键或值（长度错误）会导致整个标签操作失败，返回 `InvalidTag` 错误。不存在部分成功的情况。
