**[English](notification.md)**

# 桶通知（Webhook）文档

## 概览

StoreFS 提供基于 Webhook 的桶通知系统，当桶中的对象被创建或删除时，会发送实时事件通知到外部服务。这使外部系统无需轮询即可响应存储事件。

### 工作原理

```
用户上传对象
       ↓
StoreFS 触发事件 → 事件批处理（1秒窗口 / 100个事件）
       ↓
构建负载 → 写入队列表
       ↓
工作线程取出 → POST 到配置的 Webhook URL
       ↓
                   ┌── 2xx → 标记完成
Webhook 响应 ──────┼── 4xx → 丢弃（客户端错误）
                   └── 5xx/网络错误 → 指数退避重试
```

通知引擎在每个 StoreFS 节点上运行：

1. **事件捕获**：S3 操作（PutObject、CopyObject、DeleteObject、CompleteMultipartUpload）触发事件。
2. **批处理**：事件按通知配置进行批处理（1 秒窗口或最多 100 个事件，以先到者为准）。
3. **队列**：批处理后的事件序列化到持久化队列表（`notification_queue`）。
4. **投递**：后台工作线程每 200ms 轮询队列并通过 HTTP POST 投递 Webhook。
5. **重试**：失败的投递以指数退避策略重试（1秒 → 5秒 → 30秒 → 5分钟 → 30分钟）。
6. **TTL**：队列行在 3 天后过期并自动丢弃。

### 支持的事件类型

| 事件类型 | S3 事件名称 | 说明 |
|---------|------------|------|
| `s3:ObjectCreated:*` | 所有创建事件 | 匹配所有对象创建操作 |
| `s3:ObjectCreated:Put` | PutObject | 直接通过 PUT 上传对象 |
| `s3:ObjectCreated:Copy` | CopyObject | 对象复制操作 |
| `s3:ObjectCreated:CompleteMultipartUpload` | MultipartUpload | 分块上传完成 |
| `s3:ObjectCreated:Rename` | Rename（StoreFS 扩展） | 对象重命名操作 |
| `s3:ObjectRemoved:Delete` | Delete | 对象删除 |
| `s3:ObjectRemoved:DeleteMarkerCreated` | DeleteMarker | 创建删除标记（版本化桶） |

## 负载格式

StoreFS 支持两种 Webhook 负载格式：**native**（简化的 StoreFS 格式）和 **aws**（AWS S3 兼容格式）。

### Native 格式（默认）

单事件：

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

批处理（1 秒窗口内的多个事件）：

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

### AWS S3 兼容格式

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

## Webhook 投递

### 请求头

| 请求头 | 说明 |
|--------|------|
| `Content-Type` | `application/json` |
| `User-Agent` | `StoreFS/1.0` |
| `X-StoreFS-Event` | 事件名称（如 `s3:ObjectCreated:Put`） |
| `X-StoreFS-Event-ID` | 唯一事件标识符 |
| `X-StoreFS-Signature` | HMAC-SHA256 签名（仅在配置了 secret 时） |

### 签名验证

如果配置了 Webhook 密钥，每次投递都会包含 `X-StoreFS-Signature` 头：

```
X-StoreFS-Signature: sha256=abc123def456...
```

接收方验证签名（Python 示例）：

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

### 投递保证

- **至少一次投递**：事件在投递前持久化到队列。如果节点在投递前崩溃，行将保留在队列中状态为 `pending` 或 `delivering`。
- **孤儿处理**：如果拥有节点被检测为宕机，集群中的其他节点可以认领并投递孤立的队列行。
- **无严格顺序**：批处理内的事件可能乱序到达。使用事件 ID 或时间戳进行排序。

### 期望响应状态

| 状态码 | 处理方式 |
|--------|---------|
| 200, 201, 204 | 成功 — 标记为完成 |
| 400–499 | 客户端错误 — 丢弃（不重试） |
| 500–599 | 服务器错误 — 指数退避重试 |
| 网络错误 | 指数退避重试 |

## 管理 API

### 列出桶通知

**URL**：`GET /api/buckets/{id}/notifications`

**所需权限**：对桶有 `READ` 或 `FULL_CONTROL` 权限

**响应**（200 OK）：

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

### 获取通知

**URL**：`GET /api/notifications/{id}`

