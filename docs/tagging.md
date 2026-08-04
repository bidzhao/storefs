**[查看中文版](tagging_cn.md)**

# Tagging Documentation

## Overview

StoreFS supports S3-compatible tagging for both buckets and objects. Tags are key-value pairs that can be used for categorization, access control, and cost tracking. Tags can be managed via the S3 XML API and are supported during copy operations and multipart uploads.

## Tag Constraints

| Constraint | Bucket Tags | Object Tags |
|-----------|-------------|-------------|
| Maximum tags per resource | 50 | 10 |
| Tag key length | 1-128 characters | 1-128 characters |
| Tag value length | 0-256 characters | 0-256 characters |
| Tag key characters | Unicode letters, digits, spaces, and common symbols | Same |
| Tag value characters | Unicode letters, digits, spaces, and common symbols | Same |

Tag keys and values are case-sensitive as stored, but case-insensitive duplicate detection is applied (normalized to lowercase for comparison).

## Bucket Tagging

### Get Bucket Tagging

Retrieve all tags associated with a bucket.

**URL**: `GET /<bucket>?tagging`

**Required Permission**: `READ`

**Request**:
```http
GET /mybucket?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**Response** (200 OK):
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

**Error Responses**:
- 404 Not Found: Bucket does not exist
- 403 Forbidden: No `READ` permission
- 401 Unauthorized: Authentication failed

### Put Bucket Tagging

Replace all existing tags on a bucket with the provided tags (atomic operation).

**URL**: `PUT /<bucket>?tagging`

**Required Permission**: `WRITE`

**Request**:
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

**Notes**:
- Replaces all existing tags atomically — any tags not in the request are removed
- Max 50 tags per bucket
- Invalid tag keys or values (wrong length) will cause the entire request to fail with `InvalidTag`

**Response**:
```http
HTTP/1.1 200 OK
Content-Length: 0
```

**Error Responses**:
- 404 Not Found: Bucket does not exist
- 403 Forbidden: No `WRITE` permission
- 400 Bad Request: `InvalidTag` — tag key/value validation failure or too many tags
- 400 Bad Request: `MalformedXML` — XML body is malformed
- 401 Unauthorized: Authentication failed

### Delete Bucket Tagging

Remove all tags from a bucket.

**URL**: `DELETE /<bucket>?tagging`

**Required Permission**: `WRITE`

**Request**:
```http
DELETE /mybucket?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**Response**:
```http
HTTP/1.1 204 No Content
Content-Length: 0
```

**Error Responses**:
- 404 Not Found: Bucket does not exist
- 403 Forbidden: No `WRITE` permission
- 401 Unauthorized: Authentication failed

## Object Tagging

Object tagging allows you to assign key-value pairs (tags) to objects stored in StoreFS. Tags are version-aware: when versioning is enabled, tags are stored per object version.

### Get Object Tagging

Retrieve all tags associated with an object or a specific object version.

**URL**: `GET /<bucket>/<object>?tagging[&versionId=<version_id>]`

**Request Parameters**:
- `versionId` (optional): Specific object version ID. If omitted, returns tags for the current version.

**Required Permission**: `READ`

**Request**:
```http
GET /mybucket/document.pdf?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**Response** (200 OK):
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

**Error Responses**:
- 404 Not Found: Bucket or object does not exist
- 403 Forbidden: No `READ` permission
- 401 Unauthorized: Authentication failed

### Put Object Tagging

Replace all existing tags on an object with the provided tags (atomic operation).

**URL**: `PUT /<bucket>/<object>?tagging[&versionId=<version_id>]`

**Request Parameters**:
- `versionId` (optional): Specific object version ID. If omitted, tags are applied to the current version.

**Required Permission**: `WRITE`

**Request**:
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

**Notes**:
- Replaces all existing tags on the object atomically
- Max 10 tags per object
- When versioning is enabled, tags are applied to the specified version (or the current version if `versionId` is omitted)
- Different versions of the same object can have different tags

**Response**:
```http
HTTP/1.1 200 OK
Content-Length: 0
```

**Error Responses**:
- 404 Not Found: Bucket or object does not exist
- 403 Forbidden: No `WRITE` permission
- 400 Bad Request: `InvalidTag` — tag key/value validation failure or too many tags
- 400 Bad Request: `MalformedXML` — XML body is malformed
- 401 Unauthorized: Authentication failed

### Delete Object Tagging

Remove all tags from an object (or a specific object version).

**URL**: `DELETE /<bucket>/<object>?tagging[&versionId=<version_id>]`

**Request Parameters**:
- `versionId` (optional): Specific object version ID. If omitted, tags are removed from the current version.

**Required Permission**: `WRITE`

**Request**:
```http
DELETE /mybucket/document.pdf?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**Response**:
```http
HTTP/1.1 204 No Content
Content-Length: 0
```

**Error Responses**:
- 404 Not Found: Bucket or object does not exist
- 403 Forbidden: No `WRITE` permission
- 401 Unauthorized: Authentication failed

## Tagging During Copy Operations

When copying objects, you can control the tag behavior using the following request headers.

**Request Headers**:

