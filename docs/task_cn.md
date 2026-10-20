**[English](task.md)**

# 任务系统

StoreFS 提供了任务系统来执行后台管理操作。本文档介绍任务的概念、支持的任务类型以及如何使用它们。

## 概述

任务用于在集群上执行后台维护操作。任务通过 Admin API 创建，在目标节点上执行。每个任务都有完整的生命周期：

```
Pending → Running → Completed / Failed / Cancelled
```

### 任务生命周期

1. **Pending（待执行）** — 任务已创建并分配到目标节点
2. **Running（运行中）** — 任务正在目标节点上执行
3. **Completed（已完成）** — 任务成功完成
4. **Failed（失败）** — 任务遇到错误终止
5. **Cancelled（已取消）** — 用户手动取消了任务

### 并发规则

| 任务类型 | 并发模式 | 限制 |
|---|---|---|
| `repair` | 集群单例 | 与其它维护任务互斥执行 |
| `replacedisk` | 集群单例 | 与其它维护任务互斥执行 |
| `rebalance` | 集群单例 | 与其它维护任务互斥执行（仅 leader 节点） |
| `decommission` | 集群单例 | 与其它维护任务互斥执行 |

四种维护任务类型（`repair`、`replacedisk`、`rebalance`、`decommission`）共用**一个集群级维护互斥组**——任意时刻最多只有一个处于 pending/running，跨节点同样互斥。创建时若已有其它维护任务 pending/running，会被拒绝。

创建这四种任务都需要 **super_admin** 角色权限。

---

## 任务：`repair`（修复）

扫描并修复指定节点上损坏或丢失的文件片段。

### 适用场景

- 磁盘出现坏道，部分片段 CRC 校验失败
- 节点宕机后重新加入集群
- 怀疑存在静默数据损坏，需验证片段完整性

### 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `node` | string | 是 | 要修复的节点名称 |
| `disk` | string | 否 | 指定磁盘的绝对路径。不指定则修复节点上所有磁盘 |

### 执行流程

1. **计数** — 统计目标节点/磁盘在 `fragment_md` 和 `part_fragments` 表中的记录数
2. **验证** — 对每个片段进行本地校验：检查文件是否存在、大小是否匹配、CRC32 是否一致
3. **修复** 无效片段：
   - **副本策略（replicas）**：从其他节点读取有效副本，拷贝到本地并验证 CRC
   - **纠删码策略（erasure/ec）**：从其他节点收集可用分片，通过 Reed-Solomon 重建缺失分片并写入本地
   - **孤立片段**：如果对象元数据已不存在，则删除该片段（文件和数据库记录均删除）

### MCP 工具

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

### 结果字段

| 字段 | 说明 |
|---|---|
| `totalFiles` | 扫描的记录总数 |
| `totalBytes` | 所有扫描片段的字节总数 |
| `validFiles` | CRC 校验通过的片段数 |
| `repairedFiles` | 成功修复的片段数 |
| `failedFiles` | 无法修复的片段数 |
| `skippedFiles` | 因取消而跳过的片段数 |
| `orphanedFiles` | 已清理的孤立片段数 |
| `successPercent` | (valid + repaired) / total × 100 |

---

## 任务：`replacedisk`（替换磁盘）

将旧磁盘上的所有数据迁移到同一节点的新磁盘上。通常在物理磁盘故障或需要更换时使用。

### 适用场景

- 磁盘报告 I/O 错误，需要更换
- 升级到更大容量的磁盘
- 需要更改磁盘挂载点

### 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `oldDiskPath` | string | 是 | 旧磁盘的绝对路径（必须与节点配置中的 `path` 一致） |
| `newDiskPath` | string | 是 | 新磁盘的绝对路径 |
| `nodeName` | string | 是 | 执行此任务的节点名称 |

### 执行流程

1. **计数** — 统计旧磁盘路径在 `fragment_md` 和 `part_fragments` 表中的记录数
2. **创建** 新磁盘目录（如果不存在）
3. **对每个片段**：
   - 计算相对于旧磁盘的路径
   - 构建新磁盘下的新路径
   - 创建中间目录
   - 将文件拷贝到新位置
   - 更新数据库记录中的路径和 `diskDir`
   - 如果数据库更新失败，回滚（删除已拷贝的文件）

### MCP 工具

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

### 结果字段

| 字段 | 说明 |
|---|---|
| `totalFiles` | 待迁移的记录总数 |
| `totalBytes` | 所有迁移片段的字节总数 |
| `successFiles` | 成功迁移的片段数 |
| `failedFiles` | 迁移失败的片段数 |
| `skippedFiles` | 因取消而跳过的片段数 |
| `successPercent` | success / total × 100 |

---

## 任务：`rebalance`（再平衡）

在集群范围内重新平衡存储占用，使各磁盘不会显著过载或空置。**仅在 raft leader 节点运行**，与 `repair`、`replacedisk`、`decommission` 互斥。

### 适用场景