**所需权限**：对关联桶有 `READ` 或 `FULL_CONTROL` 权限

### 创建通知

**URL**：`POST /api/buckets/{id}/notifications`

**所需权限**：对桶有 `WRITE` 或 `FULL_CONTROL` 权限

**请求**：

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

**请求字段**：

| 字段 | 必填 | 默认值 | 说明 |
|------|------|--------|------|
| `url` | 是 | — | Webhook 端点 URL（必须为有效的 HTTP/HTTPS URL） |
| `secret` | 否 | — | HMAC-SHA256 签名密钥 |
| `events` | 否 | `s3:ObjectCreated:*` | 逗号分隔的事件名称，或 `*` 表示所有事件 |
| `filterPrefix` | 否 | — | 仅触发对象键以此前缀开头的事件 |
| `filterSuffix` | 否 | — | 仅触发对象键以此后缀结尾的事件 |
| `format` | 否 | `native` | 负载格式：`native` 或 `aws` |
| `enabled` | 否 | `true` | 启用/禁用此通知 |
| `retryCount` | 否 | `10` | 最大重试次数 |
| `retryInterval` | 否 | `1` | 初始重试间隔（秒） |

**响应**（201 Created）：返回创建的通知对象。

### 更新通知

**URL**：`PUT /api/notifications/{id}`

**所需权限**：对关联桶有 `WRITE` 或 `FULL_CONTROL` 权限

**请求**：所有字段可选 — 仅更新提供的字段。

### 删除通知

**URL**：`DELETE /api/notifications/{id}` 或 `DELETE /api/buckets/{bucketId}/notifications/{notificationId}`

### 测试 Webhook

**URL**：`POST /api/notifications/test`

**请求**：

```json
{
  "url": "https://hooks.example.com/webhook",
  "secret": "your-hmac-secret",
  "format": "native"
}
```

**响应**（200 OK）：

```json
{
  "success": true,
  "statusCode": 200,
  "body": "OK"
}
```

## 事件过滤

通知支持两种过滤方式：

### 事件类型过滤

通过设置 `events` 字段配置哪些事件类型触发通知。多个事件用逗号分隔：

- `s3:ObjectCreated:*` — 所有对象创建事件（默认）
- `s3:ObjectCreated:Put` — 仅直接 PUT 上传
- `s3:ObjectRemoved:Delete` — 仅对象删除
- `*` — 所有事件（创建和删除）

### 对象键过滤

使用 `filterPrefix` 和 `filterSuffix` 按对象键过滤：

| 过滤条件 | 示例 | 匹配 | 不匹配 |
|---------|------|------|--------|
| `filterPrefix=images/` | `images/photo.jpg` | `docs/file.txt` |
| `filterSuffix=.jpg` | `photo.jpg` | `photo.png` |
| 两者组合 | `images/photo.jpg` | `docs/photo.jpg` |

## MCP 工具

MCP 服务器提供六个桶通知管理工具：

| 工具 | 说明 |
|------|------|
| `storefs_list_bucket_notifications` | 列出桶的所有通知配置 |
| `storefs_get_notification` | 获取单个通知详情 |
| `storefs_create_notification` | 创建新通知配置 |
| `storefs_update_notification` | 更新现有通知配置 |
| `storefs_delete_notification` | 删除通知配置 |
| `storefs_test_webhook` | 发送测试事件到 Webhook URL |

## 最佳实践

1. **使用密钥**进行签名验证，确保 Webhook 负载来自 StoreFS。
2. **设置适当的过滤条件**以避免不必要的流量。使用 `filterPrefix` 和 `filterSuffix` 限定事件范围。
3. **快速响应**——尽快返回 2xx 状态码。如果需要耗时处理，先确认收到再异步处理。
4. **注意批处理**——多个事件可能在 1 秒窗口内以单个负载到达。
5. **处理重复**——Webhook 投递是至少一次的。使用 `eventId` 进行幂等性处理。
6. **使用 `aws` 格式**与现有 AWS S3 事件通知处理器集成。

## 限制

- 负载格式仅支持 JSON（不支持 XML 或表单编码投递）。
- Webhook URL 必须是 HTTP 或 HTTPS。
- 每个负载最大批处理 100 个事件。
- 队列行最多 3 天后过期。
- 没有内置的死信队列。
