**[查看中文版](auditlog_cn.md)**

# Audit Log Documentation

## Overview

StoreFS provides a comprehensive audit logging system that records all administrative and S3 data operations performed on the cluster. Every API request — whether it's creating a user, uploading an object, or deleting a bucket — is captured with detailed metadata including who performed the action, what resource was affected, when it happened, the client IP, the HTTP status code, and the processing duration.

Audit logs are essential for:
- **Security Compliance**: Track who accessed what and when
- **Operational Troubleshooting**: Correlate requests across the cluster via request IDs
- **Usage Analysis**: Monitor operation patterns and hot resources
- **Billing / Chargeback**: Count operations by user and bucket

### How It Works

```
HTTP Request arrives
       ↓
authMiddleware authenticates user → captures user identity
       ↓
Handler processes the request
       ↓
Audit middleware captures: status code, duration, operation type
       ↓
AuditEntry built and fired to AuditService (async, non-blocking)
       ↓
AuditService dispatch loop → filters → outputs
       ↓
            ┌── DB output: batch insert (100 entries / 1s window)
            ├── Syslog output: JSON to local/remote syslog
            └── File output: JSON lines to rotating file
```

The audit system runs on each StoreFS node independently:

1. **Request Interception**: Every HTTP request (both Admin API and S3 API) is intercepted by the audit middleware.
2. **User Identity Capture**: The `authMiddleware` writes the authenticated user name into the request context before passing to the handler. Unauthenticated requests are recorded as `"anonymous"`.
3. **Operation Detection**: The system detects the operation name from the HTTP method and path — for Admin API operations (e.g., `CreateUser`, `DeleteBucket`) and S3 operations (e.g., `PutObject`, `GetObject`, `ListBuckets`).
4. **Asynchronous Dispatch**: The audit entry is submitted to a buffered channel (capacity 4096) and processed by a background dispatch loop, ensuring the request handler is never blocked.
5. **Output Delivery**: The dispatch loop writes entries to all configured outputs (DB, syslog, file) with automatic batching for DB output.

## Configuration

Audit logging is configured in the YAML configuration file under the `audit` section:

```yaml
audit:
  enabled: true                      # Enable audit logging
  level: info                        # Log level: "info" or "error"
  outputs:                           # Output destinations
    - db                             #   Store in database (audit_log table)
    # - syslog                       #   Send to syslog (uncomment to enable)
    # - file                         #   Write to file (uncomment to enable)
  retention_days: 90                 # DB retention period in days (0 = forever)
  cleanup_interval: 1h               # How often to check for expired partitions
  syslog:
    enabled: false                   # Enable syslog output
    network: ""                      # "" for local syslog, "udp"/"tcp" for remote
    address: ""                      # Remote syslog "host:port", empty for local
    facility: "daemon"               # Syslog facility: "auth", "daemon", "local0".."local7"
    tag: "storefs-audit"            # Syslog tag (default: storefs-audit)
  file:
    enabled: false                   # Enable file output
    path: "/var/log/storefs/audit.json"  # Output file path
    max_size: "500MB"                # Max single file size before rotation
    max_age: "90d"                   # Max age of backup files
    max_backups: 10                  # Max number of backup files to keep
  filters:
    exclude_health_checks: true      # Exclude /health endpoint requests
    min_duration_ms: 0               # Minimum duration in ms to log (0 = log all)
```

### Configuration Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Master switch for audit logging |
| `level` | string | `"info"` | Log level. `"info"` logs all entries; `"error"` logs only 4xx/5xx responses |
| `outputs` | []string | `[]` | Output destinations. Supported: `"db"`, `"syslog"`, `"file"` |
| `retention_days` | int | `90` | Number of days to retain audit logs in DB (0 = never expire) |
| `cleanup_interval` | duration | `"1h"` | Interval for partition cleanup checks |
| `filters.exclude_health_checks` | bool | `false` | If true, `/health` requests are not logged |
| `filters.min_duration_ms` | int | `0` | Only log requests that exceed this duration (0 = log all) |

> **Note**: When `enabled` is `true`, at least one output must be configured, and `level` must be `"info"` or `"error"`.

## Audit Log Fields

Each audit log entry contains the following fields:

