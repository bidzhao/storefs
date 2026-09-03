**[English](mcp.md)**

# StoreFS MCP 服务器 — 用户指南

**StoreFS MCP 服务器**实现了 [模型上下文协议（MCP）](https://modelcontextprotocol.io)，让 AI 助手（主要是 Claude Code）通过自然语言管理您的 StoreFS 分布式 S3 存储集群，无需手动构造 API 请求。

---

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                    AI 助手 (Claude Code)                     │
│  "列出所有用户"  "创建一个桶"  "上传文件"                   │
└───────────┬─────────────────────────────────────────────────┘
            │  MCP stdio 传输 (JSON-RPC)
            ▼
┌───────────────────────┐       ┌──────────────────────────────┐
│    storefs-mcp        │       │  MCP 工具：                  │
│    (Node.js)          │       │  - 系统（健康检查、登录）    │
│    mcp/src/index.ts   │──────►│  - 用户和分组管理            │
│                       │       │  - 存储策略                  │
│                       │       │  - 桶和对象管理              │
│                       │       │  - 分块上传管理              │
│                       │       │  - S3 数据操作               │
│                       │       │  - 任务管理                  │
└───────────┬───────────┘       └──────────────────────────────┘
            │
            ├──────── HTTP ────────► Admin REST API (端口 7946)
            │                        └─ /api/auth/*, /api/users/*
            │                          /api/buckets/*, /api/policies/*
            │                          /api/groups/*, /api/node/*
            │                          /api/tasks/*
            │                          /metrics
            │
            └──────── HTTP ────────► S3 REST API (端口 8901)
                                     └─ ListBuckets, HeadObject, GetObject,
                                        PutObject, DeleteObject, CopyObject
```

MCP 服务器是一个轻量级的 Node.js 进程，充当桥梁角色：

- 它通过 **MCP（基于 stdio 的 JSON-RPC）** 与 AI 助手通信。
- 将每个工具调用转换为相应的 **Admin REST API** 或 **S3 REST API** 请求发送到 StoreFS 集群。
- 认证通过 Bearer token（Admin API）或 AWS Signature V4（S3 API）处理。

---

## 快速开始

### 前提条件

- **Node.js >= 18**
- **StoreFS v0.3.7 及以上版本**
- 正在运行的 StoreFS 集群（一个或多个节点）
- 集群的管理员凭据

### 1. 安装

```bash
tar -xzf mcp.tar.gz
cd mcp
npm install
```

### 2. 配置

在项目的 `.claude/settings.json`（或全局 `~/.claude/settings.json`）中配置 MCP 服务器：

```json
{
  "mcpServers": {
    "storefs": {
      "command": "node",
      "args": ["/绝对路径/mcp/index.js"],
      "env": {
        "STOREFS_ADMIN_URL": "http://127.0.0.1:7946",
        "STOREFS_S3_URL": "http://127.0.0.1:8901"
      }
    }
  }
}
```

环境变量：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `STOREFS_ADMIN_URL` | `http://127.0.0.1:7946` | Admin REST API 的基础 URL（gossip+HTTP 复用端口） |
| `STOREFS_S3_URL` | `http://127.0.0.1:8901` | S3 REST API 的基础 URL |
| `LANG` | — | 可选。设置为 `zh_CN`/`zh` 时工具描述显示中文 |

### 3. 启动 Claude Code

```bash
claude
```

### 4. 登录

告诉 AI 助手：

```
storefs_login(username="admin", password="your-password")
```

成功后您将看到包含角色和分组信息的提示。

> **注意：** 登录状态在会话期间保持有效。使用 `storefs_logout` 退出，使用 `storefs_whoami` 查看当前用户。

### 5. 启用 MFA 后的登录（使用个人访问令牌）

如果用户启用了 **MFA（TOTP）**，使用密码调用 `storefs_login` 会返回 MFA 需要验证的提示。由于 MCP 是命令行工具（无法实时输入 TOTP 验证码），需要使用**个人访问令牌（PAT）**来代替密码登录：

1. **创建 PAT**：在 Web 管理控制台（Profile → Personal Access Tokens）创建，或通过 Admin API：
   ```bash
   curl -X POST http://127.0.0.1:7946/api/auth/pat \
     -H "Authorization: Bearer <token>" \
     -H "Content-Type: application/json" \
     -d '{"description": "MCP 访问", "expiresIn": "30d"}'
   ```
   选择过期时间（7d/30d/90d/1y/2y/5y/forever）。**PAT 仅在创建时显示一次**，请立即保存。

2. **使用 PAT 登录**：
   ```
   storefs_login_with_pat(pat="stfs_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx")
   ```

3. **或设置环境变量**：在 MCP 配置中指定 `STOREFS_PAT`，启动时自动加载，无需手动登录：
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

PAT 可以绕过 MFA，适用于程序化访问。`storefs_login_with_pat` 工具也列在下文的系统工具表中。

---

## 功能概览

MCP 服务器提供 **90+ 工具**，分为十二个功能组：

### 1. 系统工具

| 工具 | 说明 |
|------|------|
| `storefs_health` | 检查集群可达性和健康状况 |
| `storefs_login` | 使用用户名/密码认证 |
| `storefs_login_with_pat` | 使用个人访问令牌认证（绕过 MFA 验证） |
| `storefs_logout` | 登出并在服务端吊销 JWT，然后清除本地会话 |
| `storefs_whoami` | 显示当前登录用户信息 |
| `storefs_cluster_status` | 列出所有节点、磁盘、使用情况及污点状态 |
| `storefs_node_metrics` | 获取 Prometheus 指标（可选过滤） |
| `storefs_change_password` | 修改当前用户的密码 |
| `storefs_list_node_status` | 查看所有节点的污点状态 |
| `storefs_update_node_status` | 更新节点的污点状态（仅 super_admin） |

### 2. 用户与分组管理工具

| 工具 | 说明 |
|------|------|
| `storefs_list_users` | 查询用户列表（支持分页和分组过滤） |
| `storefs_get_user` | 获取用户详细信息 |
| `storefs_create_user` | 创建用户（自动生成 AccessKey/SecretKey） |
| `storefs_update_user` | 更新用户角色、名称或分组 |
| `storefs_delete_user` | 删除用户 |
| `storefs_get_access_keys` | 查看用户的 AccessKey/SecretKey |
| `storefs_rotate_access_keys` | 重新生成密钥（旧密钥将失效） |
| `storefs_reset_password` | 重置用户密码 |
| `storefs_disable_user_mfa` | 停用用户 MFA（关闭双重验证并删除备份码） |
| `storefs_list_groups` | 查询所有分组 |
| `storefs_get_group` | 获取分组详情 |
| `storefs_create_group` | 创建新分组 |
| `storefs_update_group` | 更新分组名称或默认策略 |
| `storefs_delete_group` | 删除分组（需要 super_admin 权限） |

### 3. 存储策略管理工具

| 工具 | 说明 |
|------|------|
| `storefs_list_policies` | 列出所有存储策略（副本/纠删码） |
| `storefs_get_policy` | 获取策略详情 |
| `storefs_create_policy` | 创建存储策略 |
| `storefs_update_policy` | 修改现有策略 |
| `storefs_delete_policy` | 删除策略 |

支持两种策略类型：

- **副本（Replicas）**：简单复制 — 配置副本数量（如 3 副本）。
- **纠删码（Erasure Code / EC）**：将数据分为数据片和校验片，存储效率更高（如 4+2）。

### 4. 桶管理工具

| 工具 | 说明 |
|------|------|
| `storefs_list_buckets` | 列出桶（支持按用户/分组过滤） |
| `storefs_get_bucket` | 获取桶的配置详情（按 `bucketId` **或** `bucketName`） |
| `storefs_create_bucket` | 创建新桶（名称必须全局唯一） |
| `storefs_update_bucket` | 修改桶配置（按 `bucketId` **或** `bucketName`） |
| `storefs_delete_bucket` | 删除空桶（按 `bucketId` **或** `bucketName`） |
| `storefs_generate_presigned_url` | 生成用于上传/下载的预签名 URL（按 `bucketId` **或** `bucketName`） |
| `storefs_get_bucket_acl` | 获取桶 ACL（访问控制列表）— 按 `bucketId` **或** `bucketName` |
| `storefs_put_bucket_acl` | 设置桶 ACL — 控制用户的读/写访问权限（按 `bucketId` **或** `bucketName`） |

> **注意：** 桶工具支持传入 `bucketId`（数字）或 `bucketName`，二者提供其一即可；自然语言场景下用 `bucketName` 更方便（例如 "get bucket lakehouse"）。

ACL 授权对象类型：
- **canonical_user**: 指定用户（通过用户 ID 标识）
- **all_users**: 所有用户（含匿名）— `http://acs.amazonaws.com/groups/global/AllUsers`
- **authenticated_users**: 任意已认证用户 — `http://acs.amazonaws.com/groups/global/AuthenticatedUsers`

ACL 权限：`FULL_CONTROL` | `WRITE` | `READ` | `READ_ACP` | `WRITE_ACP`

> **注意：** 设置 ACL 会原子替换所有现有条目。Owner 始终自动保留 `FULL_CONTROL`。

可配置的桶级别功能：

- **版本控制**：追踪对象版本（`Enabled` / `Suspended`）
- **对象锁定**：防止对象被删除（GOVERNANCE / COMPLIANCE 模式）
- **公开读**：允许未经认证的 GET 请求
- **服务端加密**：对静态数据进行 SSE 加密
- **存储策略**：分配副本或 EC 策略

### 5. 对象与分块上传管理工具

| 工具 | 说明 |
|------|------|
| `storefs_list_objects` | 列出桶内的对象（按 `bucketId` **或** `bucketName`） |
| `storefs_get_object_info` | 获取对象在各节点的片段分布信息 |
| `storefs_get_object_versions` | 列出对象的所有版本（按 `bucketId` **或** `bucketName`） |
| `storefs_delete_object` | 删除对象（支持指定版本 ID；按 `bucketId` **或** `bucketName`） |
| `storefs_list_multipart_uploads` | 列出未完成的分块上传（按 `bucketId` **或** `bucketName`） |
| `storefs_get_multipart_upload` | 获取分块上传详情及 Part 列表 |
| `storefs_complete_multipart_upload` | 完成分块上传 |
| `storefs_abort_multipart_upload` | 取消分块上传 |
| `storefs_get_part_fragment_info` | 获取指定 Part 的片段健康状态 |

### 6. S3 数据操作工具

| 工具 | 说明 |
|------|------|
| `storefs_s3_list_buckets` | 通过 S3 API 列出用户可访问的桶 |
| `storefs_s3_head_object` | 获取对象元数据（大小、ETag、Content-Type） |
| `storefs_s3_get_object` | 查看小文本文件内容（默认 ≤1MB） |
| `storefs_s3_put_object` | 上传短文本内容（支持 `tags` 参数） |
| `storefs_s3_delete_object` | 通过 S3 API 删除对象 |
| `storefs_s3_copy_object` | 在桶之间复制对象 |
| `storefs_s3_upload_file` | 将本地文件上传到 S3（支持 `tags` 参数） |
| `storefs_s3_download_file` | 将 S3 对象下载到本地文件 |
| `storefs_s3_get_object_tagging` | 获取对象标签（支持 `versionId`） |
| `storefs_s3_put_object_tagging` | 替换对象的所有标签（支持 `versionId`） |
| `storefs_s3_delete_object_tagging` | 删除对象的所有标签（支持 `versionId`） |
| `storefs_s3_get_bucket_tagging` | 获取桶标签 |
| `storefs_s3_put_bucket_tagging` | 替换桶的所有标签 |
| `storefs_s3_delete_bucket_tagging` | 删除桶的所有标签 |
| `storefs_s3_select` | 通过 S3 Select API 对对象执行 SQL 查询（支持 CSV/JSON 格式） |

> **注意：** S3 数据操作使用当前用户的 AccessKey/SecretKey，登录后会自动获取。如果用户没有密钥，使用 `storefs_rotate_access_keys` 生成。

### 7. 任务管理工具

| 工具 | 说明 |
|------|------|
| `storefs_list_task_types` | 列出所有可用的任务类型 |
| `storefs_list_tasks` | 列出所有任务（支持分页和状态过滤） |
| `storefs_get_task` | 获取任务详情 |
| `storefs_create_task` | 创建任务（通用） |
| `storefs_create_repair_task` | 创建修复任务 — 修复节点上损坏/丢失的碎片（需 super_admin 权限） |
| `storefs_create_replacedisk_task` | 创建替换磁盘任务 — 将旧磁盘数据迁移到新磁盘（需 super_admin 权限） |
| `storefs_cancel_task` | 取消运行中的任务 |
| `storefs_delete_task` | 删除任务（运行中的任务会先取消） |
| `storefs_cleanup_tasks` | 清理过期的已完成/失败任务（需 super_admin 权限） |

任务是异步后台操作，在指定节点上执行。使用 `storefs_list_tasks` 监控进度，`storefs_get_task` 查看详情。

关于任务系统、修复和替换磁盘操作的详细文档，请参考[任务系统文档](task_cn.md)。

### 8. 通知管理工具

| 工具 | 说明 |
|------|------|
| `storefs_list_bucket_notifications` | 列出桶的所有通知配置（按 `bucketId` **或** `bucketName`） |
| `storefs_get_notification` | 获取单个通知详情 |
| `storefs_create_notification` | 为桶创建新的 Webhook 通知（按 `bucketId` **或** `bucketName`） |
| `storefs_update_notification` | 更新现有通知配置 |
| `storefs_delete_notification` | 删除通知配置 |
| `storefs_test_webhook` | 发送测试事件到 Webhook URL 验证连通性 |

通知配置字段：

| 字段 | 说明 |
|------|------|
| `url` | Webhook 端点 URL（必填） |
| `secret` | 可选的 HMAC-SHA256 签名密钥，用于负载验证 |
| `events` | 逗号分隔的事件类型（默认：`s3:ObjectCreated:*`） |
| `filterPrefix` | 仅触发对象键以此前缀开头的事件 |
| `filterSuffix` | 仅触发对象键以此后缀结尾的事件 |
| `format` | 负载格式：`native`（默认）或 `aws` |
| `enabled` | 启用/禁用通知 |

详细文档请参考：[通知系统文档](notification_cn.md)

### 9. KMS 管理工具

| 工具 | 说明 |
|------|------|
| `storefs_get_kms_config` | 获取当前 KMS 配置（端点、超时、健康检查间隔） |
| `storefs_get_kms_config_by_id` | 获取指定 ID 的 KMS 配置 |
| `storefs_list_kms_configs` | 列出所有 KMS 配置 |
| `storefs_create_kms_config` | 创建新的 KMS 配置 |
| `storefs_delete_kms_config` | 删除 KMS 配置（需先删除所有引用的密钥） |
| `storefs_update_kms_config` | 更新 KMS 连接参数（端点、凭证、TLS 证书、超时） |
| `storefs_test_kms_config` | 测试 KMS 连接但不保存（验证端点、TLS 和凭证） |
| `storefs_check_kms_health` | 检查 KMS 服务健康状态（在线/离线） |
| `storefs_list_kms_keys` | 列出所有 KMS 密钥（支持分页，可通过 `showRetired` 包含已退役密钥） |
| `storefs_create_kms_key` | 创建新的 KMS 密钥（需提供别名和可选描述，可选 `kmsConfigId` 选择 KMS 服务） |
| `storefs_rotate_kms_key` | 轮转 KMS 密钥（创建新密钥、退役旧密钥、重新包装所有桶密钥） |
| `storefs_delete_kms_key` | 删除 KMS 密钥（仅当没有被任何桶使用时） |

KMS 提供 **SSE-KMS**（使用 KMS 管理密钥的服务端加密）作为除 SSE-S3 和 SSE-C 之外的另一种加密模式。当桶启用 SSE-KMS 时，每个对象的数据加密密钥（DEK）由存储在外部 KMS 服务（当前支持 KMIP 1.2+，后续可扩展云服务）中的 KMS 主密钥（CMK）加密保护。

密钥轮转会创建新密钥、将所有桶的 DEK 从旧 CMK 重新包装到新 CMK，并将旧密钥标记为 `retired`（已退役）。已退役密钥默认不显示在列表中——使用 `showRetired=true`（仅 super_admin）可查看。超过 90 天的已退役密钥会被自动清理。

详细文档请参考：[KMS 配置](admin-api_cn.md#9-kms-管理) 的管理 API 文档。

### 10. 目录搜索工具

| 工具 | 说明 |
|------|------|
| `storefs_catalog_stats` | 获取目录搜索状态（启用/禁用，OpenSearch 信息） |
| `storefs_catalog_enable` | 启用目录搜索引擎（仅 super_admin） |
| `storefs_catalog_disable` | 禁用目录搜索引擎（仅 super_admin） |
| `storefs_catalog_config` | 更新目录搜索配置 — OpenSearch（端点、凭据、索引名）和 Embedding（基础 URL、模型、维度、API 密钥） |
| `storefs_catalog_get_config` | 获取当前目录搜索配置（OpenSearch + Embedding 设置） |
| `storefs_catalog_search_objects` | 全文检索 + 过滤搜索对象（支持 object_type、content_type、tags、meta、prefix、bucket_ids、大小范围、时间范围） |
| `storefs_catalog_search` | SQL 模式搜索 — 如 `SELECT * WHERE tag='key:value' AND size>=1000` |
| `storefs_catalog_vector_search` | 向量相似度搜索 — 通过向量数组或文本（自动生成嵌入），可选 bucket_ids 和 object_type 过滤 |
| `storefs_catalog_get_object_metadata` | 获取对象元数据（user_meta、http_meta），按 bucket_id 和 object_name |
| `storefs_catalog_test_connection` | 测试 OpenSearch 连接（返回集群名、健康状态、版本） |
| `storefs_catalog_test_embedding` | 测试 Embedding API 连接（返回连接状态） |

目录搜索功能支持对所有存储对象进行全文检索和语义搜索。需要以下组件：
- **OpenSearch** 集群（需安装 k-NN 插件以支持向量搜索）
- **CatalogBuilder**（独立进程）负责扫描、提取内容、生成向量嵌入并写入索引
- 可选 **Embedding API**（兼容 OpenAI 格式）用于混合搜索

详细文档请参考：[目录搜索文档](catalog_cn.md)。

### 11. 湖仓（Iceberg）工具

通过自然语言管理 StoreFS 湖仓 —— catalog、命名空间、表与维护。这些工具封装了 Iceberg 管理控制面（`/api/iceberg/admin/...`）。

| 工具 | 说明 |
|------|------|
| `storefs_iceberg_get_status` | 检查 Iceberg 湖仓功能是否启用 |
| `storefs_iceberg_enable` | 集群级启用 Iceberg（自动创建 warehouse 桶） |
| `storefs_iceberg_disable` | 集群级禁用 Iceberg |
| `storefs_iceberg_get_config` | 获取 Iceberg 配置（启用状态、warehouse 桶、默认命名空间、幂等保留天数、孤儿宽限期） |
| `storefs_iceberg_set_config` | 更新 Iceberg 配置（所有字段可选） |
| `storefs_iceberg_list_catalogs` | 列出当前用户可访问的 catalog |
| `storefs_iceberg_get_catalog` | 获取单个 catalog 详情（warehouse、owner、状态） |
| `storefs_iceberg_create_catalog` | 在可写的 warehouse（`s3://bucket/prefix/`）上创建 catalog |
| `storefs_iceberg_delete_catalog` | 软删除 catalog（仅 owner 或 super_admin；须为空） |
| `storefs_iceberg_list_namespaces` | 列出某 catalog 的命名空间（含表数量） |
| `storefs_iceberg_list_tables` | 列出命名空间下的表（大小、快照数、最后更新时间） |
| `storefs_iceberg_get_table` | 获取表详情（location、metadata location、表 UUID） |
| `storefs_iceberg_get_schema` | 获取表 Schema（字段、类型、是否必填、分区规范） |
| `storefs_iceberg_list_snapshots` | 列出表的快照历史 |
| `storefs_iceberg_list_commits` | 列出提交历史（head generation、commit ID、writer） |
| `storefs_iceberg_storage_usage` | 获取各命名空间存储占用（总/数据/元数据字节与文件数） |
| `storefs_iceberg_expire_snapshots` | 过期旧快照（支持 `retain_last`、`dry_run`） |
| `storefs_iceberg_remove_orphan_files` | 清理孤儿文件（支持 `dry_run`） |
| `storefs_iceberg_compact_metadata` | 压缩元数据链（支持 `dry_run`） |
| `storefs_iceberg_query` | 用 SQL 查询表数据（`SELECT col1, col2 FROM ...`，通过 Parquet/Arrow） |

所有维护工具（`expire_snapshots`、`remove_orphan_files`、`compact_metadata`）均支持 `dry_run: true` 先预览候选再执行。

湖仓完整说明见：[湖仓文档](lakehouse_cn.md)。

---

## 常用提示词

以下按场景分类的任务型提示词，可直接向 AI 助手提出。

### 集群操作

```
检查集群健康状态
查看集群状态——所有节点和磁盘使用情况
获取节点指标，过滤关键词 "cpu"
查看所有节点的污点状态
更新节点 "node3" 的污点状态为 "taint"
将节点 "node3" 的污点状态恢复为 "active"
```

### 用户管理

```
列出系统中所有用户
查看用户 ID 1 的详细信息
创建一个新用户 "alice"，角色为 "user"，归属于分组 1
重置用户 2 的密码为 "NewP@ss123"，要求下次登录改密
为用户 3 重新生成 AccessKey
删除 ID 为 4 的用户
```

### 分组管理

```
列出所有分组
创建一个名为 "engineering" 的分组，默认策略 ID 为 1
更新分组 2，将默认策略设为策略 3
```

### 存储策略

```
列出所有存储策略
创建一个名为 "triple-replication" 的副本策略，副本数为 3
创建一个名为 "ec-4-2" 的纠删码策略，4 个数据片和 2 个校验片
```

### 桶操作

```
列出所有桶
创建一个名为 "my-data" 的桶，策略 ID 为 1，所有者 ID 为 2
创建一个名为 "important-files" 的版本控制桶，启用版本控制和公开读
查看桶 5 的详细信息
为桶 1 中的 "upload.txt" 生成一个预签名 PUT URL
```

### 对象管理

```
列出 "my-data" 桶中的对象
查看 "my-bucket" 桶中 "report.pdf" 的片段分布信息
列出桶 3 中 "config.json" 的所有版本
删除桶 2 中的 "old-backup.zip"
```

### 分块上传

```
列出桶 1 中未完成的分块上传
查看桶 1 中 "large-file.iso" 的上传 "abc-123" 的详情
完成桶 1 中 "large-file.iso" 的分块上传 "abc-123"
取消分块上传 "xyz-789"
检查上传 "abc-123" 的 Part 3 的片段健康状态
```

### S3 数据操作

```
列出我的 S3 桶
查看桶 "docs" 中 "readme.md" 的元数据
查看桶 "my-data" 中 "config.json" 的内容
将文本 "hello world" 作为 "greeting.txt" 上传到桶 "my-data"
将本地文件 "/tmp/report.pdf" 作为 "reports/report.pdf" 上传到桶 "docs"
将 "backup.tar.gz" 从桶 "archive" 下载到 "/tmp/backup.tar.gz"
将对象 "source.txt" 从桶 "a" 复制到桶 "b" 中 "backups/source.txt"
使用 SQL 查询桶 "my-data" 中的 CSV 对象 "data.csv"："SELECT * FROM S3Object WHERE age > 30"
使用 SQL 查询桶 "my-data" 中的 JSON 对象 "data.json"："SELECT name, age FROM S3Object"
```

### 标签管理

```
查看桶 "my-data" 中对象 "report.pdf" 的标签
设置桶 "my-data" 中对象 "report.pdf" 的标签为 [{"key":"department","value":"engineering"}]
删除桶 "my-data" 中对象 "report.pdf" 的所有标签
查看桶 "my-data" 的标签
设置桶 "my-data" 的标签为 [{"key":"project","value":"storefs"}]
删除桶 "my-data" 的所有标签
上传文件并添加标签：将 "/tmp/doc.pdf" 作为 "doc.pdf" 上传到桶 "docs"，标签为 "project=storefs&department=engineering"
```

### 通知管理

```
列出桶 1 的通知配置
查看通知 5 的详细信息
为桶 1 创建 Webhook 通知：URL https://hooks.example.com/notify，事件 s3:ObjectCreated:Put
更新通知 5，将其禁用
删除通知 5
使用原生格式测试 Webhook URL https://hooks.example.com/test
```

### 目录搜索

```
查看目录搜索状态
搜索包含 "报告" 的对象
搜索标签为 "project:alpha" 且大小大于 1MB 的 PDF 文件
SQL 搜索：SELECT * WHERE object_type='pdf' AND size>=1000000
搜索桶 1 中前缀为 "documents/" 的对象
向量搜索：查找与 "季度财务报告" 相似的对象
获取目录搜索配置
启用目录搜索引擎
测试 OpenSearch 连接
测试 Embedding API 连接
```

### 湖仓（Iceberg）

```
检查 Iceberg 是否启用
启用 Iceberg 湖仓
列出所有 catalog
在 warehouse "s3://analytics-wh/" 上创建 catalog "analytics"
列出 catalog "lakehouse" 的命名空间
列出命名空间 "sales" 下的表
查看命名空间 "sales" 中表 "orders" 的 schema
列出表 "orders" 的快照
查看命名空间 "sales" 的存储占用
预演过期 "sales.orders" 的旧快照，保留 5 个
清理表 "orders" 的孤儿文件（先 dry run）
压缩表 "orders" 的元数据链
查询表 "sales.orders" 的前 10 行
```

### 维护与故障排查

```
检查哪些节点离线了
查找需要清理的未完成分块上传
显示占用磁盘空间最大的桶
列出 "admin" 分组中的所有用户
列出所有任务及其状态
查看任务 42 的详情
为节点 "node1" 创建一个 replacedisk 任务，旧盘 "/data/disk1"，新盘 "/data/disk2"
取消运行中的任务 15
```

---

## 国际化 (i18n)

语言判定按以下优先级：

1. **`LANG` 环境变量** — 如果设置了（如 `zh_CN.UTF-8`），则覆盖所有其他检测逻辑。工具描述语言也由此变量决定。
2. **根据输入自动检测** — 仅当 `LANG` 未配置时生效：
   - 如果**所有**字符串参数都包含**汉字（中文特有的 CJK 表意文字）**，响应语言为**中文**。
   - 如果任意参数包含**日文（假名）**或**韩文（谚文）**，响应语言默认使用**英文**。
   - 否则响应语言为**英文**。
- 否则响应语言为**英文**。

工具描述文本根据系统 `LANG` 环境变量决定。

---

## 安全说明

- **认证状态**保存在内存中，作用域限于 Claude Code 会话，不会持久化到磁盘。
- **S3 操作**使用当前用户的 AccessKey/SecretKey，登录后自动从 Admin API 获取。
- **登录凭据**不会记录日志或在工具调用参数之外保留。
- **对象锁定**的 COMPLIANCE 模式一旦设置无法移除——即使管理员也无法在保留期结束前删除被锁定的对象。
- **预签名 URL**默认有效期为 5 分钟。

---

## 附录：Admin API 与 S3 API 的区别

MCP 服务器使用两个独立的 API 接口：

| | Admin REST API（端口 7946） | S3 REST API（端口 8901） |
|---|---|---|
| **认证方式** | Bearer token（JWT） | AWS Signature V4 |
| **管理范围** | 集群管理——用户、桶、策略、对象元数据 | S3 兼容的数据操作 |
| **使用场景** | 所有系统、用户、策略、桶和对象管理工具 | `storefs_s3_*` 数据操作工具 |

Admin API 用于管理工具；S3 API 专用于数据面操作（上传、下载、复制、删除、查看元数据），完全兼容 S3 协议。
