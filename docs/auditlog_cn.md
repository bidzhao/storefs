**[English](auditlog.md)**

# 审计日志文档

## 概览

StoreFS 提供完整的审计日志系统，记录集群上所有管理操作和 S3 数据操作。每个 API 请求——无论是创建用户、上传对象还是删除桶——都会被捕获，包含详细的元数据，包括操作者、操作的资源、时间、客户端 IP、HTTP 状态码和处理耗时。

审计日志对以下场景至关重要：
- **安全合规**：追踪谁在什么时间访问了什么
- **运维故障排查**：通过请求 ID 关联集群中的请求
- **使用分析**：监控操作模式及热点资源
- **计费 / 成本分摊**：按用户和桶统计操作次数

### 工作原理

```
HTTP 请求到达
       ↓
authMiddleware 认证用户 → 捕获用户身份
       ↓
处理器处理请求
       ↓
审计中间件捕获：状态码、耗时、操作类型
       ↓
构建 AuditEntry 并异步提交到 AuditService（非阻塞）
       ↓
AuditService 分发循环 → 过滤 → 输出
       ↓
            ┌── DB 输出：批量插入（100 条/1 秒窗口）
            ├── Syslog 输出：JSON 到本地/远程 syslog
            └── 文件输出：JSON 行到轮转文件
```

审计系统在每个 StoreFS 节点上独立运行：

1. **请求拦截**：每个 HTTP 请求（包括 Admin API 和 S3 API）都会被审计中间件拦截。
2. **用户身份捕获**：`authMiddleware` 在将请求传递给处理器之前，将已认证的用户名写入请求上下文。未认证的请求记录为 `"anonymous"`。
3. **操作检测**：系统根据 HTTP 方法和路径检测操作名称——包括 Admin API 操作（如 `CreateUser`、`DeleteBucket`）和 S3 操作（如 `PutObject`、`GetObject`、`ListBuckets`）。
4. **异步分发**：审计条目提交到缓冲通道（容量 4096），由后台分发循环处理，确保不会阻塞请求处理器。
5. **输出投递**：分发循环将条目写入所有配置的输出（数据库、syslog、文件），数据库输出支持自动批处理。

## 配置

审计日志在 YAML 配置文件的 `audit` 部分配置：

```yaml
audit:
  enabled: true                      # 启用审计日志
  level: info                        # 日志级别："info" 或 "error"
  outputs:                           # 输出目标
    - db                             #   存储到数据库（audit_log 表）
    # - syslog                       #   发送到 syslog（取消注释启用）
    # - file                         #   写入文件（取消注释启用）
  retention_days: 90                 # 数据库保留天数（0 = 永不过期）
  cleanup_interval: 1h               # 检查过期分区的间隔
  syslog:
    enabled: false                   # 启用 syslog 输出
    network: ""                      # "" 本地 syslog, "udp"/"tcp" 远程
    address: ""                      # 远程 syslog "host:port"，空为本地
    facility: "daemon"               # Syslog 设施："auth", "daemon", "local0".."local7"
    tag: "storefs-audit"            # Syslog 标签（默认：storefs-audit）
  file:
    enabled: false                   # 启用文件输出
    path: "/var/log/storefs/audit.json"  # 输出文件路径
    max_size: "500MB"                # 单个文件最大大小，超过后轮转
    max_age: "90d"                   # 备份文件最大保留时长
    max_backups: 10                  # 最多保留备份文件数
  filters:
    exclude_health_checks: true      # 排除 /health 端点请求
    min_duration_ms: 0               # 最小记录耗时（毫秒），0 = 记录全部
```

### 配置字段说明

