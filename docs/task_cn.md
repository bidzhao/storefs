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
| `repair` | 按节点单例 | 每个节点同一时间只能运行一个 repair 任务；不同节点可以同时修复 |
| `replacedisk` | 按节点单例 | 每个节点同一时间只能运行一个 replacedisk 任务；不同节点可以同时进行 |

创建 `repair` 和 `replacedisk` 任务都需要 **super_admin** 角色权限。

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
| 创建其他类型任务 | 视类型而定 |
| 取消/删除任务 | 视类型而定 |
| 清理旧任务 | `super_admin` |