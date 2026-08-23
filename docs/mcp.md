**[查看中文版](mcp_cn.md)**

# StoreFS MCP Server — User Guide

The **StoreFS MCP Server** implements the [Model Context Protocol (MCP)](https://modelcontextprotocol.io) to let AI assistants — primarily Claude Code — manage your StoreFS distributed S3 storage cluster through natural language instead of crafting raw API calls.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    AI Assistant (Claude Code)                │
│  "list all users"  "create a bucket"  "upload a file"      │
└───────────┬─────────────────────────────────────────────────┘
            │  MCP stdio transport (JSON-RPC)
            ▼
┌───────────────────────┐       ┌──────────────────────────────┐
│    storefs-mcp        │       │  MCP Tools:                  │
│    (Node.js)          │       │  - system (health, login)    │
│    mcp/src/index.ts   │──────►│  - user & group management   │
│                       │       │  - storage policies          │
│                       │       │  - bucket & object mgmt      │
│                       │       │  - multipart upload          │
│                       │       │  - S3 data operations        │
│                       │       │  - task management           │
└───────────┬───────────┘       └──────────────────────────────┘
            │
            ├──────── HTTP ────────► Admin REST API (port 7946)
            │                        └─ /api/auth/*, /api/users/*
            │                          /api/buckets/*, /api/policies/*
            │                          /api/groups/*, /api/node/*
            │                          /api/tasks/*
            │                          /metrics
            │
            └──────── HTTP ────────► S3 REST API (port 8901)
                                     └─ ListBuckets, HeadObject, GetObject,
                                        PutObject, DeleteObject, CopyObject
```

The MCP server is a lightweight Node.js process that acts as a bridge:

- It speaks **MCP (JSON-RPC over stdio)** to the AI assistant.
- It translates each tool call into the appropriate **Admin REST API** or **S3 REST API** request to the StoreFS cluster.
- Authentication is handled via Bearer token (Admin API) or AWS Signature V4 (S3 API).

---

## Quick Start

### Prerequisites

- **Node.js >= 18**
- **StoreFS v0.3.7 or above**
- A running StoreFS cluster (one or more nodes)
- Admin credentials for the cluster

### 1. Install

```bash
tar -xzf mcp.tar.gz
cd mcp
npm install
```

### 2. Configure

Configure the MCP server in your project's `.claude/settings.json` (or global `~/.claude/settings.json`):

```json
{
  "mcpServers": {
    "storefs": {
      "command": "node",
      "args": ["/absolute/path/to/mcp/index.js"],
      "env": {
        "STOREFS_ADMIN_URL": "http://127.0.0.1:7946",
        "STOREFS_S3_URL": "http://127.0.0.1:8901"
      }
    }
  }
}
```

Environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `STOREFS_ADMIN_URL` | `http://127.0.0.1:7946` | Admin REST API base URL (gossip+HTTP multiplexed port) |
| `STOREFS_S3_URL` | `http://127.0.0.1:8901` | S3 REST API base URL |
| `LANG` | — | Optional. Set to `zh_CN`/`zh` to show tool descriptions in Chinese |

### 3. Start Claude Code

```bash
claude
```

### 4. Login

Tell the AI assistant:

```
storefs_login(username="admin", password="your-password")
```

You should see a success message with your role and group information.

> **Note:** Login state persists for the session. Use `storefs_logout` to clear it, or `storefs_whoami` to check the current user.

### 5. Login with MFA (Personal Access Token)

If the user account has **MFA (TOTP)** enabled, `storefs_login` with password will return an MFA-required notice. Since MCP is a headless CLI (cannot prompt for a TOTP code), use a **Personal Access Token (PAT)** instead:

1. **Create a PAT** in the web console (Profile → Personal Access Tokens), or via the Admin API:
   ```bash
   curl -X POST http://127.0.0.1:7946/api/auth/pat \
     -H "Authorization: Bearer <token>" \
     -H "Content-Type: application/json" \
     -d '{"description": "MCP access", "expiresIn": "30d"}'
   ```
   Choose an expiry (7d/30d/90d/1y/2y/5y/forever). The token is **only shown once at creation** — save it immediately.

2. **Log in with the PAT**:
   ```
   storefs_login_with_pat(pat="stfs_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
   ```

3. **Or set the `STOREFS_PAT` environment variable** in the MCP server config so the PAT is loaded automatically on startup:
   ```json
   {
     "mcp": {
       "servers": {
         "storefs": {
           "command": "node",
           "args": ["/path/to/mcp/index.js"],
           "env": {
             "STOREFS_PAT": "stfs_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
           }
         }
       }
     }
   }
   ```

PATs bypass MFA and work for programmatic access. The `storefs_login_with_pat` tool is also listed in the System Tools table below.

---

## Feature Overview

The MCP server exposes **70+ tools** organized into eleven groups:

### 1. System Tools

| Tool | Description |
|------|-------------|
| `storefs_health` | Check cluster reachability and health |
| `storefs_login` | Authenticate with username/password |
| `storefs_login_with_pat` | Authenticate with a Personal Access Token (bypasses MFA) |
| `storefs_logout` | Logout and revoke JWT server-side, then clear local session |
| `storefs_whoami` | Show currently logged-in user details |
| `storefs_cluster_status` | List all nodes, their disks, usage, and taint status |
| `storefs_node_metrics` | Fetch Prometheus metrics (with optional filter) |
| `storefs_change_password` | Change current user's password |
| `storefs_list_node_status` | List all node taint statuses |
| `storefs_update_node_status` | Update a node's taint status (super_admin only) |

### 2. User & Group Management Tools

| Tool | Description |
|------|-------------|
| `storefs_list_users` | List users with pagination and group filter |
| `storefs_get_user` | Get detailed user info |
| `storefs_create_user` | Create user (auto-generates AccessKey/SecretKey) |
| `storefs_update_user` | Update user role, name, or group |
| `storefs_delete_user` | Delete a user |
| `storefs_get_access_keys` | Show a user's AccessKey/SecretKey |
| `storefs_rotate_access_keys` | Regenerate keys (old ones become invalid) |
| `storefs_reset_password` | Reset a user's password |
| `storefs_disable_user_mfa` | Disable a user's MFA (turn off 2FA and delete backup codes) |
| `storefs_list_groups` | List all groups |
| `storefs_get_group` | Get group details |
| `storefs_create_group` | Create a new group |
| `storefs_update_group` | Update group name or default policy |
| `storefs_delete_group` | Delete a group (super_admin only) |

### 3. Storage Policy Management Tools

| Tool | Description |
|------|-------------|
| `storefs_list_policies` | List all storage policies (replicas/EC) |
| `storefs_get_policy` | Get policy details |
| `storefs_create_policy` | Create a storage policy |
| `storefs_update_policy` | Modify an existing policy |
| `storefs_delete_policy` | Delete a policy |

Two policy types are supported:
- **Replicas**: Simple replication — configure the number of copies (e.g., 3 replicas).
- **Erasure Code (EC)**: Divide data into data shards and parity shards for better storage efficiency (e.g., 4+2).

### 4. Bucket Management Tools

| Tool | Description |
|------|-------------|
| `storefs_list_buckets` | List buckets with filtering (by user/group) |
| `storefs_get_bucket` | Get bucket configuration details |
| `storefs_create_bucket` | Create a new bucket (name must be globally unique) |
| `storefs_update_bucket` | Modify bucket configuration |
| `storefs_delete_bucket` | Delete an empty bucket |
| `storefs_generate_presigned_url` | Generate a presigned URL for upload/download |
| `storefs_get_bucket_acl` | Get bucket ACL (access control list) |
| `storefs_put_bucket_acl` | Set bucket ACL — control read/write access for users |

Bucket ACL grantee types:
- **canonical_user**: A specific user (identified by user ID)
- **all_users**: All users (including anonymous) — `http://acs.amazonaws.com/groups/global/AllUsers`
- **authenticated_users**: Any authenticated user — `http://acs.amazonaws.com/groups/global/AuthenticatedUsers`

ACL permissions: `FULL_CONTROL` | `WRITE` | `READ` | `READ_ACP` | `WRITE_ACP`

> **Note:** Setting ACL replaces all existing entries atomically. Owner always retains `FULL_CONTROL` automatically.

Bucket-level features you can configure:
- **Versioning**: Track object versions (`Enabled` / `Suspended`)
- **Object Lock**: Prevent object deletion (GOVERNANCE / COMPLIANCE mode)
- **Public Read**: Allow unauthenticated GET requests
- **Server-Side Encryption**: SSE for data at rest
- **Storage Policy**: Assign a replication or EC policy

### 5. Object & Multipart Management Tools

| Tool | Description |
|------|-------------|
| `storefs_list_objects` | List objects in a bucket |
| `storefs_get_object_info` | Get object fragment distribution across nodes |
| `storefs_get_object_versions` | List all versions of an object |
| `storefs_delete_object` | Delete an object (supports version-ID) |
| `storefs_list_multipart_uploads` | List incomplete multipart uploads |
| `storefs_get_multipart_upload` | Get multipart upload details with part list |
| `storefs_complete_multipart_upload` | Complete a multipart upload |
| `storefs_abort_multipart_upload` | Cancel a multipart upload |
| `storefs_get_part_fragment_info` | Get fragment health for a specific part |

### 6. S3 Data Operation Tools

| Tool | Description |
|------|-------------|
| `storefs_s3_list_buckets` | List buckets via S3 API (shows user-accessible buckets) |
| `storefs_s3_head_object` | Get object metadata (size, ETag, Content-Type) |
| `storefs_s3_get_object` | View small text object content (≤1MB by default) |
| `storefs_s3_put_object` | Upload short text content (supports `tags` parameter) |
| `storefs_s3_delete_object` | Delete an object via S3 API |
| `storefs_s3_copy_object` | Copy an object between buckets |
| `storefs_s3_upload_file` | Upload a local file to S3 (supports `tags` parameter) |
| `storefs_s3_download_file` | Download an S3 object to a local file |
| `storefs_s3_get_object_tagging` | Get object tags (supports `versionId`) |
| `storefs_s3_put_object_tagging` | Replace all object tags (supports `versionId`) |
| `storefs_s3_delete_object_tagging` | Delete all object tags (supports `versionId`) |
| `storefs_s3_get_bucket_tagging` | Get bucket tags |
| `storefs_s3_put_bucket_tagging` | Replace all bucket tags |
| `storefs_s3_delete_bucket_tagging` | Delete all bucket tags |
| `storefs_s3_select` | Query object content with SQL via S3 Select API (CSV/JSON) |

> **Note:** S3 data operations use the current user's AccessKey/SecretKey, which are automatically retrieved after login. If the user has no keys, use `storefs_rotate_access_keys` to generate them.

### 7. Task Management Tools

| Tool | Description |
|------|-------------|
| `storefs_list_task_types` | List all available task types |
| `storefs_list_tasks` | List all tasks with pagination and status filter |
| `storefs_get_task` | Get task details |
| `storefs_create_task` | Create a task (generic) |
| `storefs_create_repair_task` | Create a repair task — repair damaged/missing fragments on a node (super_admin only) |
| `storefs_create_replacedisk_task` | Create a replace-disk task — migrate data from old disk to new disk (super_admin only) |
| `storefs_cancel_task` | Cancel a running task |
| `storefs_delete_task` | Delete a task (running tasks are cancelled first) |
| `storefs_cleanup_tasks` | Clean up old completed/failed tasks (super_admin only) |

Tasks are asynchronous background operations running on a specific node. Use `storefs_list_tasks` to monitor progress and `storefs_get_task` for details.

For detailed documentation on the task system, repair, and replace-disk operations, see the [Task System Documentation](task.md).

### 8. Notification Management Tools

| Tool | Description |
|------|-------------|
| `storefs_list_bucket_notifications` | List all notification configs for a bucket |
| `storefs_get_notification` | Get details of a single notification |
| `storefs_create_notification` | Create a new webhook notification for a bucket |
| `storefs_update_notification` | Update an existing notification config |
| `storefs_delete_notification` | Delete a notification config |
| `storefs_test_webhook` | Send a test event to a webhook URL to verify connectivity |

Notification config fields:

| Field | Description |
|-------|-------------|
| `url` | Webhook endpoint URL (required) |
| `secret` | Optional HMAC-SHA256 signing secret for payload verification |
| `events` | Comma-separated event types (default: `s3:ObjectCreated:*`) |
| `filterPrefix` | Only fire for objects with this key prefix |
| `filterSuffix` | Only fire for objects with this key suffix |
| `format` | Payload format: `native` (default) or `aws` |
| `enabled` | Enable/disable the notification |

For detailed documentation, see the [Notification System Documentation](notification.md).

### 9. KMS Management Tools

| Tool | Description |
|------|-------------|
| `storefs_get_kms_config` | Get current KMS configuration (endpoint, timeout, health check interval) |
| `storefs_get_kms_config_by_id` | Get a specific KMS config by ID |
| `storefs_list_kms_configs` | List all KMS configs |
| `storefs_create_kms_config` | Create a new KMS config |
| `storefs_delete_kms_config` | Delete a KMS config (remove all key references first) |
| `storefs_update_kms_config` | Update KMS connection parameters (endpoint, credentials, TLS certs, timeout) |
| `storefs_test_kms_config` | Test a KMS connection without saving (validates endpoint, TLS, and credentials) |
| `storefs_check_kms_health` | Check KMS service health status (online/offline) |
| `storefs_list_kms_keys` | List all KMS keys with pagination, supports `groupId` filter for super_admin and `showRetired` to include retired keys |
| `storefs_create_kms_key` | Create a new KMS key (alias, optional description, optional `groupId`, optional `kmsConfigId` to select KMS service) |
| `storefs_rotate_kms_key` | Rotate a KMS key (creates a new key, retires the old one, re-wraps all bucket keys) |
| `storefs_delete_kms_key` | Delete a KMS key (only if not in use by any bucket) |

KMS provides **SSE-KMS** (Server-Side Encryption with KMS-managed keys) as an additional encryption mode alongside SSE-S3 and SSE-C. When SSE-KMS is enabled on a bucket, each object's data encryption key (DEK) is wrapped by a KMS Customer Master Key (CMK) stored in an external KMS (currently KMIP 1.2+, with future cloud provider support).

**Access Control**: KMS keys are organized into two levels:
- **Global keys** (groupId=0): Managed by `super_admin`, visible to all users
- **Group keys** (groupId>0): Managed by `group_admin` for their own group, visible to group members
- **KMS configuration** is only accessible to `super_admin` (contains sensitive connection credentials)

Key rotation creates a new key, re-wraps all bucket DEKs from the old CMK to the new one, and marks the old key as `retired`. Retired keys are hidden from the default listing — use `showRetired=true` (super_admin only) to view them. Expired retired keys are automatically cleaned up after 90 days.

For detailed documentation, see the [KMS Configuration](admin-api.md#9-kms-management) section in the Admin API documentation.

### 10. Catalog Search Tools

| Tool | Description |
|------|-------------|
| `storefs_catalog_stats` | Get catalog search status (enabled/disabled, OpenSearch info) |
| `storefs_catalog_enable` | Enable catalog search engine (super_admin only) |
| `storefs_catalog_disable` | Disable catalog search engine (super_admin only) |
| `storefs_catalog_config` | Update catalog configuration — OpenSearch (endpoint, credentials, index names) and Embedding (base URL, model, dimensions, API key) |
| `storefs_catalog_get_config` | Get current catalog configuration (OpenSearch + Embedding settings) |
| `storefs_catalog_search_objects` | Search objects with full-text query and filters (object_type, content_type, tags, meta, prefix, bucket_ids, size range, time range) |
| `storefs_catalog_search` | SQL-like search — e.g., `SELECT * WHERE tag='key:value' AND size>=1000` |
| `storefs_catalog_vector_search` | Vector similarity search — by vector array or text (auto-embeds text), with optional bucket_ids and object_type filter |
| `storefs_catalog_get_object_metadata` | Get object metadata (user_meta, http_meta) by bucket_id and object_name |
| `storefs_catalog_test_connection` | Test OpenSearch connectivity using saved config (returns cluster name, health status, version) |
| `storefs_catalog_test_embedding` | Test Embedding API connectivity using saved config (returns connection status) |

The catalog enables full-text and semantic search across all objects. It requires:
- **OpenSearch** cluster (with k-NN plugin for vector search)
- **CatalogBuilder** (standalone process) to scan, extract content, generate embeddings, and index objects
- Optional **Embedding API** (OpenAI-compatible) for hybrid search

For detailed documentation, see the [Catalog Search Documentation](catalog.md).

---

## Suggested Prompts

Below are task-oriented prompts organized by scenario. You can use them directly with the AI assistant.

### Cluster Operations

```
Check the cluster health
Show me the cluster status — all nodes and disk usage
Fetch node metrics, filter by "cpu"
List all node taint statuses
Update node "node3" taint status to "taint"
Restore node "node3" taint status to "active"
```

### User Administration

```
List all users in the system
Show details for user ID 1
Create a new user named "alice" with role "user" in group 1
Reset password for user 2 to "NewP@ss123", require password change on next login
Rotate access keys for user 3
Delete the user with ID 4
```

### Group Management

```
List all groups
Create a group called "engineering" with default policy ID 1
Update group 2 to use policy ID 3 as default
```

### Storage Policies

```
List all storage policies
Create a replicas policy named "triple-replication" with 3 replicas
Create an EC policy named "ec-4-2" with 4 data shards and 2 parity shards
```

### Bucket Operations

```
List all buckets
Create a bucket named "my-data" with policy ID 1, owned by user ID 2
Create a versioned bucket "important-files" with versioning enabled and public read
Show details for bucket 5
Generate a presigned PUT URL for bucket 1, object "upload.txt"
```

### Object Management

```
List objects in bucket "my-data"
Show fragment distribution for object "report.pdf" in bucket "my-bucket"
List all versions of object "config.json" in bucket 3
Delete object "old-backup.zip" from bucket 2
```

### Multipart Uploads

```
List incomplete multipart uploads in bucket 1
Show details for upload "abc-123" in bucket 1, object "large-file.iso"
Complete the multipart upload "abc-123" for bucket 1, object "large-file.iso"
Abort the multipart upload "xyz-789"
Check fragment health for upload "abc-123", part 3
```

### S3 Data Operations

```
List my S3 buckets
Head the object "readme.md" in bucket "docs"
View the content of "config.json" in bucket "my-data"
Upload the text "hello world" as "greeting.txt" in bucket "my-data"
Upload the local file "/tmp/report.pdf" as "reports/report.pdf" in bucket "docs"
Download the object "backup.tar.gz" from bucket "archive" to "/tmp/backup.tar.gz"
Copy object "source.txt" from bucket "a" to "backups/source.txt" in bucket "b"
Query the CSV object "data.csv" in bucket "my-data" with SQL: "SELECT * FROM S3Object WHERE age > 30"
Query the JSON object "data.json" in bucket "my-data" with SQL: "SELECT name, age FROM S3Object"
```

### Tagging

```
Get tags on object "report.pdf" in bucket "my-data"
Set tags on object "report.pdf" in bucket "my-data" to [{"key":"department","value":"engineering"}]
Delete all tags on object "report.pdf" in bucket "my-data"
Get tags on bucket "my-data"
Set tags on bucket "my-data" to [{"key":"project","value":"storefs"}]
Delete all tags on bucket "my-data"
Upload a file with tags: upload "/tmp/doc.pdf" as "doc.pdf" in bucket "docs" with tags "project=storefs&department=engineering"
```

### Notifications

```
List notification configs for bucket 1
Show details for notification 5
Create a webhook notification for bucket 1: URL https://hooks.example.com/notify, events s3:ObjectCreated:Put
Update notification 5 to disable it
Delete notification 5
Test webhook URL https://hooks.example.com/test with native format
```

### Catalog Search

```
Check the catalog search status
Search for objects containing "report"
Search for PDF files with tag "project:alpha" and size > 1MB
Search SQL: SELECT * WHERE object_type='pdf' AND size>=1000000
Search for objects in bucket 1 with prefix "documents/"
Vector search: find objects similar to "quarterly financial report"
Get the catalog configuration
Enable the catalog search engine
Test the OpenSearch connection
Test the Embedding API connection
```

### Maintenance & Troubleshooting

```
Check which nodes are offline
Find incomplete multipart uploads that should be cleaned up
Show me buckets that are using the most disk space
List all users in the "admin" group
List all tasks and their statuses
Get details for task 42
Create a replacedisk task for node "node1", old disk "/data/disk1", new disk "/data/disk2"
Cancel running task 15
```

---

## Internationalization (i18n)

Language determination follows this priority:

1. **`LANG` environment variable** — If set (e.g., `zh_CN.UTF-8`), it overrides all other detection. Tool descriptions also use this variable.
2. **Auto-detection from input** — Only applies when `LANG` is not configured:
   - If **all** string parameters contain **Chinese characters** (CJK ideographs exclusive to Chinese), responses are in **Chinese**.
   - If any parameter contains **Japanese** (Hiragana/Katakana) or **Korean** (Hangul) characters, responses default to **English**.
   - Otherwise, responses are in **English**.

Tool descriptions follow the system's `LANG` environment variable.

---

## Security Notes

- **Authentication state** is held in memory and scoped to the Claude Code session. It is not persisted to disk.
- **S3 operations** use the current user's AccessKey/SecretKey, fetched automatically from the Admin API after login.
- **Login credentials** are never logged or stored beyond the tool call parameters.
- **Object lock** with COMPLIANCE mode cannot be removed once set — even the root admin cannot delete locked objects before the retention period expires.
- **Presigned URLs** are valid for 5 minutes by default.

---

## Appendix: Admin API vs S3 API

The MCP server uses two separate API surfaces:

| | Admin REST API (port 7946) | S3 REST API (port 8901) |
|---|---|---|
| **Authentication** | Bearer token (JWT) | AWS Signature V4 |
| **Scope** | Cluster management — users, buckets, policies, object metadata | S3-compatible data operations |
| **When used** | All system, user, policy, bucket, and object management tools | `storefs_s3_*` data operation tools |

The Admin API is used by the management tools; the S3 API is reserved for data plane operations (upload, download, copy, delete, head) and is fully S3-compatible.
