**[English](faq.md)**

# FAQ 文档

## 常见问题解答

### 1. 安装和配置

#### 1.1 如何选择数据库类型？

StoreFS 目前支持以下数据库：
- **MySQL**：传统关系型数据库，适合小型部署
- **Apache Doris**：高性能分析型数据库，适合大规模存储和复杂查询

对于生产环境，推荐使用 Apache Doris，因为它提供了更好的查询性能和扩展性。

#### 1.2 如何配置多个磁盘？

在 `config.yaml` 文件中添加多个磁盘配置：

```yaml
node:
  disks:
    - path: /data/disk1
      weight: 1
    - path: /data/disk2
      weight: 2
```

权重值越大，该磁盘分配到的数据越多。

#### 1.3 如何修改节点监听端口？

在 `config.yaml` 文件中修改以下配置：

```yaml
node:
  port: 7946          # 节点通信, API和console端口
  internal_port: 17946 # 内部文件操作端口
  s3:
    port: 8901        # S3 API端口
```

### 2. 集群管理

#### 2.1 如何添加新节点到集群？

1. 在新节点上安装 StoreFS
2. 配置 `config.yaml`，确保：
   - `cluster.name` 与现有集群一致
   - `node.name` 和 `node.num` 唯一
   - `seeds` 包含至少一个现有节点
3. 启动新节点：`./storefs -config config.yaml`

#### 2.2 如何安全地删除节点？

1. 确保节点上的所有数据已经复制到其他节点
2. 停止节点进程：`pkill storefs`
3. 从其他节点的 `seeds` 配置中移除该节点地址

#### 2.3 如何检查集群健康状态？

使用健康检查 API：

```bash
curl http://<node-ip>:7946/api/health
```

返回 `"status": "healthy"` 表示节点健康。

### 3. 数据管理

#### 3.1 如何备份数据？

StoreFS 支持以下备份方式：
1. **手动备份**：直接复制磁盘上的文件
2. **S3 接口**：使用 AWS CLI 或其他 S3 工具同步数据到外部存储
3. **数据库备份**：备份 Apache Doris/MySQL 中的元数据

#### 3.2 如何恢复数据？

1. 恢复元数据：从数据库备份中恢复
2. 恢复数据文件：将备份的文件复制到相应位置
3. 启动节点：`./storefs -config config.yaml`

#### 3.3 如何更换磁盘？

StoreFS 支持通过任务系统在线更换磁盘，无需停服即可完成旧盘到新盘的数据迁移。

操作步骤：

1. **设置节点为污点状态** — 标记节点进入维护模式，阻止新数据写入。可通过以下任一方式：
   - **Web Admin Console**：节点管理 → 将目标节点状态设为 "taint"
   - **Admin API**：`curl -X PUT http://<node-ip>:7946/api/node-status/<节点名称> -H "Authorization: Bearer <token>" -d '{"status":"taint"}'`
   - **MCP**：`storefs_update_node_status(nodeName="<节点名称>", status="taint")`

2. **对污点节点执行 ReplaceDisk 任务**，将数据从旧磁盘迁移到新磁盘：
   - **Web Admin Console**：任务管理 → 创建任务 → 选择 "Replace Disk" 类型，选择目标节点，填入旧磁盘路径和新磁盘路径
   - **Admin API**：`curl -X POST http://<node-ip>:7946/api/tasks -H "Authorization: Bearer <token>" -H "Content-Type: application/json" -d '{"type":"replacedisk","params":{"oldDiskPath":"/data/old-disk","newDiskPath":"/data/new-disk","nodeName":"<节点名称>"}}'`
   - **MCP**：`storefs_create_task(type="replacedisk", params={oldDiskPath: "/data/old-disk", newDiskPath: "/data/new-disk", nodeName: "<节点名称>"})`

   可通过以下方式查看任务执行进度：
   - **Web Admin Console**：任务管理 → 活跃任务（进度条实时更新）
   - **Admin API**：`curl http://<node-ip>:7946/api/tasks/<任务ID> -H "Authorization: Bearer <token>"`
   - **MCP**：`storefs_get_task(taskId=...)`

