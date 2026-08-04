**[查看中文版](notification_cn.md)**

# Bucket Notification (Webhook) Documentation

## Overview

StoreFS provides a webhook-based notification system that sends real-time event notifications when objects are created or deleted in a bucket. This enables external services to react to storage events without polling.

### How It Works

```
User uploads object
       ↓
StoreFS fires event → Event batching (1s window / 100 events)
       ↓
Build payload → Insert into queue table
       ↓
Worker picks up → POST webhook to configured URL
       ↓
                   ┌── 2xx → Mark completed
Webhook response ──┼── 4xx → Discard (client error)
                   └── 5xx/network error → Retry with backoff
```

The notification engine runs on each StoreFS node:

1. **Event Capture**: S3 operations (PutObject, CopyObject, DeleteObject, CompleteMultipartUpload) fire events.
2. **Batching**: Events are batched per notification config (1-second window or 100-event max, whichever comes first).
3. **Queue**: Batched events are serialized into a persistent queue table (`notification_queue`).
4. **Delivery**: A background worker polls the queue every 200ms and delivers webhooks via HTTP POST.
5. **Retry**: Failed deliveries are retried with exponential backoff (1s → 5s → 30s → 5m → 30m).
6. **TTL**: Queue rows expire after 3 days and are auto-discarded.

### Supported Event Types

| Event Type | S3 Event Name | Description |
|-----------|---------------|-------------|
| `s3:ObjectCreated:*` | All creation events | Matches all object creation operations |
| `s3:ObjectCreated:Put` | PutObject | Direct object upload via PUT |
| `s3:ObjectCreated:Copy` | CopyObject | Object copy operation |
| `s3:ObjectCreated:CompleteMultipartUpload` | MultipartUpload | Completion of a multipart upload |
| `s3:ObjectCreated:Rename` | Rename (StoreFS extension) | Object rename operation |
| `s3:ObjectRemoved:Delete` | Delete | Object deletion |
| `s3:ObjectRemoved:DeleteMarkerCreated` | DeleteMarker | Creation of a delete marker (versioned buckets) |

## Payload Formats

StoreFS supports two webhook payload formats: **native** (simplified StoreFS format) and **aws** (AWS S3-compatible format).

### Native Format (default)

Single event:

```json
{
  "eventVersion": "1.0",
  "eventId": "ev-1722153600000000000-12345",
  "eventTime": "2025-07-28T12:00:00Z",
  "eventType": "ObjectCreated",
  "source": "storefs",
  "bucket": "my-bucket",
  "object": {
    "key": "documents%2Freport.pdf",
    "size": 1024000,
    "etag": "\"abc123\"",
    "versionId": "uuid-version-id"
  }
}
```

Batch (multiple events in 1-second window):

```json
{
  "events": [
    {
      "eventVersion": "1.0",
      "eventId": "ev-...",
      "eventTime": "2025-07-28T12:00:00Z",
      "eventType": "ObjectCreated",
      "source": "storefs",
      "bucket": "my-bucket",
      "object": {
        "key": "documents%2Ffile1.pdf",
        "size": 512000,
        "etag": "\"abc123\"",
        "versionId": ""
      }
    }
  ]
}
```

### AWS S3 Compatible Format

```json
{
  "Records": [
    {
      "eventVersion": "2.1",
      "eventSource": "aws:s3",
      "awsRegion": "us-east-1",
      "eventName": "s3:ObjectCreated:Put",
      "eventTime": "2025-07-28T12:00:00.000Z",
      "userIdentity": {
        "principalId": "1"
      },
      "requestParameters": {
        "sourceIPAddress": "10.0.0.1"
      },
      "responseElements": {},
      "s3": {
        "s3SchemaVersion": "1.0",
        "bucket": {
          "name": "my-bucket",
          "ownerIdentity": {
            "principalId": "1"
          },
          "arn": "arn:aws:s3:::my-bucket"
        },
        "object": {
          "key": "documents%2Freport.pdf",
          "size": 1024000,
          "eTag": "\"abc123\"",
          "versionId": "uuid-version-id",
          "sequencer": "..."
        }
      },
      "storefs": {
        "cluster": "mycluster",
        "node": "node1",
        "eventId": "ev-..."
      }
    }
  ]
}
```

## Webhook Delivery

### Request Headers

| Header | Description |
|--------|-------------|
| `Content-Type` | `application/json` |
| `User-Agent` | `StoreFS/1.0` |
| `X-StoreFS-Event` | Event name (e.g., `s3:ObjectCreated:Put`) |
| `X-StoreFS-Event-ID` | Unique event identifier |
| `X-StoreFS-Signature` | HMAC-SHA256 signature (only if secret is configured) |

### Signature Verification

If a webhook secret is configured, each delivery includes an `X-StoreFS-Signature` header:

```
X-StoreFS-Signature: sha256=abc123def456...
```

To verify the signature on the receiver side:

