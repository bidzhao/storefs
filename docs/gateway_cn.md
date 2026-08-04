**[English](gateway.md)**

# Gateway (NFS/SMB)

StoreFS 提供 NFSv3 和 SMB 3.1.1 协议网关，让您可以将 S3 存储桶挂载为标准 POSIX 文件系统进行访问。这使得遗留应用程序、文件服务器和操作系统无需任何 S3 SDK 集成即可与 StoreFS 对象存储进行交互。

## 架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        StoreFS 节点                              │
│                                                                │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌─────────────┐ │
│  │ NFSv3    │   │ SMB 3.1.1│   │ S3 API   │   │ Admin API   │ │
│  │ 网关     │   │ 网关     │   │ 服务器   │   │ 服务器      │ │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘   └──────┬──────┘ │
│       │              │              │                 │         │
│       └──────────────┴──────────────┴─────────────────┘         │
│                              │                                  │
│                    ┌─────────▼──────────┐                       │
│                    │   Gateway VFS      │                       │
│                    │  (S3FS 适配层)     │                       │
│                    └─────────┬──────────┘                       │
│                              │                                  │
│                    ┌─────────▼──────────┐                       │
│                    │  Store 分发器      │                       │
│                    └─────────┬──────────┘                       │
│                              │                                  │
│              ┌───────────────┴───────────────┐                   │
│              │                               │                  │
│         ┌────▼────┐                    ┌─────▼─────┐            │
│         │  磁盘 1 │       ......       │  磁盘 N   │            │
│         └─────────┘                    └───────────┘            │
└─────────────────────────────────────────────────────────────────┘
```

### 工作原理

NFS 和 SMB 网关共享一个通用的 **VFS（虚拟文件系统）层**，将 POSIX 文件系统操作转换为 S3 对象操作：

1. **POSIX → S3 转换**：文件路径如 `/mybucket/dir/file.txt` 被解析为桶名称（`mybucket`）和对象键（`dir/file.txt`）。
2. **目录模拟**：S3 是扁平对象存储，目录通过零字节标记对象（如 `dir/`）来模拟实现。
3. **共享缓存**：目录列表缓存（5 秒 TTL）减少重复的 S3 列表操作。
4. **用户映射**：所有 NFS/SMB 客户端请求被映射到特定的 StoreFS 用户进行认证和授权。

### 核心优势

- **零集成**：遗留应用程序无需任何代码更改即可访问对象存储
- **统一命名空间**：同一数据可通过 S3 API、NFS 和 SMB 同时访问
- **一致性**：通过一个协议做出的更改立即通过其他协议可见
- **熟悉工具**：使用标准 POSIX 命令（`cp`、`mv`、`ls`、`cat`、`find`）管理对象

## NFS 网关

### 概述

NFS 网关实现了 **NFSv3 协议**，支持 MOUNT 协议。它将 StoreFS 存储桶作为标准 NFS 导出，任何 NFSv3 兼容客户端都可以挂载访问。

### 功能特性

- **NFSv3 协议**：完整的 NFSv3 RPC 实现，支持 MOUNT 协议
- **多种认证类型**：支持 `none`（无认证）、`sys`（AUTH_UNIX）和 `krb5`（Kerberos）
- **用户映射**：将 NFS 客户端凭证映射到配置的 StoreFS 用户
- **导出路径控制**：限制客户端挂载到特定子路径
- **只读模式**：将文件系统导出为只读
- **连接限制**：可配置的最大并发客户端连接数
- **文件句柄缓存**：基于 LRU 的文件句柄缓存，可配置限制
- **对象读取缓存**：512 MB 默认全对象读取缓存

### 配置

```yaml
gateway:
  nfs:
    enabled: true                 # 启用 NFS 网关
    port: 2049                    # NFS 服务端口（NFSv3 + MOUNT 协议）
    export_path: "/"              # NFS 客户端挂载的导出根路径
    auth_type: "none"             # 认证类型："none" | "sys" | "krb5"
                                  #   - none: 无认证，所有客户端使用 map_user
                                  #   - sys: UNIX 认证（AUTH_UNIX）
                                  #   - krb5: Kerberos 认证（需要 RPCSEC_GSS 支持）
    map_user: "user"              # NFS 客户端映射的 StoreFS 用户
    map_uid: 1000                 # 向 NFS 客户端暴露的 UID
    map_gid: 1000                 # 向 NFS 客户端暴露的 GID
    uid_mappings: {}              # UID → StoreFS 用户名映射（用于 auth_type=sys）
    read_only: false              # true 禁用写入/删除操作
    max_connections: 1024         # 最大缓存的 NFS 客户端连接数（LRU 淘汰）