| Header | Description |
|--------|-------------|
| `x-amz-tagging-directive` | `COPY` — copy tags from source; `REPLACE` (default) — set tags from `x-amz-tagging` header |
| `x-amz-tagging` | Tag set in URL-encoded format (`key1=value1&key2=value2`) when using `REPLACE` directive |

**Request** (Replace tags during copy):
```http
PUT /mybucket/destination.pdf HTTP/1.1
Host: 127.0.0.1:8901
x-amz-copy-source: /mybucket/source.pdf
x-amz-tagging-directive: REPLACE
x-amz-tagging: department=engineering&type=report
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**Notes**:
- `x-amz-tagging-directive: COPY` — Copies tags from the source object to the destination object
- `x-amz-tagging-directive: REPLACE` (default) — Sets tags on the destination object from the `x-amz-tagging` header
- If neither header is present, no tags are set on the destination object
- Tags specified via `x-amz-tagging` header must be URL-encoded (`key=value` pairs separated by `&`)

## Tagging During Multipart Uploads

During multipart upload initialization, you can specify tags via the `x-amz-tagging` header. These tags are stored with the multipart upload metadata and automatically applied to the final object when the multipart upload is completed.

**URL**: `POST /<bucket>/<object>?uploads`

**Request**:
```http
POST /mybucket/largefile.zip?uploads HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/zip
x-amz-tagging: project=storefs&department=engineering
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**Notes**:
- Tags are specified in URL-encoded key-value format: `key1=value1&key2=value2`
- Max 10 tags per multipart upload (object tag limit)
- Tags are stored with the multipart upload metadata and applied to the final object upon completion
- If the `x-amz-tagging` header is not provided, the final object will have no tags

## Admin API

The Admin API does not provide dedicated endpoints for managing tags. Tag management is performed through the S3 API only.

## Database Schema

### Bucket Tags Table

```sql
CREATE TABLE IF NOT EXISTS bucket_tags (
    bucket_id            BIGINT       NOT NULL,
    tag_key              VARCHAR(128) NOT NULL,
    tag_value            VARCHAR(256) NOT NULL,
    normalized_tag_key   VARCHAR(128) NOT NULL COMMENT 'lowercase for case-insensitive search',
    normalized_tag_value VARCHAR(256) NOT NULL COMMENT 'lowercase for case-insensitive search',
    created_at           DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX index_bucket_tags_key (tag_key) USING BITMAP,
    INDEX index_bucket_tags_normalized_key (normalized_tag_key) USING GIN
) PRIMARY KEY (bucket_id, tag_key);
```

### Object Tags Table

```sql
CREATE TABLE IF NOT EXISTS object_tags (
    bucket_id            BIGINT        NOT NULL,
    obj_name_hash        LARGEINT      NOT NULL,
    object_name          VARCHAR(1024) NOT NULL,
    version              VARCHAR(36)   NOT NULL DEFAULT 'null',
    tag_key              VARCHAR(128)  NOT NULL,
    tag_value            VARCHAR(256)  NOT NULL,
    normalized_tag_key   VARCHAR(128)  NOT NULL COMMENT 'lowercase for case-insensitive search',
    normalized_tag_value VARCHAR(256)  NOT NULL COMMENT 'lowercase for case-insensitive search',
    created_at           DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX index_object_tags_key (tag_key) USING BITMAP,
    INDEX index_object_tags_normalized_key (normalized_tag_key) USING GIN
) PRIMARY KEY (bucket_id, obj_name_hash, object_name, version, tag_key);
```

## Usage Examples

### Set Tags on a Bucket

```bash
aws s3api put-bucket-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket \
  --tagging 'TagSet=[{Key=project,Value=storefs},{Key=owner,Value=admin}]'
```

### Get Tags from a Bucket

```bash
aws s3api get-bucket-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket
```

### Delete Tags from a Bucket

```bash
aws s3api delete-bucket-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket
```

### Set Tags on an Object

```bash
aws s3api put-object-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket \
  --key document.pdf \
  --tagging 'TagSet=[{Key=department,Value=engineering},{Key=type,Value=report}]'
```

### Get Tags from an Object

```bash
aws s3api get-object-tagging \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket \
  --key document.pdf
```

### Set Tags via Header During Upload

```bash
aws s3api put-object \
  --endpoint-url http://127.0.0.1:8901 \
  --bucket mybucket \
  --key document.pdf \
  --body ./document.pdf \
  --tagging "department=engineering&type=report"
```

## Key Behaviors

1. **Atomic replacement**: Setting tags replaces all existing tags. There is no "add tag" operation — the full desired set must be provided each time.

2. **Version-aware**: When versioning is enabled, object tags are stored per version. Different versions of the same object can have different tags. The `versionId` query parameter controls which version's tags are accessed.

3. **Copy behavior**: By default, copying an object does not copy its tags (REPLACE). Use `x-amz-tagging-directive: COPY` to copy tags from the source object.

4. **Multipart upload tags**: Tags specified during multipart upload initialization are preserved and applied to the final object upon completion.

5. **Validation**: Invalid tag keys or values (wrong length) cause the entire tagging operation to fail with an `InvalidTag` error. There is no partial success.
