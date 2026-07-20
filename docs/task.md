**[查看中文版](task_cn.md)**

# Task System

StoreFS provides a task system for long-running administrative operations. This document covers the concepts, supported task types, and how to use them.

## Overview

Tasks are used to perform background maintenance on the cluster. They are created via the Admin API and executed on a target node. Each task has a lifecycle:

```
Pending → Running → Completed / Failed / Cancelled
```

### Task Lifecycle

1. **Pending** — task is created and assigned to a node
2. **Running** — task is being executed on the assigned node
3. **Completed** — task finished successfully
4. **Failed** — task encountered an error and stopped
5. **Cancelled** — task was manually cancelled by a user

### Concurrency Rules

| Task Type | Concurrency Mode | Constraint |
|---|---|---|
| `repair` | Per-node singleton | One repair per node at a time; different nodes can repair simultaneously |
| `replacedisk` | Per-node singleton | One replacedisk per node at a time; different nodes can run simultaneously |

Both `repair` and `replacedisk` tasks require **super_admin** role to create.

---

## Task: `repair`

Scans and repairs corrupted or missing file fragments on a given node.

### When to Use

- A disk has degraded sectors and some fragments failed CRC verification
- A node is being re-added to the cluster after an outage
- You suspect silent data corruption and want to verify fragment integrity

### Parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `node` | string | Yes | Node name to repair |
| `disk` | string | No | Absolute path to a specific disk. If omitted, all disks on the node are repaired. |

### What It Does

1. **Counts** the records in `fragment_md` and `part_fragments` tables for the target node/disk
2. **Verifies** each fragment locally: checks file existence, size, and CRC32
3. **Repairs** invalid fragments:
   - **Replica policy (`replicas`)**: reads a valid replica from another node, copies it locally, and verifies CRC
   - **EC policy (`erasure`/`ec`)**: collects available shards from other nodes, reconstructs the missing shard via Reed-Solomon, and writes it locally
   - **Orphaned fragments**: if the object metadata no longer exists, the fragment is deleted (both file and database record)

### MCP Tool

```
storefs_create_repair_task(node: "node1", disk: "/data/disk1")
```

### REST API

```
POST /api/tasks
Content-Type: application/json

{
  "type": "repair",
  "params": {
    "node": "node1",
    "disk": "/data/disk1"
  }
}
```

### Result Fields

| Field | Description |
|---|---|
| `totalFiles` | Total records scanned |
| `totalBytes` | Total bytes across all scanned fragments |
| `validFiles` | Fragments that passed CRC verification |
| `repairedFiles` | Successfully repaired fragments |
| `failedFiles` | Fragments that could not be repaired |
| `skippedFiles` | Fragments skipped due to cancellation |
| `orphanedFiles` | Orphaned fragments cleaned up |
| `successPercent` | (valid + repaired) / total × 100 |

---

## Task: `replacedisk`

Migrates all data from an old disk to a new disk on the same node. Commonly used when a physical disk is failing or being replaced.

### When to Use

- A disk is reporting I/O errors and needs replacement
- You are upgrading to larger capacity disks
- You need to change the disk mount point

### Parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `oldDiskPath` | string | Yes | Absolute path to the old disk (must match the `path` in the node's disk configuration) |
| `newDiskPath` | string | Yes | Absolute path to the new disk |
| `nodeName` | string | Yes | Node name to execute this task on |

### What It Does

1. **Counts** fragments on the old disk path from both `fragment_md` and `part_fragments` tables
2. **Creates** the new disk directory if it doesn't exist
3. **For each fragment**:
   - Computes the relative path from the old disk
   - Constructs the new path under the new disk
   - Creates intermediate directories
   - Copies the file to the new location
   - Updates the database record with the new path and `diskDir`
   - On database update failure, the copied file is rolled back (deleted)

### MCP Tool

```
storefs_create_replacedisk_task(
  oldDiskPath: "/data/old_disk",
  newDiskPath: "/data/new_disk",
  nodeName: "node1"
)
```

### REST API

```
POST /api/tasks
Content-Type: application/json

{
  "type": "replacedisk",
  "params": {
    "oldDiskPath": "/data/old_disk",
    "newDiskPath": "/data/new_disk",
    "nodeName": "node1"
  }
}
```

### Result Fields

| Field | Description |
|---|---|
| `totalFiles` | Total records to migrate |
| `totalBytes` | Total bytes across all migrated fragments |
| `successFiles` | Successfully migrated fragments |
| `failedFiles` | Fragments that failed to migrate |
| `skippedFiles` | Fragments skipped due to cancellation |
| `successPercent` | success / total × 100 |

---

## Managing Tasks

### List Tasks

```bash
# MCP
storefs_list_tasks(page: 1, pageSize: 20, status: "running")

# REST
GET /api/tasks?page=1&pageSize=20&status=running
```

### Get Task Details

```bash
# MCP
storefs_get_task(taskId: "123456")

# REST
GET /api/tasks/123456
```

### Cancel a Task

```bash
# MCP
storefs_cancel_task(taskId: "123456")

# REST
POST /api/tasks/123456/cancel
```

### Delete a Task

```bash
# MCP
storefs_delete_task(taskId: "123456")

# REST
DELETE /api/tasks/123456
```

### Clean Up Old Tasks

```bash
# MCP
storefs_cleanup_tasks(days: 30)

# REST
POST /api/tasks/cleanup
Content-Type: application/json

{ "days": 30 }
```

---

## Permissions

| Action | Required Role |
|---|---|
| List / View tasks | Any authenticated user |
| Create `repair` task | `super_admin` |
| Create `replacedisk` task | `super_admin` |
| Create other task types | Varies by type |
| Cancel / Delete tasks | Varies by type |
| Clean up old tasks | `super_admin` |