```

### 挂载 NFS

#### Linux

```bash
# 安装 nfs-common（如未安装）
sudo apt-get install nfs-common   # Debian/Ubuntu
sudo yum install nfs-utils        # CentOS/RHEL

# 挂载 NFS 导出
sudo mount -t nfs -o vers=3,port=2049,noresvport <storefs-host>:/ /mnt/storefs

# 使用特定导出路径挂载
sudo mount -t nfs -o vers=3,port=2049 <storefs-host>:/exports /mnt/storefs

# 验证
df -h /mnt/storefs
ls -la /mnt/storefs/
```

**注意**：建议使用 `noresvport` 选项以获得更好的 NFS 重连行为。

#### macOS

```bash
# 通过 Finder 或命令行挂载
mkdir -p /mnt/storefs
mount_nfs -o vers=3,port=2049,mountport=2049 <storefs-host>:/ /mnt/storefs
```

#### Windows

Windows NFS 客户端支持 NFSv3：

```bash
# 启用 NFS 客户端（Windows 功能 → Services for NFS）
mount -o nolock -o mtype=hard \\<storefs-host>\ /mnt/storefs
```

### 使用示例

```bash
# 列出存储桶（顶级目录）
ls /mnt/storefs/

# 列出存储桶中的对象
ls /mnt/storefs/mybucket/
ls -la /mnt/storefs/mybucket/

# 创建新目录（创建零字节标记对象）
mkdir /mnt/storefs/mybucket/projects

# 通过 NFS 上传文件（等同于 S3 PutObject）
cp report.pdf /mnt/storefs/mybucket/projects/

# 通过 NFS 下载文件（等同于 S3 GetObject）
cp /mnt/storefs/mybucket/projects/report.pdf .

# 读取文件内容
cat /mnt/storefs/mybucket/readme.txt

# 查看文件属性
stat /mnt/storefs/mybucket/file.txt

# 删除文件（等同于 S3 DeleteObject）
rm /mnt/storefs/mybucket/old-file.txt

# 复制/移动文件（NFS 重命名）
mv /mnt/storefs/mybucket/file.txt /mnt/storefs/mybucket/renamed.txt
cp /mnt/storefs/mybucket/file.txt /mnt/storefs/mybucket/copy.txt

# 按名称查找文件
find /mnt/storefs/mybucket/ -name "*.pdf"

# 检查磁盘使用量
du -sh /mnt/storefs/mybucket/
```

## SMB 网关

### 概述

SMB 网关实现了 **SMB 3.1.1 协议**（兼容 SMB 2.1）。它将 StoreFS 存储桶导出为 SMB 共享，可以被 Windows、Linux 和 macOS 客户端挂载访问。

### 功能特性

- **SMB 3.1.1 协议**：完整的 SMB 3.1.1 实现，兼容 SMB 2.1
- **NTLM 认证**：基于 NTLMSSP 的用户名/密码认证
- **访客访问**：可选的匿名/访客访问（映射到配置的 StoreFS 用户）
- **共享级别导出**：单个共享名称（默认 `storefs`）导出整个 StoreFS 命名空间
- **只读模式**：将共享导出为只读
- **工作组支持**：可配置的 NetBIOS 工作组/域名
- **并发连接**：可配置的最大并发客户端连接数
- **缓冲池**：64 KB 缓冲池，实现高效数据传输

### 配置

```yaml
gateway:
  smb:
    enabled: true                     # 启用 SMB 3.1.1 网关
    port: 4445                        # SMB 服务端口（445 需要 root 权限）
    server_name: "STOREFS"            # 向客户端通告的 NetBIOS 服务器名称
    workgroup: "WORKGROUP"            # NTLMSSP 认证的工作组/域名
    share_name: "storefs"             # 客户端挂载的共享名称
    guest_allowed: true               # 允许匿名/访客访问
    read_only: false                  # 将共享导出为只读
    map_user: "user"                  # 访客/匿名连接映射的 StoreFS 用户
    max_connections: 1024             # 最大并发客户端连接数