| 字段 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `enabled` | bool | `false` | 审计日志总开关 |
| `level` | string | `"info"` | 日志级别。`"info"` 记录所有条目；`"error"` 仅记录 4xx/5xx 响应 |
| `outputs` | []string | `[]` | 输出目标。支持：`"db"`、`"syslog"`、`"file"` |
| `retention_days` | int | `90` | 审计日志在数据库中的保留天数（0 = 永不过期） |
| `cleanup_interval` | duration | `"1h"` | 分区清理检查间隔 |
| `filters.exclude_health_checks` | bool | `false` | 为 true 时不记录 `/health` 请求 |
| `filters.min_duration_ms` | int | `0` | 仅记录超过此耗时的请求（0 = 记录全部） |

> **注意**：当 `enabled` 为 `true` 时，必须配置至少一个输出，且 `level` 必须为 `"info"` 或 `"error"`。

## 审计日志字段

每条审计日志条目包含以下字段：

| 字段 | 类型 | 描述 |
|------|------|------|
| `id` | BIGINT | Snowflake ID，全局唯一且可按时间排序 |
| `timestamp` | DATETIME | 请求接收时间（UTC） |
| `source_ip` | VARCHAR(45) | 客户端 IP 地址（从 `X-Forwarded-For` 或 `X-Real-IP` 头提取，回退到 `RemoteAddr`） |
| `http_method` | VARCHAR(10) | HTTP 方法（GET、POST、PUT、DELETE 等） |
| `path` | VARCHAR(2048) | 请求路径 |
| `query_string` | VARCHAR(2048) | 查询字符串参数 |
| `user_identity` | VARCHAR(256) | 已认证的用户名，或 `"anonymous"` |
| `status_code` | INT | HTTP 响应状态码 |
| `duration_ms` | BIGINT | 请求处理耗时（毫秒） |
| `request_id` | VARCHAR(64) | 唯一请求 ID，用于分布式追踪 |
| `operation_type` | VARCHAR(128) | 操作名称（如 `CreateUser`、`PutObject`、`ListBuckets`） |
| `error_code` | VARCHAR(64) | 请求失败时的错误码 |
| `bucket_name` | VARCHAR(128) | S3 桶名称（S3 操作时） |
| `object_key` | VARCHAR(1024) | S3 对象键（S3 操作时） |
| `resource_type` | VARCHAR(32) | 资源类型：`"s3"` 或 `"admin"` |
| `target_resource_id` | VARCHAR(64) | 目标资源 ID（管理操作时，如用户 ID、策略 ID） |
| `action_details` | TEXT | JSON 字符串，包含额外操作详情 |

### 请求 ID

每个请求通过 `X-Request-ID` 头部获得一个唯一的 16 字节十六进制编码请求 ID（32 个十六进制字符）。该 ID 包含在审计日志中，可用于关联请求、网络追踪和错误消息。

## 被审计的操作

### Admin API 操作

每个 Admin API 端点都会被审计。操作类型根据 HTTP 方法和路径检测：