3. **取消节点的污点状态**，任务成功后恢复为正常状态：
   - **Web Admin Console**：节点管理 → 将节点状态设为 "active"
   - **Admin API**：`curl -X PUT http://<node-ip>:7946/api/node-status/<节点名称> -H "Authorization: Bearer <token>" -d '{"status":"active"}'`
   - **MCP**：`storefs_update_node_status(nodeName="<节点名称>", status="active")`

> **注意**：`replacedisk` 任务也可通过 S3 控制台的任务管理界面创建和查看进度。所有操作仅 `super_admin` 角色可执行。

#### 3.4 何时需要执行 Repair（修复碎片）任务？

Repair 任务用于重建丢失或不一致的片段元数据。在以下场景中应执行 Repair：

- **节点异常重启后** — 某些片段可能未正确刷盘，导致元数据不一致
- **磁盘更换后** — 新磁盘上线后，旧磁盘上的片段引用可能需要更新
- **集群故障恢复时** — 当元数据与实际数据不同步时，通过 Repair 扫描并重建片段索引
- **定期健康检查** — 在低负载时段主动执行 Repair，提前发现并修复片段级别的问题

操作步骤：

1. **设置节点为污点状态**（如果针对特定节点修复）：
   - **Web Admin Console**：节点管理 → 将目标节点状态设为 "taint"
   - **Admin API**：`curl -X PUT http://<node-ip>:7946/api/node-status/<节点名称> -H "Authorization: Bearer <token>" -d '{"status":"taint"}'`

2. **对节点执行 Repair 任务**：
   - **Web Admin Console**：任务管理 → 创建任务 → 选择 "Repair Fragments" 类型，选择目标节点，可选指定磁盘路径以只修复该磁盘
   - **Admin API**：`curl -X POST http://<node-ip>:7946/api/tasks -H "Authorization: Bearer <token>" -H "Content-Type: application/json" -d '{"type":"repair","params":{"node":"<节点名称>","disk":"/data/disk1"}}'`
   - **MCP**：`storefs_create_task(type="repair", params={node: "<节点名称>", disk: "/data/disk1"})`

   > `disk` 参数为可选项。不指定时修复节点上所有磁盘。

   可通过以下方式查看进度：
   - **Web Admin Console**：任务管理 → 活跃任务
   - **Admin API**：`curl http://<node-ip>:7946/api/tasks/<任务ID> -H "Authorization: Bearer <token>"`
   - **MCP**：`storefs_get_task(taskId=...)`

3. **取消节点的污点状态**，任务完成后恢复为正常状态：
   - **Web Admin Console**：节点管理 → 将节点状态设为 "active"
   - **Admin API**：`curl -X PUT http://<node-ip>:7946/api/node-status/<节点名称> -H "Authorization: Bearer <token>" -d '{"status":"active"}'`
   - **MCP**：`storefs_update_node_status(nodeName="<节点名称>", status="active")`

> **注意**：Repair 任务是只读操作 — 它扫描并修复片段元数据，不会移动数据。可以在运行中的节点上安全执行，但建议先设置污点状态以避免修复过程中有新数据写入。

#### 3.5 何时需要执行 ReplaceDisk（替换磁盘）任务？

ReplaceDisk 任务将数据从一个磁盘迁移到同一节点上的另一个磁盘。与 Repair 不同，ReplaceDisk 是物理移动数据。在以下场景中应执行 ReplaceDisk：

- **磁盘故障或降级** — 磁盘出现 I/O 错误、坏道或即将故障时，将数据迁移到健康磁盘
- **磁盘容量升级** — 用小容量磁盘替换大容量磁盘时，将数据迁移到新磁盘
- **磁盘性能升级** — 用 SSD 替换 HDD 时，将数据迁移到更快的磁盘
- **磁盘路径变更** — 磁盘挂载点或目录路径发生变化时，将数据迁移到新路径
- **磁盘下线** — 需要从节点中移除磁盘时，先将数据迁移到其他磁盘

