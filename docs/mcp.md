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
└───────────┬───────────┘       └──────────────────────────────┘
            │
            ├──────── HTTP ────────► Admin REST API (port 7946)
            │                        └─ /api/auth/*, /api/users/*
            │                          /api/buckets/*, /api/policies/*
            │                          /api/groups/*, /api/node/*
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

---

## Feature Overview

The MCP server exposes **40+ tools** organized into six groups:

### 1. System Tools

| Tool | Description |
|------|-------------|
| `storefs_health` | Check cluster reachability and health |
| `storefs_login` | Authenticate with username/password |
| `storefs_logout` | Clear current authentication session |
| `storefs_whoami` | Show currently logged-in user details |
| `storefs_cluster_status` | List all nodes, their disks, and usage |
| `storefs_node_metrics` | Fetch Prometheus metrics (with optional filter) |
| `storefs_change_password` | Change current user's password |

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
| `storefs_s3_put_object` | Upload short text content |
| `storefs_s3_delete_object` | Delete an object via S3 API |
| `storefs_s3_copy_object` | Copy an object between buckets |
| `storefs_s3_upload_file` | Upload a local file to S3 |
| `storefs_s3_download_file` | Download an S3 object to a local file |

> **Note:** S3 data operations use the current user's AccessKey/SecretKey, which are automatically retrieved after login. If the user has no keys, use `storefs_rotate_access_keys` to generate them.

---

## Suggested Prompts

Below are task-oriented prompts organized by scenario. You can use them directly with the AI assistant.

### Cluster Operations

```
Check the cluster health
Show me the cluster status — all nodes and disk usage
Fetch node metrics, filter by "cpu"
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
```

### Maintenance & Troubleshooting

```
Check which nodes are offline
Find incomplete multipart uploads that should be cleaned up
Show me buckets that are using the most disk space
List all users in the "admin" group
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