| 操作类型 | HTTP 方法 | 路径模式 |
|----------|-----------|---------|
| `Login` | POST | `/auth/login` |
| `ChangePassword` | PUT | `/auth/change-password` |
| `GetUsers` | GET | `/users` |
| `CreateUser` | POST | `/users` |
| `GetUser` | GET | `/users/{id}` |
| `UpdateUser` | PUT | `/users/{id}` |
| `DeleteUser` | DELETE | `/users/{id}` |
| `GetPolicies` | GET | `/policies` |
| `CreatePolicy` | POST | `/policies` |
| `GetPolicy` | GET | `/policies/{id}` |
| `UpdatePolicy` | PUT | `/policies/{id}` |
| `DeletePolicy` | DELETE | `/policies/{id}` |
| `GetGroups` | GET | `/groups` |
| `CreateGroup` | POST | `/groups` |
| `GetGroup` | GET | `/groups/{id}` |
| `UpdateGroup` | PUT | `/groups/{id}` |
| `DeleteGroup` | DELETE | `/groups/{id}` |
| `GetBuckets` | GET | `/buckets` |
| `CreateBucket` | POST | `/buckets` |
| `GetBucket` | GET | `/buckets/{id}` |
| `UpdateBucket` | PUT | `/buckets/{id}` |
| `DeleteBucket` | DELETE | `/buckets/{id}` |
| `GetBucketACL` | GET | `/buckets/{id}/acl` |
| `PutBucketACL` | PUT | `/buckets/{id}/acl` |
| `GetBucketNotifications` | GET | `/buckets/{id}/notifications` |
| `CreateBucketNotification` | POST | `/buckets/{id}/notifications` |
| `GetBucketNotification` | GET | `/buckets/{id}/notifications/{nid}` |
| `UpdateBucketNotification` | PUT | `/buckets/{id}/notifications/{nid}` |
| `DeleteBucketNotification` | DELETE | `/buckets/{id}/notifications/{nid}` |
| `GetObjectsByBucketID` | GET | `/buckets/{id}/objects` |
| `DeleteObject` | DELETE | `/buckets/{id}/objects` |
| `GetObjectVersions` | GET | `/buckets/{id}/objects/versions` |
| `ListMultipartUploads` | GET | `/buckets/{id}/multipart-uploads` |
| `GetMultipartUpload` | GET | `/multipart-uploads/{id}` |
| `AbortMultipartUpload` | DELETE | `/multipart-uploads/{id}` |
| `CompleteMultipartUpload` | POST | `/multipart-uploads/{id}/complete` |
| `GetAccessKeys` | GET | `/users/{id}/access-keys` |
| `CreateAccessKey` | POST | `/users/{id}/access-keys` |
| `UpdateAccessKey` | PUT | `/users/{id}/access-keys` |
| `DeleteAccessKey` | DELETE | `/users/{id}/access-keys` |
| `ResetPassword` | PUT | `/users/{id}/reset-password` |
| `DisableUserMFA` | PUT | `/users/{id}/disable-mfa` |
| `MFAGetStatus` | GET | `/auth/mfa/status` |
| `MFAEnable` | POST | `/auth/mfa/enable` |
| `MFAVerifySetup` | POST | `/auth/mfa/verify` |
| `MFADisable` | POST | `/auth/mfa/disable` |
| `MFARecreateBackupCodes` | POST | `/auth/mfa/backup-codes` |
| `MFAVerifyLogin` | POST | `/auth/mfa/verify-login` |
| `ListPATs` | GET | `/auth/pat` |
| `CreatePAT` | POST | `/auth/pat` |
| `DeletePAT` | DELETE | `/auth/pat/{id}` |
| `GetKMSConfig` | GET | `/kms/config` |
| `UpdateKMSConfig` | PUT | `/kms/config` |
| `TestKMSConfig` | POST | `/kms/config/test` |
| `CheckKMSHealth` | GET | `/kms/config/health` |
| `ListKMSConfigs` | GET | `/kms/configs` |
| `CreateKMSConfig` | POST | `/kms/configs` |
| `GetKMSConfigByID` | GET | `/kms/configs/{id}` |
| `UpdateKMSConfigByID` | PUT | `/kms/configs/{id}` |
| `DeleteKMSConfig` | DELETE | `/kms/configs/{id}` |
| `ListKMSKeys` | GET | `/kms/keys` |
| `CreateKMSKey` | POST | `/kms/keys` |
| `GetKMSKey` | GET | `/kms/keys/{id}` |
| `UpdateKMSKey` | PUT | `/kms/keys/{id}` |
| `DeleteKMSKey` | DELETE | `/kms/keys/{id}` |
| `RotateKMSKey` | POST | `/kms/keys/{id}/rotate` |
| `GetNodeStatus` | GET | `/node/status` |
| `GetTasks` | GET | `/tasks` |
| `CreateTask` | POST | `/tasks` |
| `GetTask` | GET | `/tasks/{id}` |
| `CancelTask` | PUT | `/tasks/{id}` |
| `HealthCheck` | GET | `/health` |
| `GetObjectInfo` | GET | `/object/info` |
| `SetObjectTags` | POST | `/object/tags` |