| Field | Type | Description |
|-------|------|-------------|
| `id` | BIGINT | Snowflake ID, globally unique and time-sortable |
| `timestamp` | DATETIME | Request received time (UTC) |
| `source_ip` | VARCHAR(45) | Client IP address (extracted from `X-Forwarded-For` or `X-Real-IP` headers, falling back to `RemoteAddr`) |
| `http_method` | VARCHAR(10) | HTTP method (GET, POST, PUT, DELETE, etc.) |
| `path` | VARCHAR(2048) | Request path |
| `query_string` | VARCHAR(2048) | Query string parameters |
| `user_identity` | VARCHAR(256) | Authenticated user name, or `"anonymous"` |
| `status_code` | INT | HTTP response status code |
| `duration_ms` | BIGINT | Request processing time in milliseconds |
| `request_id` | VARCHAR(64) | Unique request ID for distributed tracing |
| `operation_type` | VARCHAR(128) | Operation name (e.g., `CreateUser`, `PutObject`, `ListBuckets`) |
| `error_code` | VARCHAR(64) | Error code if the request failed |
| `bucket_name` | VARCHAR(128) | S3 bucket name (for S3 operations) |
| `object_key` | VARCHAR(1024) | S3 object key (for S3 operations) |
| `resource_type` | VARCHAR(32) | Resource type: `"s3"` or `"admin"` |
| `target_resource_id` | VARCHAR(64) | Target resource ID (for admin operations like user ID, policy ID) |
| `action_details` | TEXT | JSON string with additional action details |

### Request ID

Each request receives a unique 16-byte hex-encoded request ID (32 hex characters) via the `X-Request-ID` header. This ID is included in the audit log and can be used to correlate requests across logs, network traces, and error messages.

## Audited Operations

### Admin API Operations

Every Admin API endpoint is audited. The operation type is detected from the HTTP method and path:

| Operation Type | HTTP Method | Path Pattern |
|----------------|-------------|-------------|
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

### S3 API Operations

All S3 API operations are also audited:

| Operation Type | Description |
|----------------|-------------|
| `ListBuckets` | List all buckets |
| `CreateBucket` | Create a bucket |
| `DeleteBucket` | Delete a bucket |
| `HeadBucket` | Check bucket existence |
| `ListObjects` | List objects in a bucket |
| `ListObjectsV2` | List objects v2 |
| `GetBucketVersioning` | Get bucket versioning configuration |
| `PutBucketVersioning` | Set bucket versioning configuration |
| `GetBucketTagging` | Get bucket tags |
| `PutBucketTagging` | Set bucket tags |
| `DeleteBucketTagging` | Delete bucket tags |
| `GetBucketACL` | Get bucket ACL |
| `PutBucketACL` | Set bucket ACL |
| `ListMultipartUploads` | List in-progress multipart uploads |
| `ListObjectVersions` | List object versions |
| `GetObjectLockConfig` | Get object lock configuration |
| `GetObject` | Retrieve an object |
| `HeadObject` | Get object metadata |
| `PutObject` | Upload an object |
| `CopyObject` | Copy an object |
| `DeleteObject` | Delete an object |
| `DeleteObjects` | Delete multiple objects |
| `GetObjectTagging` | Get object tags |
| `PutObjectTagging` | Set object tags |
| `DeleteObjectTagging` | Delete object tags |
| `GetObjectRetention` | Get object retention settings |
| `InitiateMultipartUpload` | Start a multipart upload |
| `UploadPart` | Upload a part |
| `UploadPartCopy` | Copy a part |
| `ListParts` | List uploaded parts |
| `CompleteMultipartUpload` | Complete a multipart upload |
| `AbortMultipartUpload` | Abort a multipart upload |
| `SelectObjectContent` | Query object content with SQL |
| `RenameObject` | Rename an object |

## Querying Audit Logs

### Via MySQL / Apache Doris SQL

Audit logs are stored in the `audit_log` table. You can query them directly using a MySQL client connected to the Apache Doris database:

```sql
-- Find all operations by a specific user
SELECT * FROM audit_log
WHERE user_identity = 'admin'
ORDER BY timestamp DESC
LIMIT 100;

-- Find all CreateUser operations
SELECT * FROM audit_log
WHERE operation_type = 'CreateUser'
ORDER BY timestamp DESC;

-- Find failed requests
SELECT * FROM audit_log
WHERE status_code >= 400
ORDER BY timestamp DESC;

-- Count operations by type per day
SELECT DATE(timestamp) AS day, operation_type, COUNT(*) AS count
FROM audit_log
GROUP BY day, operation_type
ORDER BY day DESC, count DESC;

-- Find slow requests (top 10 by duration)
SELECT * FROM audit_log
ORDER BY duration_ms DESC
LIMIT 10;

-- Find all operations on a specific bucket
SELECT * FROM audit_log
WHERE bucket_name = 'my-bucket'
ORDER BY timestamp DESC;

-- Trace a specific request across logs
SELECT * FROM audit_log
WHERE request_id = 'abc123...'
ORDER BY timestamp;
```

