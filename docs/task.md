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
| `repair` | Cluster singleton | Runs exclusively with the other maintenance tasks |
| `replacedisk` | Cluster singleton | Runs exclusively with the other maintenance tasks |
| `rebalance` | Cluster singleton | Runs exclusively with the other maintenance tasks (leader only) |
| `decommission` | Cluster singleton | Runs exclusively with the other maintenance tasks |

All four maintenance task types (`repair`, `replacedisk`, `rebalance`, `decommission`) share a **single cluster-wide maintenance mutex** — at most one of them can be pending/running at any time, across all nodes. Creating one while another is pending/running is rejected.

All four task types require **super_admin** role to create.

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

## Task: `rebalance`

Rebalances storage usage across the cluster so that no disk is significantly over- or under-utilized. Runs **on the raft leader** only, and is mutually exclusive with `repair`, `replacedisk`, and `decommission`.

### When to Use

- A newly added node/disk is mostly empty while existing disks are full
- Disk usage has become skewed after decommission or replacement
- You want to proactively keep disks balanced

### Parameters

None. The plan is computed automatically from the current cluster balance.

### What It Does

1. **Collects** disk usage from all online nodes (per-disk).
2. **Computes** the balance; if the max deviation is within 5%, it exits immediately (`already_balanced_within_5pct`).
3. **Plans** migrations (source disks over budget → target disks under budget), respecting anti-affinity.
4. **Migrates** fragments with a worker pool (raw copy + row move + source delete; missing source files are rebuilt from sibling shards/replicas).
5. **Exits** when the deviation drops back within 2% or the queue is exhausted; stops early if the topology changes.

### MCP Tool

```
storefs_create_rebalance_task()
```

### REST API

```
POST /api/tasks
Content-Type: application/json

{
  "type": "rebalance",
  "params": {}
}
```

### Result Fields

| Field | Description |
|---|---|
| `totalPlanned` | Number of planned migrations |
| `migratedFiles` | Successfully migrated fragments |
| `migratedBytes` | Total bytes migrated |
| `failedFiles` | Fragments that failed to migrate |
| `skippedFiles` | Fragments skipped (target tainted, etc.) |
| `imbalanceBefore` / `imbalanceAfter` | Max deviation before/after (×100 %) |
| `topologyChanged` | Whether the task stopped due to a topology change |
| `exitReason` | `balanced` / `migrated` / `topology_changed` / `cancelled` |

---

## Task: `decommission`

Migrates **all** data off a node so it can be removed from the cluster. Mutually exclusive with `repair`, `replacedisk`, and `rebalance`.

### When to Use

- Decommissioning a server permanently
- Consolidating storage onto fewer nodes

### Parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `nodeName` | string | Yes | The node to decommission (its fragments are migrated off) |
| `forceRestore` | bool | No | When the source node is **offline**, set `true` to rebuild its fragments from sibling shards/replicas (slower than online migration). An offline source with `forceRestore=false` is rejected with `409`. |

### What It Does

1. **Validates** the source node is online (or `forceRestore` is set) and that enough eligible nodes remain to satisfy all storage policies.
2. **Marks** the node `decommission` (blocks new writes) at task start.
3. **Migrates** `completed` fragments first, then `pending`/`failure` fragments (last, with a 30-minute timeout).
4. **Verifies** the source is empty, waits for gossip propagation, re-scans, then atomically marks the node `removed` and the task `completed`.

### MCP Tool

```
storefs_create_decommission_task(
  nodeName: "node3",
  forceRestore: false
)
```

### REST API

```
POST /api/tasks
Content-Type: application/json

{
  "type": "decommission",
  "params": {
    "nodeName": "node3",
    "forceRestore": false
  }
}
```

### Result Fields

| Field | Description |
|---|---|
| `phase` | `completed` / `pending_timeout` / `migrated` / `cancelled` / `blocked` |
| `totalFragments` | Total fragments on the source node |
| `migratedFiles` / `migratedBytes` | Successfully migrated fragments / bytes |
| `failedFiles` | Fragments that failed to migrate |
| `skippedFiles` | Fragments skipped (soft-deleted buckets, etc.) |
| `pendingTotal` / `pendingMigrated` / `pendingUnmoved` | Pending-class fragment counts |
| `nodeEmpty` | Whether the source is fully drained |
| `exitReason` | `completed` / `pending_timeout` / `queue_exhausted` / `cancelled` |

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

### Force-Cancel a Task (super_admin)

A normal cancel is rejected when the execution node is `Unknown`/`Offline`. Force-cancel bypasses that liveness check (still respecting the task state machine — `completed`/`failed` are never overwritten).

```bash
# MCP
storefs_force_cancel_task(taskId: "123456")

# REST
POST /api/tasks/123456/force-cancel
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
| Create `rebalance` task | `super_admin` |
| Create `decommission` task | `super_admin` |
| Cancel task | Any authenticated user (liveness-checked) |
| Force-cancel task | `super_admin` |
| Delete task | Any authenticated user |
| Clean up old tasks | `super_admin` |
| Backfill target_node | `super_admin` |