### S3 API 操作

所有 S3 API 操作也会被审计：

| 操作类型 | 描述 |
|----------|------|
| `ListBuckets` | 列出所有桶 |
| `CreateBucket` | 创建桶 |
| `DeleteBucket` | 删除桶 |
| `HeadBucket` | 检查桶是否存在 |
| `ListObjects` | 列出桶中的对象 |
| `ListObjectsV2` | 列出对象 V2 |
| `GetBucketVersioning` | 获取桶版本控制配置 |
| `PutBucketVersioning` | 设置桶版本控制配置 |
| `GetBucketTagging` | 获取桶标签 |
| `PutBucketTagging` | 设置桶标签 |
| `DeleteBucketTagging` | 删除桶标签 |
| `GetBucketACL` | 获取桶 ACL |
| `PutBucketACL` | 设置桶 ACL |
| `ListMultipartUploads` | 列出进行中的分块上传 |
| `ListObjectVersions` | 列出对象版本 |
| `GetObjectLockConfig` | 获取对象锁定配置 |
| `GetObject` | 获取对象 |
| `HeadObject` | 获取对象元数据 |
| `PutObject` | 上传对象 |
| `CopyObject` | 复制对象 |
| `DeleteObject` | 删除对象 |
| `DeleteObjects` | 批量删除对象 |
| `GetObjectTagging` | 获取对象标签 |
| `PutObjectTagging` | 设置对象标签 |
| `DeleteObjectTagging` | 删除对象标签 |
| `GetObjectRetention` | 获取对象保留设置 |
| `InitiateMultipartUpload` | 初始化分块上传 |
| `UploadPart` | 上传分块 |
| `UploadPartCopy` | 复制分块 |
| `ListParts` | 列出已上传的分块 |
| `CompleteMultipartUpload` | 完成分块上传 |
| `AbortMultipartUpload` | 取消分块上传 |
| `SelectObjectContent` | 使用 SQL 查询对象内容 |
| `RenameObject` | 重命名对象 |

## 查询审计日志

### 通过 MySQL / Apache Doris SQL

审计日志存储在 `audit_log` 表中。您可以直接使用 MySQL 客户端连接到 Apache Doris 数据库进行查询：

```sql
-- 查找特定用户的所有操作
SELECT * FROM audit_log
WHERE user_identity = 'admin'
ORDER BY timestamp DESC
LIMIT 100;

-- 查找所有 CreateUser 操作
SELECT * FROM audit_log
WHERE operation_type = 'CreateUser'
ORDER BY timestamp DESC;

-- 查找失败的请求
SELECT * FROM audit_log
WHERE status_code >= 400
ORDER BY timestamp DESC;

-- 按天统计各操作类型的数量
SELECT DATE(timestamp) AS day, operation_type, COUNT(*) AS count
FROM audit_log
GROUP BY day, operation_type
ORDER BY day DESC, count DESC;

-- 查找最慢的请求（按耗时排序前 10）
SELECT * FROM audit_log
ORDER BY duration_ms DESC
LIMIT 10;

-- 查找特定桶上的所有操作
SELECT * FROM audit_log
WHERE bucket_name = 'my-bucket'
ORDER BY timestamp DESC;

-- 追踪特定请求 ID 在日志中的流转
SELECT * FROM audit_log
WHERE request_id = 'abc123...'
ORDER BY timestamp;
```

### 通过 Admin API

审计日志也可以通过 Admin API 的审计日志端点查询（如果您的版本支持）。

### 通过 Web 管理控制台

如果审计日志已启用并配置了 `db` 输出，您可以直接在 Web 管理控制台中查看和搜索审计日志。

**操作步骤：**