### Via Admin API

The audit logs can also be queried through the Admin API's audit log endpoints (if available in your version).

### Via Web Admin Console

If audit logging is enabled with the `db` output, you can view and search audit logs directly from the Web Admin Console.

**Steps:**

1. Navigate to the **Audit Logs** page from the sidebar menu.
2. Use the filter bar at the top to narrow down results:
   - **Time Range**: Select a start and end time to filter by request timestamp.
   - **User Identity**: Filter by the authenticated user name who performed the operation.
   - **Operation Type**: Filter by operation name (e.g., `CreateUser`, `PutObject`, `ListBuckets`).
   - **Status Code**: Filter by HTTP response status code.
3. Click **Search** to apply filters, or **Reset** to clear all filters.
4. The results table displays key fields: timestamp, user identity, operation type, path, status code, and duration.
5. Click the **Detail** button on any row to view the full audit log entry, including source IP, request ID, bucket name, object key, error code, and action details.

> **Security Note**: Presigned URL parameters (such as `X-Amz-Credential` and `X-Amz-Signature`) are automatically masked as `******` in the displayed query string and path to prevent credential leakage through audit logs.

## Output Types

### Database (DB) Output

The default and most common output. Entries are batched (up to 100 entries or 1-second window, whichever comes first) and bulk-inserted into the `audit_log` table in Apache Doris.

**Features:**
- Daily partition-based storage for efficient querying and cleanup
- Automatic partition creation for future dates
- Partition cleanup based on `retention_days` configuration
- Indexed on `timestamp`, `operation_type`, `user_identity`, `bucket_name`, `status_code`, and `request_id`

**Partition Management:**
- Partitions are created daily via `ALTER TABLE audit_log ADD PARTITION`
- The partition cleanup loop runs at the configured `cleanup_interval` (default 1h)
- Expired partitions are dropped with `ALTER TABLE audit_log DROP PARTITION`, releasing disk space immediately

### Syslog Output

Sends audit log entries as JSON to syslog. On Unix/Linux/macOS/FreeBSD, this uses the native `log/syslog` package and supports local syslog daemon or remote syslog servers over UDP/TCP. On Windows, it writes to the **Windows Event Log** instead.

**Configuration:**
- Local syslog (Unix): leave `network` and `address` empty
- Remote syslog (Unix): set `network` to `"udp"` or `"tcp"` and `address` to `"host:port"`
- Windows: Event Log source is registered automatically using the `tag` value; entries are written to the Application log
- Customize facility and tag as needed

### File Output

Writes audit log entries as newline-delimited JSON to a file on disk.

**Features:**
- Automatic log rotation (by file size and age)
- Configurable max backups count
- JSON lines format (one entry per line)

## Filters

The audit system supports two built-in filters:

1. **Exclude Health Checks**: When `exclude_health_checks` is `true`, requests to `/health` are silently dropped and not logged. This prevents health check polling from cluttering the audit logs.

2. **Minimum Duration**: When `min_duration_ms` is set to a positive value, only requests that take longer than the specified duration are logged. This can be useful for focusing on slow operations when debugging performance issues.

## Performance Considerations

- The audit system uses a buffered channel (capacity 4096) and processes entries asynchronously, so audit logging does not block request handling.
- DB output uses batch inserts (100 entries or 1-second window) to minimize database write overhead.
- For high-throughput clusters, consider:
  - Setting `exclude_health_checks: true` to reduce noise
  - Tuning the `retention_days` to manage storage growth
  - Using Syslog or File output to offload storage from the database

## Security

- Audit logs capture the authenticated user identity for every request. Unauthenticated requests are recorded as `"anonymous"`.
- Audit logs are stored in the database with the same security as other system metadata. Access to the `audit_log` table should be restricted to administrators.
- The `X-Request-ID` header enables tracing requests across the system for security investigations.