```

### 挂载 SMB

#### Linux

```bash
# 安装 cifs-utils（如未安装）
sudo apt-get install cifs-utils    # Debian/Ubuntu
sudo yum install cifs-utils        # CentOS/RHEL

# 使用凭据挂载
sudo mount -t cifs //<storefs-host>:4445/storefs /mnt/storefs \
  -o username=<smb-user>,password=<smb-pass>,vers=3.1.1

# 使用 SMB 2.1 回退
sudo mount -t cifs //<storefs-host>:4445/storefs /mnt/storefs \
  -o username=<smb-user>,password=<smb-pass>,vers=2.1

# 以访客身份挂载
sudo mount -t cifs //<storefs-host>:4445/storefs /mnt/storefs \
  -o guest,vers=3.1.1

# 验证
df -h /mnt/storefs
ls -la /mnt/storefs/
```

#### macOS

```bash
# 通过 Finder 连接
# 前往 → 连接服务器（Cmd+K）
# 输入：smb://<storefs-host>:4445/storefs

# 或通过命令行
mkdir -p /mnt/storefs
mount_smbfs //<smb-user>:<smb-pass>@<storefs-host>:4445/storefs /mnt/storefs
```

#### Windows

```bash
# 通过命令行映射网络驱动器
net use Z: \\<storefs-host>\storefs /user:<smb-user> <smb-pass>

# 或通过文件资源管理器
# \\<storefs-host>\storefs
```

### 使用示例

```bash
# 列出存储桶（顶级目录）
ls /mnt/storefs/

# 列出存储桶中的对象
ls /mnt/storefs/mybucket/

# 创建目录
mkdir /mnt/storefs/mybucket/data

# 上传文件
cp data.csv /mnt/storefs/mybucket/data/

# 下载文件
cp /mnt/storefs/mybucket/data.csv .

# 读取文件内容
cat /mnt/storefs/mybucket/readme.txt

# 删除文件
rm /mnt/storefs/mybucket/old-data.csv

# 重命名/移动文件
mv /mnt/storefs/mybucket/data.csv /mnt/storefs/mybucket/archive/data.csv
```

## S3 ↔ NFS/SMB 互操作

网关的一个关键特性是：通过一个协议写入的数据可以立即通过其他协议访问。这实现了强大的混合工作流：

### S3 → NFS/SMB 工作流

通过 S3 API（PutObject、MultipartUpload）上传的对象立即可以通过 NFS 或 SMB 挂载点可见和读取：

```bash
# 步骤 1：通过 S3 API 上传（使用 AWS CLI）
aws s3 cp report.pdf s3://mybucket/reports/report.pdf --endpoint-url http://127.0.0.1:8901

# 步骤 2：通过 NFS 读取（立即可见）
cat /mnt/storefs/mybucket/reports/report.pdf
```

### NFS/SMB → S3 工作流

通过 NFS 或 SMB 挂载点写入的文件立即可以通过 S3 API 访问：

```bash
# 步骤 1：通过 NFS 写入
cp data.csv /mnt/storefs/mybucket/smb-nfs/data.csv