1. 从左侧导航菜单进入 **审计日志** 页面。
2. 使用顶部的筛选栏缩小结果范围：
   - **时间范围**：选择开始和结束时间，按请求时间戳筛选。
   - **用户身份**：按执行操作的用户名筛选。
   - **操作类型**：按操作名称筛选（如 `CreateUser`、`PutObject`、`ListBuckets`）。
   - **状态码**：按 HTTP 响应状态码筛选。
3. 点击 **搜索** 应用筛选条件，或点击 **重置** 清除所有筛选条件。
4. 结果表格显示关键字段：时间戳、用户身份、操作类型、路径、状态码和耗时。
5. 点击任意行的 **详情** 按钮查看完整的审计日志条目，包括来源 IP、请求 ID、桶名称、对象键、错误码和操作详情。

> **安全提示**：预签名 URL 参数（如 `X-Amz-Credential` 和 `X-Amz-Signature`）在显示的查询字符串和路径中会自动被遮蔽为 `******`，以防止凭据通过审计日志泄露。

## 输出类型

### 数据库（DB）输出

默认且最常用的输出。条目会批量处理（最多 100 条或 1 秒窗口，以先到者为准）并批量插入到 Apache Doris 的 `audit_log` 表中。

**特性：**
- 基于日期的分区存储，便于高效查询和清理
- 自动创建未来日期的分区
- 根据 `retention_days` 配置进行分区清理
- 在 `timestamp`、`operation_type`、`user_identity`、`bucket_name`、`status_code` 和 `request_id` 上建有索引

**分区管理：**
- 通过 `ALTER TABLE audit_log ADD PARTITION` 每日创建分区
- 分区清理循环按配置的 `cleanup_interval`（默认 1 小时）运行
- 过期分区通过 `ALTER TABLE audit_log DROP PARTITION` 删除，立即释放磁盘空间

### Syslog 输出

将审计日志条目以 JSON 格式发送到 syslog。在 Unix/Linux/macOS/FreeBSD 上使用原生 `log/syslog` 包，支持本地 syslog 守护进程或通过 UDP/TCP 的远程 syslog 服务器。在 Windows 上则写入 **Windows Event Log**。

**配置：**
- 本地 syslog（Unix）：留空 `network` 和 `address`
- 远程 syslog（Unix）：设置 `network` 为 `"udp"` 或 `"tcp"`，`address` 为 `"host:port"`
- Windows：使用 `tag` 值自动注册 Event Log 源，条目写入应用程序日志
- 根据需要自定义 facility 和 tag

### 文件输出

将审计日志条目以换行符分隔的 JSON 格式写入磁盘文件。

**特性：**
- 自动日志轮转（按文件大小和时长）
- 可配置最大备份数
- JSON 行格式（每行一条记录）

## 过滤器

审计系统支持两个内置过滤器：

1. **排除健康检查**：当 `exclude_health_checks` 为 `true` 时，对 `/health` 的请求会被静默丢弃，不记录日志。这可以防止健康检查轮询干扰审计日志。

2. **最小耗时**：当 `min_duration_ms` 设置为正值时，仅记录超过指定耗时的请求。这在排查性能问题时，可以聚焦于慢操作。

## 性能考量

- 审计系统使用缓冲通道（容量 4096）异步处理条目，因此审计日志记录不会阻塞请求处理。
- 数据库输出使用批量插入（100 条或 1 秒窗口）以最小化数据库写入开销。
- 对于高吞吐量集群，建议：
  - 设置 `exclude_health_checks: true` 以减少噪音
  - 调整 `retention_days` 以控制存储增长
  - 使用 Syslog 或文件输出以减轻数据库存储压力

## 安全性

- 审计日志为每个请求捕获已认证的用户身份。未认证的请求记录为 `"anonymous"`。
- 审计日志存储在数据库中，与其他系统元数据具有相同的安全级别。对 `audit_log` 表的访问应限制为管理员。
- `X-Request-ID` 头部使得在安全调查中能够跨系统追踪请求。