```python
import hmac, hashlib

def verify_signature(secret, body, signature_header):
    expected = "sha256=" + hmac.new(
        secret.encode(),
        body,
        hashlib.sha256
    ).hexdigest()
    return hmac.compare_digest(expected, signature_header)
```

### Delivery Guarantees

- **At-least-once delivery**: Events are persisted in the queue before delivery. If a node crashes before delivering, the row remains in the queue with status `pending` or `delivering`.
- **Orphan handling**: If the owning node is detected as dead, other nodes in the cluster can claim and deliver orphaned queue rows.
- **No strict ordering**: Events within a batch may arrive out of order. Use event IDs or timestamps for sequencing.

### Expected Response Status

| Status Code | Handling |
|-------------|----------|
| 200, 201, 204 | Success — marked as completed |
| 400–499 | Client error — discarded (not retried) |
| 500–599 | Server error — retried with backoff |
| Network error | Retried with backoff |

## Admin API

### List Bucket Notifications

**URL**: `GET /api/buckets/{id}/notifications`

**Required Permission**: `READ` or `FULL_CONTROL` on the bucket

**Response** (200 OK):

```json
{
  "notifications": [
    {
      "id": "1",
      "bucketId": "1",
      "url": "https://hooks.example.com/webhook",
      "events": "s3:ObjectCreated:*",
      "filterPrefix": "images/",
      "filterSuffix": "",
      "format": "native",
      "enabled": true,
      "retryCount": 10,
      "retryInterval": 1,
      "createdAt": "2025-01-01 12:00:00",
      "lastUpdateAt": "2025-01-01 12:00:00"
    }
  ],
  "total": 1
}
```

### Get Notification

**URL**: `GET /api/notifications/{id}`

**Required Permission**: `READ` or `FULL_CONTROL` on the associated bucket

### Create Notification

**URL**: `POST /api/buckets/{id}/notifications`

**Required Permission**: `WRITE` or `FULL_CONTROL` on the bucket

**Request**:

```json
{
  "url": "https://hooks.example.com/webhook",
  "secret": "your-hmac-secret",
  "events": "s3:ObjectCreated:Put,s3:ObjectRemoved:Delete",
  "filterPrefix": "images/",
  "filterSuffix": ".jpg",
  "format": "native",
  "enabled": true,
  "retryCount": 10,
  "retryInterval": 1
}
```

**Request Fields**:

| Field | Required | Default | Description |
|-------|----------|---------|-------------|
| `url` | Yes | — | Webhook endpoint URL (must be valid HTTP/HTTPS URL) |
| `secret` | No | — | HMAC-SHA256 signing secret |
| `events` | No | `s3:ObjectCreated:*` | Comma-separated event names or `*` for all events |
| `filterPrefix` | No | — | Only fire events for objects with this key prefix |
| `filterSuffix` | No | — | Only fire events for objects with this key suffix |
| `format` | No | `native` | Payload format: `native` or `aws` |
| `enabled` | No | `true` | Enable/disable this notification |
| `retryCount` | No | `10` | Maximum retry attempts |
| `retryInterval` | No | `1` | Initial retry backoff interval in seconds |

**Response** (201 Created): Returns the created notification object.

**Error Responses**:
- 400 Bad Request: Invalid URL or missing required fields
- 403 Forbidden: No `WRITE` permission on the bucket
- 404 Not Found: Bucket does not exist

### Update Notification

**URL**: `PUT /api/notifications/{id}`

**Required Permission**: `WRITE` or `FULL_CONTROL` on the associated bucket

**Request**: All fields are optional — only provided fields are updated.

```json
{
  "url": "https://hooks.example.com/new-webhook",
  "enabled": false
}
```

### Delete Notification

**URL**: `DELETE /api/notifications/{id}`

**Required Permission**: `WRITE` or `FULL_CONTROL` on the associated bucket

### Delete Bucket Notification

**URL**: `DELETE /api/buckets/{bucketId}/notifications/{notificationId}`

### Test Webhook

**URL**: `POST /api/notifications/test`

**Required Permission**: Authenticated

**Request**:

```json
{
  "url": "https://hooks.example.com/webhook",
  "secret": "your-hmac-secret",
  "format": "native"
}
```

**Response** (200 OK):

```json
{
  "success": true,
  "statusCode": 200,
  "body": "OK"
}
```

On failure:

```json
{
  "success": false,
  "statusCode": 0,
  "error": "request failed: dial tcp: connection refused"
}
```

## Event Filtering

Notifications support two types of filters:

### Event Type Filtering

Configure which event types trigger the notification by setting the `events` field. Multiple events are comma-separated:

- `s3:ObjectCreated:*` — All object creation events (default)
- `s3:ObjectCreated:Put` — Only direct PUT uploads
- `s3:ObjectRemoved:Delete` — Only object deletions
- `*` — All events (both creation and deletion)

### Object Key Filtering

Filter by object key using `filterPrefix` and `filterSuffix`:

| Filter | Example | Matches | Does Not Match |
|--------|---------|---------|----------------|
| `filterPrefix=images/` | `images/photo.jpg` | `docs/file.txt` |
| `filterSuffix=.jpg` | `photo.jpg` | `photo.png` |
| Both | `images/photo.jpg` | `docs/photo.jpg` |

## MCP Tools

The MCP server provides six tools for managing bucket notifications:

| Tool | Description |
|------|-------------|
| `storefs_list_bucket_notifications` | List all notification configs for a bucket |
| `storefs_get_notification` | Get details of a single notification |
| `storefs_create_notification` | Create a new notification config |
| `storefs_update_notification` | Update an existing notification config |
| `storefs_delete_notification` | Delete a notification config |
| `storefs_test_webhook` | Send a test event to a webhook URL |

## Database Schema

### bucket_notifications

```sql
CREATE TABLE IF NOT EXISTS bucket_notifications
(
    id             BIGINT       NOT NULL COMMENT 'notification unique id',
    bucket_id      BIGINT       NOT NULL COMMENT 'associated bucket id',
    url            VARCHAR(2048) NOT NULL COMMENT 'Webhook endpoint URL',
    secret         VARCHAR(512) DEFAULT '' COMMENT 'HMAC-SHA256 signing secret',
    events         VARCHAR(512) NOT NULL DEFAULT 's3:ObjectCreated:*' COMMENT 'event types, comma-separated',
    filter_prefix  VARCHAR(1024) DEFAULT '' COMMENT 'object key prefix filter',
    filter_suffix  VARCHAR(1024) DEFAULT '' COMMENT 'object key suffix filter',
    format         VARCHAR(16)  DEFAULT 'native' COMMENT 'payload format: native or aws',
    enabled        BOOLEAN      DEFAULT '1' COMMENT 'enable/disable this notification',
    retry_count    INT          DEFAULT '10' COMMENT 'max retry count',
    retry_interval INT          DEFAULT '1' COMMENT 'initial retry backoff seconds',
    created_at     DATETIME     DEFAULT CURRENT_TIMESTAMP,
    last_update_at DATETIME     DEFAULT CURRENT_TIMESTAMP
) PRIMARY KEY (id)
DISTRIBUTED BY HASH(id) BUCKETS 3;
```

### notification_queue

```sql
CREATE TABLE IF NOT EXISTS notification_queue
(
    id              BIGINT       NOT NULL COMMENT 'Snowflake ID, globally ordered',
    noti_config_id  BIGINT       NOT NULL COMMENT 'FK → bucket_notifications.id',
    url             VARCHAR(2048) NOT NULL COMMENT 'Webhook endpoint (denormalized)',
    payload         TEXT         NOT NULL COMMENT 'pre-built JSON body',
    header_event    VARCHAR(64)  NOT NULL COMMENT 'X-StoreFS-Event header value',
    header_event_id VARCHAR(64)  NOT NULL COMMENT 'X-StoreFS-Event-ID header value',
    signature       VARCHAR(128) DEFAULT '' COMMENT 'X-StoreFS-Signature header value',
    status          VARCHAR(16)  NOT NULL DEFAULT 'pending' COMMENT 'pending|delivering|completed|failed|discarded',
    owner_node      VARCHAR(128) DEFAULT '' COMMENT 'node currently responsible for delivery',
    retry_count     INT          DEFAULT '0' COMMENT 'retry attempts so far',
    max_retries     INT          DEFAULT '10' COMMENT 'max retry count from config',
    next_retry_at   DATETIME     NULL COMMENT 'earliest time allowed for next retry',
    last_error      TEXT         NULL COMMENT 'last failure reason',
    ttl_expires_at  DATETIME     NOT NULL COMMENT 'row expires at this time',
    created_at      DATETIME     DEFAULT CURRENT_TIMESTAMP,
    last_update_at  DATETIME     DEFAULT CURRENT_TIMESTAMP
) PRIMARY KEY (id)
DISTRIBUTED BY HASH(id) BUCKETS 3;
```

## Best Practices

1. **Use a secret** for signature verification to ensure webhook payloads come from StoreFS.
2. **Set appropriate filters** to avoid unnecessary traffic. Use `filterPrefix` and `filterSuffix` to scope events.
3. **Respond quickly** — return a 2xx status as fast as possible. If processing takes time, acknowledge receipt and process asynchronously.
4. **Expect batching** — multiple events may arrive in a single payload within a 1-second window.
5. **Handle duplicates** — webhook delivery is at-least-once. Use `eventId` for idempotency.
6. **Use `aws` format** if integrating with existing AWS S3 event notification handlers.

## Limitations

- Payload format is JSON only (no XML or form-encoded delivery).
- Webhook URL must be HTTP or HTTPS.
- Maximum event batch size is 100 events per payload.
- Queue rows expire after 3 days at most.
- There is no built-in dead letter queue.