# 步骤 2：通过 S3 API 下载（立即可用）
aws s3 cp s3://mybucket/smb-nfs/data.csv . --endpoint-url http://127.0.0.1:8901
```

### 使用场景

| 使用场景 | 描述 | 推荐协议 |
|----------|------|----------|
| 遗留应用迁移 | 仅支持文件系统访问的应用程序 | NFS（Linux）/ SMB（Windows） |
| 媒体流传输 | 通过文件系统向媒体播放器提供视频/音频文件 | NFS |
| CI/CD 流水线 | 在 S3 和构建服务器之间共享构建产物 | SMB |
| 数据备份 | 写入网络共享的备份软件 | SMB |
| 跨平台文件共享 | 使用不同操作系统的团队 | SMB |
| 高吞吐数据处理 | 使用标准 POSIX 工具进行大文件传输 | NFS |
| 混合云存储 | 通过 S3 API 和文件系统均可访问的数据 | 两者 |

## 认证机制

### NFS 认证

| 认证类型 | 描述 |
|----------|------|
| `none` | 无认证。所有客户端都映射到配置的 `map_user`。 |
| `sys` | AUTH_UNIX 认证。客户端提供 UID/GID。目前所有客户端仍然映射到配置的 StoreFS 用户，`uid_mappings` 保留用于未来的按 UID 用户映射。 |
| `krb5` | Kerberos 5 认证。已声明但需要 RPCSEC_GSS 支持，当前尚不支持。 |

### SMB 认证

| 认证方法 | 描述 |
|----------|------|
| NTLMSSP | 使用用户名和密码的标准 NTLM 认证。凭据会针对 StoreFS 用户进行验证。 |
| 访客 | 匿名访问。启用后，访客连接映射到配置的 `map_user`。 |

### 通用用户映射

两个网关都将外部客户端身份映射到内部 StoreFS 用户：

1. **NFS**：所有 NFS 客户端（无论认证类型）都映射到 `nfs` 部分配置的单个 `map_user`。
2. **SMB**：经过认证的 SMB 用户会针对 StoreFS 用户凭据进行验证。访客连接映射到 `smb` 部分中的 `map_user`。

映射的 StoreFS 用户决定：
- 客户端可以访问哪些存储桶（基于用户的桶所有权和权限）
- 基于桶策略的操作授权（读/写/删除）

## 性能考虑

- **目录列表缓存**：5 秒 TTL 缓存减少目录列表的重复 S3 LIST 操作。
- **对象读取缓存**：NFS 网关包含 512 MB 的全对象读取缓存，提高重复读取性能。
- **缓冲池**：SMB 网关使用 64 KB 缓冲池减少内存分配开销。
- **并发连接**：两个网关都支持可配置的最大并发连接数，以控制资源使用。
- **NFS 句柄限制**：NFS 网关维护文件句柄的 LRU 缓存（默认 1024），实现高效的句柄重用。

## 限制

- **仅 NFSv3**：NFS 网关实现 NFSv3，而非 NFSv4。客户端必须使用 NFSv3（`vers=3`）。
- **无 NFSv4 ACL**：不支持 NFSv4 ACL。请使用 StoreFS 存储桶策略或 S3 ACL 进行访问控制。
- **目录模拟**：S3 是扁平键值存储。目录通过零字节标记对象模拟，可能与本机文件系统目录的行为不完全一致。
- **无硬链接支持**：不支持 POSIX 硬链接（S3 没有硬链接概念）。
- **无符号链接支持**：不支持 POSIX 符号链接（S3 没有符号链接概念）。
- **无文件锁定**：不支持 POSIX 文件锁定（`flock`、`fcntl`）。
- **无扩展属性**：不支持 POSIX 扩展属性（xattr）。
- **SMB 端口**：端口 445（标准 SMB）需要 root 权限。测试时请使用非特权端口（如 4445）。

## 故障排除

### NFS 挂载问题

**问题**：`mount.nfs: Connection refused`
**解决方案**：确保配置中启用了 NFS 网关，并且 StoreFS 节点正在运行。确认端口（默认 2049）未被防火墙阻止。

**问题**：`mount.nfs: access denied by server while mounting`
**解决方案**：检查 `export_path` 配置。客户端的挂载路径必须与导出路径匹配。

**问题**：访问文件时权限被拒绝
**解决方案**：确认 `map_user` 存在于 StoreFS 中，并且对目标存储桶具有适当的权限。

### SMB 挂载问题

**问题**：`mount error(13): Permission denied`
**解决方案**：验证 SMB 用户名和密码。确认该用户存在于 StoreFS 中且密码正确。

**问题**：`mount error(112): Host is down`
**解决方案**：确保 SMB 网关已启用且端口可访问。如果使用端口 445，请确认服务具有 root 权限。

**问题**：无法写入文件
**解决方案**：检查 SMB 配置中是否将 `read_only` 设置为 `true`。