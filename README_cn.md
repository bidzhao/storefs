**[English](README.md)**

<p align="center">
  <img src="docs/pics/logo.jpg" height="100" alt="StoreFS Logo">
</p>
# StoreFS - 分布式S3兼容存储系统

## 索引
- [概览](#概览)
- [安装与部署](#安装与部署)
- [管理控制台](#管理控制台)
- [用户角色与用户组](#用户角色与用户组)
- [S3 API](#s3-api)
- [Admin API](#admin-api)
- [s3file CLI](#s3file-cli)
- [Gateway (NFS/SMB)](#gateway-nfssmb-1)
- [监控](#监控)
- [MCP for AI Agent](#mcp-for-ai-agent)
- [通知系统](#通知系统)
- [审计日志](#审计日志)
- [任务系统](#任务系统)
- [快速开始](#快速开始)
- [技术支持](#技术支持)
- [许可证](#许可证)

## 概览

StoreFS是一个基于Go语言实现的分布式S3兼容存储系统，采用gossip协议实现集群成员管理和通信。系统支持动态节点管理、数据分布和容错功能，为用户提供高性能、可扩展的对象存储服务。
本项目使用Claude Code自动化生成全部代码和文档。

### 核心特点

- **S3兼容API**：兼容AWS S3 API，支持使用AWS CLI和其他S3工具
- **分布式架构**：通过gossip协议实现节点发现和通信
- **动态扩展**：支持自由添加/删除节点，无需停机维护
- **高性能存储**：优化的存储引擎，支持多种存储介质
- **RDMA加速**：get、put和multipart upload均支持RDMA，用于高吞吐对象传输
- **容错机制**：节点故障时，数据会自动恢复
- **负载均衡**：请求会自动分发到可用节点
- **Web管理控制台**：提供直观的Web界面管理用户、策略、桶和对象
- **多语言支持**：管理控制台支持中文和英文
- **安全特性**：支持对象版本控制（保留历史版本，防止意外删除）、对象锁定（WORM模型，提供治理和合规两种模式）、桶级别 AES-256-CTR 加密、SSE-C（客户端提供加密密钥）以及 SSE-KMS（通过 KMS 的外部密钥管理，当前支持 KMIP 1.2+，后续可扩展云服务），全方位保护数据安全。**MFA（TOTP）**：管理控制台双重身份验证，支持个人访问令牌（PAT）用于程序化访问。
- **Gzip压缩传输**：PutObject 和 Multipart Upload 支持通过 Content-Encoding 头传输 gzip 压缩的请求体。
- **s3file CLI**：交互式 S3FS 模式，像操作本地文件系统一样浏览对象，支持 Ctrl+C 取消操作。
- **任务管理**：后台任务系统，用于执行长时间运行的管理操作，如修复损坏的文件片段和替换故障磁盘，支持进度追踪和取消操作。
- **节点污点标记**：将节点标记为污点（taint）以阻止新数据写入，或重新激活节点——适用于节点维护、磁盘替换或故障排查场景。
- **事件通知**：基于 Webhook 的桶级别事件通知，当对象创建或删除时自动触发。支持可配置的事件类型、对象键前缀/后缀过滤、HMAC-SHA256 签名验证、指数退避重试，以及原生和 AWS S3 兼容两种负载格式。

### 核心概念

- **User（用户）**：系统的使用者，拥有唯一的身份标识。每个用户都有角色（`user`、`group_admin` 或 `super_admin`），通常归属于一个用户组。用户通过访问密钥（AK）和秘密密钥（SK）进行身份验证。

- **Role（角色）**：决定用户的管理范围和存储访问行为。普通用户管理自己的资源，用户组管理员管理本组内的用户和资源，超级管理员管理全局系统配置但不直接访问 S3 对象数据。

- **Group（用户组）**：用于组织用户、委派管理权限和配置共享的默认策略。用户组可以设置默认策略，在创建桶且未显式选择策略时自动使用。

- **Policy（策略）**：定义了用户对桶（Bucket）和对象（Object）的访问权限。策略可以精确控制用户的操作权限，如读写、列出桶内容、删除对象等。

- **Bucket（桶）**：存储对象的容器。每个桶有唯一的名称，用户可以在桶中创建、删除和管理对象。桶可以配置访问策略，控制哪些用户可以访问。

- **Versioning（版本控制）**：桶级别的配置，启用后会保留对象的历史版本。当对象被覆盖或删除时，会创建新的版本或删除标记，允许恢复到之前的版本。

- **Object Lock（对象锁定）**：桶级别的WORM（Write Once, Read Many）配置，支持两种锁定模式：
  - **Governance Mode（治理模式）**：特定权限用户可以覆盖或删除锁定对象
  - **Compliance Mode（合规模式）**：任何用户都不能覆盖或删除锁定对象，直到保留期结束
  - 支持默认保留策略，自动应用于新上传的对象

- **Encryption（加密）**：桶级别的 AES-256-CTR 加密（默认：开启）。上传到加密桶的每个对象都会自动生成唯一的 AES-256 密钥并加密存储。可以通过管理控制台或管理 API 在创建或编辑桶时开启或关闭加密。
  - **SSE-S3**：使用桶管理密钥的服务端加密（AES-256-CTR）。
  - **SSE-C**：使用客户端提供的密钥进行服务端加密。
  - **SSE-KMS**：通过 KMS（当前支持 KMIP 1.2+，后续可扩展云服务）使用外部密钥管理服务进行加密。支持集中式密钥创建、轮转和管理。每个对象的数据密钥（DEK）由 KMS 主密钥（CMK）加密保护。

- **ACL（访问控制列表）**：S3 兼容的桶级别访问控制，支持五种权限（FULL_CONTROL、WRITE、READ、READ_ACP、WRITE_ACP）和三种授权对象类型（CanonicalUser、AllUsers、AuthenticatedUsers）。可通过 S3 XML API 或 Admin JSON API 管理。详细信息，请参考：[ACL 文档](docs/acl_cn.md)

- **Tagging（标签）**：可附加到桶（最多 50 个）和对象（最多 10 个）上的键值对元数据，用于分类、访问控制和成本跟踪。支持通过 S3 API 进行获取/设置/删除操作，启用版本控制时支持版本感知的标签管理。

- **S3 Select（对象内容选择）**：允许使用 SQL 表达式查询结构化对象内容（CSV 和 JSON），无需下载整个对象。支持 SELECT、WHERE、LIMIT、聚合函数（COUNT、SUM、AVG、MIN、MAX）以及多种 SQL 函数（SUBSTRING、TRIM、UPPER、LOWER 等）。支持 GZIP 解压缩。

- **Taint（节点污点标记）**：节点的一种状态，标记节点为不健康或维护中。被标记为污点的节点会被排除在新数据写入和对象放置之外，但已有数据仍可正常读取。节点可以由管理员手动标记为污点（用于维护或故障排查），也可在磁盘满时自动标记。

- **Task（任务）**：一种后台管理操作，在目标节点上执行长时间运行的后台维护活动。任务的生命周期为 Pending（待执行）→ Running（运行中）→ Completed（已完成）/ Failed（失败）/ Cancelled（已取消）。支持的任务类型包括 `repair`（扫描并修复损坏的文件片段）和 `replacedisk`（将旧磁盘数据迁移到新磁盘）。

- **Notification（桶通知）**：桶级别的 Webhook 配置，当匹配的对象事件发生时自动发送 HTTP POST 请求。支持两种负载格式（native 和 AWS S3 兼容）、事件类型过滤、对象键前缀/后缀过滤，以及带指数退避的自动重试。通知持久化到投递队列，提供至少一次投递保证和 3 天 TTL。

### 集群架构

StoreFS集群由多个节点组成，节点之间通过gossip协议进行通信，同时使用基于 Raft 的 leader 选举层来协调集群级别的单例服务（审计日志分区清理、生命周期扫描器）：

- **Gossip 协议**：节点发现、成员管理及污点状态传播，基于 HashiCorp Memberlist
- **Raft Leader 选举**：基于共识的单一 leader 运行集群级服务；失去 quorum 则无 leader（防止 split-brain）
- **动态节点管理**：支持自由添加/删除节点，无需停机维护
- **数据分布**：对象数据会根据策略分布到多个节点上
- **容错机制**：节点故障时，数据会自动恢复
- **负载均衡**：请求会自动分发到可用节点

![](docs/pics/arch_cn.jpg)

## 安装与部署

### 1. 配置文件详解

StoreFS使用YAML格式的配置文件（config.yaml），以下是配置项的详细说明：

```yaml
cluster:
  name: mycluster              # 集群名称，所有节点必须使用相同的名称
  db:                          # 数据库配置（使用StarRocks作为元数据存储）
    host: "127.0.0.1"          # 数据库主机地址
    port: 9030                 # MySQL查询端口
    user: "root"               # 数据库用户名
    password: ""               # 数据库密码
    database: "mydb"           # 数据库名称
    timeout: 10s               # 连接超时时间
  node:                        # 当前节点配置
    name: node1                # 节点名称，必须唯一
    num: 1                     # 节点编号，必须唯一
    ip: 127.0.0.1              # 节点IP地址
    port: 7946                 # 复用端口。admin rest api，admin web console, 以及节点通信端口（gossip协议）都使用这个端口
    internal_port: 17946       # 节点间文件操作的内部端口
    disks:                     # 节点的磁盘配置
      - path: /path/to/disk1   # 磁盘路径
        weight: 1              # 磁盘权重，用于数据分布策略
      - path: /path/to/disk2
        weight: 1
    s3:                        # S3 API配置
      host: 127.0.0.1          # S3 API主机地址
      port: 8901               # S3 API端口
  seeds:                       # 集群种子节点列表（用于节点发现）
    - 127.0.0.1:7946
    - 127.0.0.1:7947
    - 127.0.0.1:7948
```

### 2. 物理机/云虚拟机部署

#### 步骤1：部署数据库

StoreFS使用StarRocks作为元数据存储，需要先部署StarRocks：

```bash
# 下载并启动StarRocks（单节点部署）
url: https://www.starrocks.io/download/community/index.html

tar -xzf StarRocks-<versiion>.tar.gz
cd StarRocks-<version>

# 启动FE（Frontend）
./fe/bin/start_fe.sh --daemon

# 启动BE（Backend）
./be/bin/start_be.sh --daemon

# 初始化元数据（使用MySQL客户端连接）
mysql -h db -P9030 -uroot < /init.sql
```

#### 步骤2：准备配置文件

为每个节点创建配置文件（如config1.yaml、config2.yaml等），确保每个节点的`node.name`和`node.num`唯一。

#### 步骤3：启动StoreFS节点

```bash
# 下载对应平台的StoreFS二进制文件
例如 storefs_linux_x86_64

# 启动节点1
./storefs_linux_x86_64 -config config1.yaml

# 启动节点2（在另一个终端）
./storefs_linux_x86_64 -config config2.yaml

# 启动节点3（在另一个终端）
./storefs_linux_x86_64 -config config3.yaml
```

### 3. Linux二进制版本说明

提供两个Linux二进制版本：

#### `storefs_linux`（标准版）
- **运行要求**：无特殊依赖！可在任何Linux系统上运行
- **功能**：仅普通S3功能
- **适用场景**：不需要RDMA或目标系统缺少libibverbs时

#### `storefs_linux_rdma`（RDMA版）
- **重要警告**：如果目标系统没有安装 `libibverbs`，程序会立即崩溃！
- **运行要求**：目标系统必须安装 `libibverbs`
- **功能**：get、put和multipart upload均支持RDMA + 普通S3功能
- **适用场景**：需要高性能RDMA数据传输时

**检查系统是否有libibverbs**：
```bash
ldconfig -p | grep libibverbs
```
如果有输出，说明系统有libibverbs，可以使用`storefs_linux_rdma`！

### 4. Docker Compose部署

StoreFS提供了Docker Compose部署方式，快速启动一个3节点集群：

```bash
# 为Docker Volumes创建目录
./create_dirs.sh
```

```bash
# 启动Docker Compose
docker-compose up -d
```

Docker Compose会自动启动：
- 1个StarRocks数据库容器
- 3个StoreFS节点容器
- 端口映射：节点1(7946/8901)、节点2(7947/8902)、节点3(7948/8903)

```bash
# 停止Docker Compose
docker-compose stop
```

```bash
# 清除Docker Compose容器
docker-compose down
rm -rf configs/db-init/
```

### 4. RDMA支持（仅Linux）

StoreFS支持RDMA（远程直接内存访问）用于高性能数据传输，get、put和multipart upload均支持RDMA。RDMA绕过操作系统内核和TCP/IP协议栈，实现极低延迟和极高吞吐量的数据传输。

RDMA支持的主要特性：
- GetObject、PutObject和multipart UploadPart均支持RDMA
- RDMA READ用于put和multipart upload操作（服务器直接从客户端内存读取）
- RDMA WRITE用于get操作（服务器直接写入客户端内存）
- WebSocket控制通道用于RDMA连接建立
- 零拷贝数据传输
- 支持硬件RDMA和Soft-RoCE（软件仿真）

**注意**：RDMA支持仅适用于Linux。在macOS、Windows或其他操作系统上无法使用。

有关RDMA设置、配置和使用的详细文档，请参考：
- [RDMA 文档](docs/rdma_cn.md) - 详细的中文RDMA文档

## 管理控制台

### 管理控制台介绍

StoreFS提供了一个基于Vue.js的Web管理控制台，位于`web`目录下。控制台提供了直观的用户界面，用于管理用户、策略、桶和对象。

### 访问方式

访问`http://localhost:7946/console`，默认管理员账户为：
- 用户名：admin
- 密码：admin123
- 角色：`super_admin`（超级管理员）

### 功能特性

| 功能模块 | 描述                | 截图 |
|------|-------------------|----------|
| 用户管理 | 创建/编辑/删除用户，管理访问密钥 | [登录](docs/pics/login.jpg), [用户列表](docs/pics/user.jpg), [组列表](docs/pics/grouplist.jpg), [MFA](docs/pics/mfa.jpg)|
| 策略管理 | 创建/编辑/删除策略，配置权限规则 | [策略列表](docs/pics/policy.jpg) |
| 桶管理  | 创建/编辑/删除桶，配置访问策略，开启/关闭加密 | [桶列表](docs/pics/bucket.jpg) |
| | | [添加桶（版本）](docs/pics/versionBucket.jpg) |
| 对象管理 | 上传/下载/删除对象，预览文件内容 | [对象列表](docs/pics/object.jpg), [对象信息](docs/pics/objectinfo.jpg) |
| 版本对象管理 | 上传/下载/删除对象，预览文件内容 | [对象（版本）列表](docs/pics/versionObjectList.jpg), [版本列表](docs/pics/versionList.jpg) |
| 分块管理 | 完成/取消 | [分块列表](docs/pics/multipart.jpg), [分块信息](docs/pics/partdetail.jpg), [分块分片信息](docs/pics/partfragment.jpg) |
| 任务管理 | 创建任务，查看 运行中/历史 任务 | [任务列表](docs/pics/tasklist.jpg), [任务详细信息](docs/pics/taskdetail.jpg) |
| 节点管理 | 查看节点状态，添加/删除节点    | [节点列表](docs/pics/node.jpg) |
| 国际化  | 切换语言              | [国际化](docs/pics/internationalization.jpg) |

## 用户角色与用户组

### 概要介绍

StoreFS 使用基于角色的访问控制（RBAC）并结合用户组进行权限隔离。用户属于某个用户组，并拥有 `user`、`group_admin` 或 `super_admin` 三种角色之一。用户组可用于按团队或租户委派管理权限，并可配置默认策略，供组内用户创建桶且未显式选择策略时使用。

### 角色权限

| 角色 | 管理控制台 / Admin API 权限 | S3 API 与对象数据权限 | 作用范围 |
|------|----------------------------|-----------------------|----------|
| `user` | 管理自己的桶、对象、个人资料和访问密钥 | 可按桶策略访问自己拥有的桶 | 当前用户及其拥有的桶 |
| `group_admin` | 管理同组内的用户、桶、对象和用户组设置；可在组内分配 `user` 和 `group_admin` 角色 | 可按桶策略访问同组用户拥有的桶 | 当前用户组 |
| `super_admin` | 管理所有用户组、用户、策略、桶、节点和全局系统资源；可创建或分配 `super_admin` 用户 | 不能直接访问 S3 API 桶或读写对象数据；数据操作应委派给普通用户或用户组管理员 | 全局系统范围 |

> 注意：桶策略仍然控制具体的 S3 操作，例如读、写、列出和删除。角色用于定义管理边界以及用户可以操作的桶所有权范围。

## ACL（访问控制列表）

StoreFS 支持与 S3 兼容的 ACL 用于桶级别权限管理，为不同用户提供细粒度的访问控制。ACL 定义了五种权限（FULL_CONTROL、WRITE、READ、READ_ACP、WRITE_ACP），支持三种授权对象类型（CanonicalUser、AllUsers、AuthenticatedUsers）。可通过 S3 XML API（GetBucketAcl / PutBucketAcl）或 Admin JSON API 进行管理。

详细信息，请参考：[ACL 文档](docs/acl_cn.md)

## 标签管理（Tagging）

StoreFS 支持与 S3 兼容的桶和对象标签功能。标签是键值对，可用于分类、访问控制和成本跟踪。桶最多支持 50 个标签，对象最多支持 10 个标签。启用版本控制时标签是版本感知的，可通过 S3 XML API（GetBucketTagging / PutBucketTagging / DeleteBucketTagging）进行管理。复制操作可通过 `x-amz-tagging-directive` 请求头控制标签行为，分块上传初始化时也可指定标签。

详细信息，请参考：[标签文档](docs/tagging_cn.md)

## S3 API

### 概要介绍

StoreFS实现了S3 API的核心功能，兼容AWS S3的客户端和工具。您可以使用AWS CLI、S3 SDK或其他支持S3协议的工具与StoreFS进行交互。

### 已实现的API接口

详细的API接口文档请参考：[S3 API 文档](docs/s3_cn.md)

主要实现的API接口包括：

- **桶操作**：创建桶、列出桶、删除桶
- **对象操作**：上传对象、下载对象、删除对象、列出对象
- **分块操作**：创建分块上传、上传分块、完成分块上传、取消分块上传、列出分块、列出分块上传
- **版本控制操作**：获取桶版本控制状态、设置桶版本控制状态
- **对象锁定操作**：获取桶对象锁定配置、获取对象保留配置
- **标签操作**：获取/设置/删除桶标签，获取/设置/删除对象标签（版本感知）
- **S3 Select 操作**：使用 SQL 查询对象内容（CSV/JSON 输入输出，GZIP 解压缩）
- **ACL 操作**：获取/设置桶 ACL（S3 XML API）
- **事件通知**：对象创建和删除操作会自动触发事件并投递到已配置的 Webhook 端点（详见[通知文档](docs/notification_cn.md)）

### 公共URI读取

StoreFS支持对标记为公开的桶中的对象进行公开读取访问。当桶设置为公开时，可以通过HTTP GET请求直接访问对象，无需身份验证。

#### 启用公开访问

可以在创建桶或稍后更新时将桶标记为公开：
- 管理API：在桶创建/更新请求中将`isPublic`字段设置为`true`
- 管理控制台：在桶设置中切换"公开"开关

#### 公共URI格式

可以使用路径样式或虚拟主机样式URI访问公共桶中的对象：

**路径样式**：
```
http://<s3主机>:<s3端口>/<桶名称>/<对象键>
```
示例：`http://127.0.0.1:8901/my-bucket/documents/report.pdf`

**虚拟主机样式**：
```
http://<桶名称>.<s3主机>:<s3端口>/<对象键>
```
示例：`http://my-bucket.127.0.0.1:8901/documents/report.pdf`

#### 工作原理

当StoreFS接收到对象的GET请求时：
1. 如果桶是公开的，则无需身份验证直接提供对象
2. 如果桶不是公开的，则回退到正常的身份验证检查

公开访问仅适用于对象的GET请求。所有其他操作（上传、删除、列出等）仍需要适当的身份验证和授权。

## Admin API

### 概要介绍

StoreFS提供了一套RESTful Admin API，用于管理系统的用户、策略、桶和节点。这些API主要用于Web管理控制台和自动化运维。

### 已实现的API接口

详细的API接口文档请参考：[Admin API 文档](docs/admin-api_cn.md)

主要实现的API接口包括：

- **认证**：登录、登出、修改密码
- **用户管理**：创建/删除用户、修改用户信息、管理访问密钥
- **策略管理**：创建/删除策略、修改策略内容
- **用户组管理**：创建/更新/删除用户组，配置默认策略
- **桶管理**：创建/删除桶、修改桶属性、列出桶内容（支持 ACL 感知的权限字段：`userPermission`、`canReadAcl`、`canWrite`）
- **对象管理**：管理桶中的对象、获取对象元数据、列出对象版本
- **分块管理**：列出、获取、完成、取消分块上传；获取分块片段信息
- **桶 ACL 管理**：通过 JSON API 获取/设置桶 ACL
- **桶通知管理**：创建/更新/删除/列出桶 Webhook 通知，测试 Webhook 端点
- **KMS 管理**：管理多个 KMS 服务配置、测试 KMS 连接、检查 KMS 健康状态、创建/列出/更新/删除/轮转 KMS 密钥
- **节点管理**：查看节点状态、获取污点状态、更新污点状态、查询 Raft leader
- **任务管理**：列出任务类型、创建/取消/清理后台任务
- **健康检查**：检查集群健康状态

## s3file CLI

### 概要

s3file 是一个用于与 S3 兼容存储服务交互的命令行工具，支持交互式和静默两种模式。它适用于 StoreFS、MinIO、AWS S3 以及所有 S3 兼容服务。

### 功能特性

- **交互式 Shell 模式**：像操作本地文件系统一样浏览 S3 存储
- **静默模式**：编程方式执行命令
- **多供应商支持**：适用于 StoreFS、MinIO、AWS S3 以及所有 S3 兼容服务
- **分页支持**：轻松浏览大型目录
- **命令历史**：浏览之前执行过的命令
- **自动补全**：命令的 Tab 补全功能
- **通配符支持**：使用 * 和 ? 进行模糊匹配

### 文档

详细文档请参考：[s3file CLI 文档](docs/s3file_cn.md)

## Gateway (NFS/SMB)

### 概要介绍

StoreFS 提供 NFSv3 和 SMB 3.1.1 协议网关，让您可以将 S3 存储桶挂载为标准 POSIX 文件系统进行访问。这使得遗留应用程序、文件服务器和操作系统无需任何 S3 SDK 集成即可与 StoreFS 对象存储进行交互。

### 主要特性

- **NFSv3 网关**：完整的 NFSv3 协议实现，支持 MOUNT 协议，支持 `none`、`sys` 和 `krb5` 认证
- **SMB 3.1.1 网关**：完整的 SMB 3.1.1 协议，兼容 SMB 2.1，支持 NTLMSSP 认证和访客访问
- **统一 VFS 层**：两个网关共享通用的虚拟文件系统，将 POSIX 操作转换为 S3 对象操作
- **S3 ↔ NFS/SMB 互操作**：通过一个协议写入的数据可立即通过其他协议访问
- **用户映射**：NFS/SMB 客户端身份映射到 StoreFS 用户进行认证和授权
- **只读模式**：两个网关均可配置为只读导出
- **目录模拟**：S3 扁平命名空间通过零字节标记对象呈现为层次化文件系统

### 配置示例

```yaml
gateway:
  nfs:
    enabled: true
    port: 2049
    export_path: "/"
    auth_type: "none"
    map_user: "user"
  smb:
    enabled: true
    port: 4445
    share_name: "storefs"
    guest_allowed: true
    map_user: "user"
```

### 快速挂载

```bash
# NFS 挂载（Linux）
sudo mount -t nfs -o vers=3,port=2049,noresvport <storefs-host>:/ /mnt/storefs

# SMB 挂载（Linux）
sudo mount -t cifs //<storefs-host>:4445/storefs /mnt/storefs \
  -o username=<user>,password=<pass>,vers=3.1.1
```

### 文档

详细的架构、配置、使用和故障排除信息，请参考：[Gateway (NFS/SMB) 文档](docs/gateway_cn.md)

## 监控

StoreFS 提供了基于 Prometheus + Grafana + Alertmanager 的完整监控与告警系统。

### 主要功能

- **指标采集**：每个 StoreFS 节点暴露 `/metrics` 端点，包含系统级指标（CPU、内存、磁盘、网络）、操作计数器（对象上传/下载、分块上传、碎片操作）和 Go 运行时指标。
- **热点桶检测**：基于滑动窗口算法（2 分钟窗口）实时跟踪热点桶 Top-K，覆盖上传、下载、UploadPart 和 CompleteMultipart 操作。
- **预置 Grafana 仪表盘**：包含两个预配置仪表盘——单节点详细视图和集群汇总视图。
- **告警规则**：预定义的 Prometheus 告警规则，涵盖节点宕机、磁盘使用率、CPU/内存阈值和 goroutine 数量。
- **通知渠道**：Alertmanager 配置包含 Slack、Email 和 Webhook 通知模板（默认全部关闭，按需启用）。

### 文档

详细信息请参考：[监控指南](docs/metrics_cn.md)

## MCP for AI Agent

StoreFS 提供了 [MCP（模型上下文协议）](https://modelcontextprotocol.io) 服务器，让 AI 助手（特别是 **Claude Code**）能够通过自然语言管理集群。您无需记忆 API 端点和请求格式，只需描述您的需求即可。

### 主要功能

- **自然语言管理**：通过对话管理用户、分组、桶、策略和对象
- **40+ 工具**：六大类工具覆盖集群管理、用户管理、存储策略、桶操作、对象管理和 S3 数据操作
- **自动语言检测**：根据输入自动在英文和中文间切换响应语言
- **安全认证**：管理员操作使用 Bearer token，S3 数据操作使用 AWS Signature V4
- **文件操作**：通过自然语言指令直接上传、下载和复制文件

### 前提条件

- **Node.js >= 18**
- **StoreFS v0.3.7 及以上版本**

### 文档

详细信息请参考：[MCP 服务器指南](docs/mcp_cn.md)

## 通知系统

StoreFS 提供基于 Webhook 的事件通知系统，当桶中的对象被创建或删除时，自动向已配置的端点发送 HTTP POST 请求。该系统与 AWS S3 事件通知兼容。

### 主要特性

- **实时事件**：自动检测并分发对象创建（PutObject、CopyObject、CompleteMultipartUpload）和删除事件
- **可配置事件类型**：按特定事件类型过滤（如 `s3:ObjectCreated:Put`、`s3:ObjectRemoved:Delete`）或使用通配符（`s3:ObjectCreated:*`、`*`）
- **对象键过滤**：使用前缀和后缀过滤，限制哪些对象触发通知
- **两种负载格式**：原生格式（简化的 StoreFS 格式）和 AWS S3 兼容格式
- **HMAC-SHA256 签名**：可选的密钥签名，用于 Webhook 端点验证
- **自动重试**：失败投递使用指数退避重试（1秒 → 5秒 → 30秒 → 5分钟 → 30分钟）
- **持久化队列**：事件持久化到数据库表，提供至少一次投递保证
- **MCP 集成**：通过自然语言创建和管理通知

### 文档

详细信息请参考：[通知系统文档](docs/notification_cn.md)

## 审计日志

StoreFS 提供完整的审计日志系统，记录集群上所有管理操作和 S3 数据操作。每个 API 请求都会被捕获，包含详细的元数据，包括操作者、操作的资源、时间、客户端 IP、HTTP 状态码和处理耗时。

### 主要特性

- **自动捕获**：每个 Admin API 和 S3 API 请求都会自动记录，无需修改应用程序
- **丰富的元数据**：记录用户身份、操作类型、资源、客户端 IP、状态码、耗时、请求 ID 等
- **多输出目标**：支持数据库（StarRocks）、syslog（本地/远程）和文件输出
- **异步处理**：审计条目通过缓冲通道在后台处理，从不阻塞请求处理
- **批量插入**：数据库输出使用批量插入（100 条或 1 秒窗口），高效存储
- **请求追踪**：每个请求携带唯一的 `X-Request-ID` 头部，用于跨集群的分布式追踪
- **可配置过滤**：排除健康检查或设置最小耗时阈值，减少噪音
- **自动清理**：基于日期的分区存储，可配置保留期和自动清理
- **操作检测**：自动从 HTTP 方法和路径检测 50+ 种 Admin API 操作和 30+ 种 S3 操作
- **Snowflake ID**：每个审计条目使用全局唯一、可按时间排序的 ID

### 文档

详细信息请参考：[审计日志文档](docs/auditlog_cn.md)

## 任务系统

StoreFS 提供了任务系统用于在集群上执行后台管理操作，例如修复损坏的文件片段和替换故障磁盘。

### 任务类型

- **修复任务**（`repair`）：扫描并修复指定节点上损坏或丢失的文件片段。支持副本和纠删码（EC）两种策略——自动从健康节点重建数据。
- **替换磁盘任务**（`replacedisk`）：将旧磁盘上的所有数据迁移到同一节点的新磁盘上。用于物理磁盘故障或需要升级时。

### 主要特性

- **后台执行**：任务在后台异步执行，支持进度追踪
- **按节点并发**：不同节点上的同类型任务可以同时运行
- **支持取消**：运行中的任务可以安全取消
- **仅超级管理员**：修复和替换磁盘操作需要 `super_admin` 角色
- **MCP 集成**：通过自然语言创建和管理任务

### 文档

详细信息请参考：[任务系统文档](docs/task_cn.md)

## 快速开始

### 1. 使用 s3file CLI 连接

```bash
# 启动交互式模式（默认连接到 localhost:8901）
s3file

# 列出所有存储桶
s3file --silent --command 'buckets'

# 使用静默模式创建存储桶并上传文件
s3file --silent --command 'mb mybucket' --command 'cd s3://mybucket' --command 'upload localfile.txt remote.txt'

# 使用静默模式下载文件
s3file --silent --command 'cd s3://mybucket' --command 'download remote.txt localfile.txt'
```

更详细的使用方法请参考：[s3file CLI 文档](docs/s3file_cn.md)

### 2. 使用AWS CLI连接

```bash
# 配置AWS CLI
aws configure --profile storefs
AWS Access Key ID [None]: <your-ak>
AWS Secret Access Key [None]: <your-sk>
Default region name [None]: us-east-1
Default output format [None]: json

# 列出所有桶
aws s3 ls --endpoint-url http://127.0.0.1:8901 --profile storefs

# 创建桶
aws s3 mb s3://mybucket --endpoint-url http://127.0.0.1:8901 --profile storefs

# 上传文件
aws s3 cp localfile.txt s3://mybucket/ --endpoint-url http://127.0.0.1:8901 --profile storefs

# 下载文件
aws s3 cp s3://mybucket/localfile.txt . --endpoint-url http://127.0.0.1:8901 --profile storefs
```

### 3. 使用管理控制台

1. 访问`http://localhost:7946/console`
2. 使用默认管理员账户登录（用户名：admin，密码：admin123，角色：`super_admin`）
3. 创建用户、策略和桶
4. 管理您的存储资源

## 技术支持

如果您在使用StoreFS过程中遇到问题，请参考：

1. [FAQ文档](docs/faq_cn.md) - 常见问题解答
2. [故障排除](docs/troubleshooting_cn.md) - 常见问题排查
3. [GitHub Issues](https://github.com/bidzhao/storefs/issues) - 提交问题报告

## 许可证

您可以自由使用和分发本软件，但需要保留原作者的版权声明和许可信息。