具体操作步骤与 [3.3 如何更换磁盘？](#3-3-如何更换磁盘) 相同。

> **注意**：ReplaceDisk 是数据移动操作，而非只读操作。启动任务前请确保目标磁盘有足够容量。执行 ReplaceDisk 前应将节点设为污点状态，防止迁移过程中有新数据写入。

### 4. 性能优化

#### 4.1 如何提高写入性能？

1. 增加节点数量，分担写入压力
2. 使用更快的存储设备（如 SSD）
3. 调整磁盘权重，使数据均匀分布
4. 优化数据库配置（如 Apache Doris 的内存和并发设置）

#### 4.2 如何提高读取性能？

1. 使用副本策略，增加数据冗余
2. 在查询频繁的节点上存储热点数据
3. 优化磁盘布局，避免磁盘竞争
4. 使用 CDN 或缓存机制

#### 4.3 如何优化内存使用？

1. 调整数据库缓存大小
2. 优化查询语句，减少内存消耗
3. 限制并发查询数量
4. 使用更大内存的服务器

### 5. 安全

#### 5.1 如何保护 StoreFS 集群？

1. 使用防火墙限制访问端口
2. 配置 HTTPS 通信
3. 定期更换访问密钥
4. 限制数据库访问权限

#### 5.2 如何审计操作？

StoreFS 目前不直接提供审计日志，但可以通过以下方式实现：
1. 启用数据库审计功能
2. 使用网络抓包工具（如 tcpdump）
3. 配置系统日志记录

#### 5.3 如何处理访问密钥泄漏？

1. 立即删除泄漏的访问密钥
2. 创建新的访问密钥
3. 更新受影响的应用程序配置
4. 审计系统，查找异常活动

### 6. 资源命名规则

StoreFS 在整个系统中（后端 API 和管理控制台）强制执行资源名称校验。

#### 6.1 桶名称（Bucket Name）规则
- 长度：3–63 个字符
- 只能包含小写字母、数字和连字符（`-`）
- 必须以字母或数字开头和结尾
- 不能是 IP 地址格式（如 `192.168.1.1`）
- 不能有连续的连字符
- 禁止使用保留前缀 `xn--` 和 `sthree-`

#### 6.2 对象键（Object Key）规则
- 长度：1–1024 字节（非字符数）
- 必须是有效的 UTF-8 编码
- 不能包含控制字符（C0/C1 控制码）
- 不能包含空字符（`0x00`）

#### 6.3 策略名称（Policy Name）规则
- 长度：1–128 个字符
- 允许字符：字母（`a-zA-Z`）、数字（`0-9`）和 `+=,.@_-`
- 区分大小写（保留原始大小写）

#### 6.4 用户名（Username）规则
- 长度：1–63 个字符
- 允许字符：小写字母、数字、点（`.`）、下划线（`_`）和连字符（`-`）
- 必须以字母或数字开头
- 不能有连续的点（`..`）
- 不能以连字符或点结尾
- 禁止使用保留名称：`admin`、`root`、`system`、`guest`

#### 6.5 用户组名称（Group Name）规则
- 与用户名规则相同（见 6.4）

### 7. 其他问题

#### 7.1 为什么上传的文件无法下载？

可能的原因：
1. 文件没有完全上传
2. 文件元数据损坏
3. 网络连接问题
4. 权限配置错误

#### 7.2 如何查看系统日志？

StoreFS 日志默认输出到标准输出，可以通过以下方式查看：
1. 启动时重定向到文件：`./storefs -config config.yaml > storefs.log 2>&1`
2. 使用系统日志服务（如 systemd）
3. 使用 Docker 日志功能：`docker logs storefs-node1`

### 8. 技术支持

如果您遇到未在本FAQ中解决的问题，请：

1. 查看 [故障排除](troubleshooting_cn.md) 文档
2. 检查系统日志
3. 在 GitHub Issues 中报告问题
4. 联系技术支持团队