- 新增节点/磁盘大部分为空，而现有磁盘已满
- decommission 或替换磁盘后磁盘占用失衡
- 希望主动保持磁盘均衡

### 参数

无。规划根据当前集群平衡度自动计算。

### 执行过程

1. **采集**所有在线节点的分盘用量。
2. **计算**平衡度；若最大偏差在 5% 以内，立即退出（`already_balanced_within_5pct`）。
3. **规划**迁移（超预算的源磁盘 → 低于预算的目标磁盘），遵循反亲和。
4. **迁移** fragment（worker 池：raw 拷贝 + 迁行 + 删源；源文件缺失时从兄弟 shard/replica 重建）。
5. **退出**条件：偏差回到 2% 以内或队列耗尽；拓扑变化时提前停止。

### MCP 工具

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

### 结果字段

| 字段 | 说明 |
|---|---|
| `totalPlanned` | 规划的迁移数 |
| `migratedFiles` | 成功迁移的 fragment 数 |
| `migratedBytes` | 迁移的总字节数 |
| `failedFiles` | 迁移失败的 fragment 数 |
| `skippedFiles` | 跳过的 fragment 数（目标被 taint 等） |
| `imbalanceBefore` / `imbalanceAfter` | 迁移前后最大偏差（×100 %） |
| `topologyChanged` | 是否因拓扑变化而停止 |
| `exitReason` | `balanced` / `migrated` / `topology_changed` / `cancelled` |

---

## 任务：`decommission`（节点下线）

将节点上的**全部**数据迁出，使其可从集群移除。与 `repair`、`replacedisk`、`rebalance` 互斥。

### 适用场景

- 永久下线一台服务器
- 将存储整合到更少的节点

### 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `nodeName` | string | 是 | 要下线的节点（其 fragment 被迁出） |
| `forceRestore` | bool | 否 | 当源节点**离线**时设为 `true`：从兄弟 shard/replica 重建其 fragment（比在线迁移慢）。离线源节点且 `forceRestore=false` 会被 `409` 拒绝。 |

### 执行过程

1. **校验**源节点在线（或已设 `forceRestore`），且剩余可承载节点数满足全部存储策略的冗余要求。
2. **标记**节点为 `decommission`（任务开始即阻断写入）。
3. **迁移** `completed` fragment 优先，随后是 `pending`/`failure` fragment（最后处理，30 分钟超时）。
4. **校验**源节点已清空、等待 gossip 传播、二次扫描后，原子地将节点置为 `removed` 且任务置为 `completed`。

### MCP 工具

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

### 结果字段

| 字段 | 说明 |
|---|---|
| `phase` | `completed` / `pending_timeout` / `migrated` / `cancelled` / `blocked` |
| `totalFragments` | 源节点上的 fragment 总数 |
| `migratedFiles` / `migratedBytes` | 成功迁移的 fragment 数 / 字节数 |
| `failedFiles` | 迁移失败的 fragment 数 |
| `skippedFiles` | 跳过的 fragment 数（软删桶等） |
| `pendingTotal` / `pendingMigrated` / `pendingUnmoved` | pending 类 fragment 计数 |
| `nodeEmpty` | 源节点是否已完全清空 |
| `exitReason` | `completed` / `pending_timeout` / `queue_exhausted` / `cancelled` |

---

## 任务管理

### 列出任务

```bash
# MCP
storefs_list_tasks(page: 1, pageSize: 20, status: "running")

# REST
GET /api/tasks?page=1&pageSize=20&status=running
```

### 查看任务详情

```bash
# MCP
storefs_get_task(taskId: "123456")

# REST
GET /api/tasks/123456
```

### 取消任务

```bash
# MCP
storefs_cancel_task(taskId: "123456")

# REST
POST /api/tasks/123456/cancel
```

### 强制取消任务（super_admin）

当执行节点状态为 `Unknown`/`Offline` 时普通取消会被拒绝。强制取消绕过该 liveness 检查（仍遵循任务状态机——`completed`/`failed` 不会被覆盖）。

```bash
# MCP
storefs_force_cancel_task(taskId: "123456")

# REST
POST /api/tasks/123456/force-cancel
```

### 删除任务

```bash
# MCP
storefs_delete_task(taskId: "123456")

# REST
DELETE /api/tasks/123456
```

### 清理旧任务

```bash
# MCP
storefs_cleanup_tasks(days: 30)

# REST
POST /api/tasks/cleanup
Content-Type: application/json

{ "days": 30 }
```

---

## 权限

| 操作 | 所需角色 |
|---|---|
| 列出/查看任务 | 任意已认证用户 |
| 创建 `repair` 任务 | `super_admin` |
| 创建 `replacedisk` 任务 | `super_admin` |
| 创建 `rebalance` 任务 | `super_admin` |
| 创建 `decommission` 任务 | `super_admin` |
| 取消任务 | 任意已认证用户（受 liveness 检查） |
| 强制取消任务 | `super_admin` |
| 删除任务 | 任意已认证用户 |
| 清理旧任务 | `super_admin` |
| 回填 target_node | `super_admin` |