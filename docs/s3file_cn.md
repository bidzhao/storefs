**[English](s3file.md)**

# s3file - S3 文件系统命令行工具

一个用于与 S3 兼容存储服务交互的命令行工具，支持交互式和静默两种模式。

## 功能特性

- **交互式 Shell 模式**：像操作本地文件系统一样浏览 S3 存储
- **静默模式**：编程方式执行命令，脚本友好的输出格式
- **本地文件操作**：使用 `--local` 参数在本地文件系统上执行命令
- **多供应商支持**：适用于 StoreFS、MinIO、AWS S3 以及所有 S3 兼容服务
- **分页支持**：轻松浏览大型目录，支持交互和非交互两种分页模式
- **命令历史**：浏览之前执行过的命令
- **自动补全**：命令的 Tab 补全功能，包含参数提示
- **通配符支持**：使用 `*` 和 `?` 进行模糊匹配
- **取消支持**：按 Ctrl+C 可随时取消正在进行的传输
- **实时进度**：上传和下载时显示实时进度条和百分比
- **批量操作**：递归目录上传/下载、批量重命名/移动、Dry-Run 预览

## 安装

```bash
# 直接运行
s3file --help
```

## 使用方法

### 交互式模式

```bash
# 启动交互式模式（默认连接到 localhost:8901）
s3file

# 连接到 AWS S3
s3file --endpoint https://s3.amazonaws.com --region us-east-1 --ssl

# 连接到 MinIO
s3file --endpoint play.min.io --access-key Q3AM3UQ867SPQQA43P2F --secret-key zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG --ssl
```

### 静默模式

静默模式专为脚本和自动化设计。多条命令可使用 `;` 分隔：

```bash
# 列出所有存储桶
s3file --silent --command 'buckets'

# 使用 ; 分隔执行多条命令
s3file --silent --connect localhost:9000 --command 'cd s3://mybucket; ls'

# 静默模式下 ls 使用分页（默认 page=1, pageSize=20）
s3file --silent --command 'cd s3://mybucket; ls --page 1 --pageSize 50'

# 退出码：成功返回 0，失败返回 1
s3file --silent --command 'buckets' && echo "Success"

# 使用静默模式上传文件
s3file --silent --command 'cd s3://mybucket; upload /path/to/local.txt remote.txt'

# 使用静默模式下载文件
s3file --silent --command 'cd s3://mybucket; download remote.txt /path/to/local.txt'
```

## 命令行选项

| 选项 | 描述 | 默认值 |
|--------|-------------|---------|
| `--help, -h` | 显示帮助信息 | - |
| `--silent` | 以静默模式运行（非交互式） | - |
| `--command <cmd>` | 在静默模式下执行的命令（使用 `;` 分隔多条命令） | - |
| `--connect <url>` | 在执行命令前连接到指定端点 | - |
| `--endpoint <url>` | S3 端点 URL | `localhost:8901` |
| `--region <region>` | AWS 区域 | `us-east-1` |
| `--access-key <key>` | 访问密钥 | `admin-ak` |
| `--secret-key <key>` | 秘密密钥 | `admin-sk` |
| `--ssl` | 使用 SSL/TLS 连接 | `false` |
| `--no-banner` | 不显示欢迎横幅 | - |

## StoreFS 探测

当连接到 StoreFS 服务器时，s3file 会自动检测（通过检查 S3 API 响应中的 `X-StoreFS-Version` 头），并启用优化功能：

### 快速目录列表

检测到 StoreFS 后，`ls` 命令会使用 **Admin API**（`/api/buckets/{name}/entries`）替代 S3 `ListObjectsV2` 来列出目录内容。因为 Admin API 直接查询元数据库而非扫描对象存储，所以列表性能显著提升。

快速路径自动启用，无需额外配置。如果 Admin API 不可用，s3file 会自动回退到标准 S3 `ListObjectsV2` 方式。

### 连接信息

使用 `info` 命令查看 StoreFS 探测状态：

```
no bucket selected> info
────────────────────────────────────────────────────────────
Endpoint:       http://127.0.0.1:8901
Region:         us-east-1
...
StoreFS:        Detected (version v0.5.1)
Admin API:      http://127.0.0.1:7946
Admin API:      Connected
────────────────────────────────────────────────────────────
```

如果服务器不是 StoreFS 实例，则显示：
```
StoreFS:        Not detected
```

## 隐藏文件

在 StoreFS 模式下，`ls` 默认隐藏内部系统文件（名称以 `_sys_` 开头的文件）。使用 `ls --all` 或 `ls -a` 可显示所有条目，包括系统文件。

## 环境变量

| 变量 | 描述 |
|----------|-------------|
| `AWS_ENDPOINT_URL` | S3 端点 URL |
| `AWS_REGION` | AWS 区域 |
| `AWS_ACCESS_KEY_ID` | 访问密钥 |
| `AWS_SECRET_ACCESS_KEY` | 秘密密钥 |

## 交互式命令

### 基本导航

| 命令 | 描述 |
|---------|-------------|
| `ls [--no-page] [--page <n>] [--pageSize <n>]` | 列出当前目录内容（默认分页） |
| `cd <directory>` | 改变目录（支持 `s3://bucket`、`..`、`/`） |
| `pwd` | 显示当前路径，加 `--local` 显示本地 CWD |
| `mkdir <dir>` | 创建目录，加 `--local` 在本地创建 |
| `clear` | 清除屏幕 |

**cd 命令详细说明：**

- `cd s3://bucket` — 切换到存储桶
- `cd s3://bucket/path` — 切换到存储桶并进入子目录
- `cd /` — 在根目录时：退出存储桶；在子目录时：回到根目录
- `cd ..` — 进入父目录，若在根目录则退出存储桶
- `cd --local <dir>` — 切换本地工作目录

### 文件操作

| 命令 | 描述 |
|---------|-------------|
| `cp <src> <dst>` | 复制文件或目录（自动检测上传/下载），加 `--local` 在本地复制 |
| `cat <file>` | 显示文件内容，加 `--local` 显示本地文件 |
| `upload, up <local> <remote>` | 上传文件或目录 |
| `download, dl <remote> <local>` | 下载文件或目录 |
| `mv <source> <destination>` | 重命名或移动 S3 对象（支持单个、目录前缀、通配符） |
| `rename <pattern> <replacement>` | 批量重命名对象（使用通配符模式匹配） |
| `lv <object>` | 列出对象的所有版本（按时间倒序） |

#### cp 命令选项

| 选项 | 描述 |
|--------|-------------|
| `-u, --upload` | 强制上传模式 |
| `-d, --download` | 强制下载模式 |
| `-r, --recursive` | 递归复制目录 |
| `-f, --force` | 强制覆盖，不进行确认 |
| `-n, --no-clobber` | 不覆盖已存在的文件 |
| `--versionId <id>` | 下载对象的特定版本（仅下载模式下使用） |
| `--dry-run` | 预览递归复制 |

#### upload/download 选项

| 选项 | 描述 |
|--------|-------------|
| `-r, --recursive` | 递归上传/下载目录 |
| `-f, --force` | 强制覆盖，不进行确认 |
| `-n, --no-clobber` | 不覆盖已存在的文件 |
| `--versionId <id>` | 下载对象的特定版本（仅下载时使用） |
| `--dry-run` | 预览将要传输的文件 |

**本地路径解析规则：**
- `upload` 和 `cp --upload`：本地源路径相对于本地 CWD 解析
- `download` 和 `cp --download`：本地目标路径相对于本地 CWD 解析
- 下载目标为 `./` 或 `./dir/` 时，自动在末尾追加远程文件名

#### mv 命令选项

| 选项 | 描述 |
|--------|-------------|
| `-f, --force` | 目标存在时强制覆盖 |
| `--dry-run` | 仅显示将要移动的内容 |

**mv 示例：**

```bash
mv old.txt new.txt              # 单个文件重命名
mv olddir/ newdir/              # 目录（前缀）重命名（尾部 / 可选）
mv olddir newdir                 # 同上 — 自动检测为目录前缀
mv '*.log' /logs/               # 通配符：移动所有匹配的文件
```

#### rename 命令选项

| 选项 | 描述 |
|--------|-------------|
| `-f, --force` | 目标存在时强制覆盖 |
| `--dry-run` | 仅显示将要重命名的内容 |

**rename 示例：**

模式匹配基于对象的相对名称（非完整路径）。在替换中使用 `$1`、`$2` 等引用通配符匹配的部分。

```bash
rename '*.log' '*.bak'                  # 更改扩展名
rename 'report-*.txt' 'backup-$1.txt'   # 前缀替换
```

### 删除操作

| 命令 | 描述 |
|---------|-------------|
| `rm [options] <target>` | 删除文件或目录，加 `--local` 删除本地文件 |

#### rm 命令选项

| 选项 | 描述 |
|--------|-------------|
| `-r, --recursive` | 递归删除目录及其内容 |
| `-f, --force` | 强制删除，不进行确认 |
| `--dry-run` | 仅预览将要删除的内容（会实际查询 S3 列出匹配的文件） |

#### rm 示例

```bash
rm file.txt                    # 删除单个文件
rm -r mydir/                   # 递归删除目录
rm file1.txt file2.txt         # 删除多个文件
rm *.txt                       # 删除所有 txt 文件
rm -r --dry-run mydir/         # 预览删除操作（从 S3 列出实际要删除的文件）
```

### 存储桶管理

| 命令 | 描述 |
|---------|-------------|
| `buckets` | 列出所有存储桶 |
| `use <bucket>` | 切换到指定存储桶 |
| `mb <bucket> [--versioning]` | 创建新存储桶（可选择启用版本控制） |
| `exit-bucket, root` | 退出当前存储桶，回到未选择状态 |

### 连接管理

| 命令 | 描述 |
|---------|-------------|
| `connect <endpoint> [region] [access-key] [secret-key] [--ssl]` | 连接到不同的 S3 服务 |
| `connect -i, --interactive` | 交互式连接模式 |
| `info, connection` | 显示当前连接信息 |

#### connect 示例

```bash
connect localhost:9000
connect localhost:9000 us-east-1 admin admin123
connect https://s3.amazonaws.com us-east-1 AKIA... SK... --ssl
connect play.min.io us-east-1 Q3AM3UQ867SPQQA43P2F zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG --ssl
connect --interactive
```

### 配置

| 命令 | 描述 |
|---------|-------------|
| `pagesize [num]` | 设置或查看每页显示的项目数（5-100）。用作 `ls --pageSize` 的默认值 |

### 版本管理

| 命令 | 描述 |
|---------|-------------|
| `lv <object>` | 按时间倒序列出对象的所有版本 |
| `download --versionId <id> <remote> <local>` | 下载对象的特定版本 |
| `cp --versionId <id> <source> <destination>` | 复制对象的特定版本（仅下载模式） |
| `mb <bucket> --versioning` | 创建启用版本控制的存储桶 |

`lv` 命令会显示：
- 指定对象的所有版本
- 哪个版本是最新的（用 ✓ yes 标记）
- 每个版本的最后修改时间
- 每个版本的大小
- 删除标记（如果有）

### 取消操作

在任何传输过程中按下 **Ctrl+C** 即可取消：

- **单个文件上传/下载**：立即取消正在进行的 HTTP 请求，显示 "⏹ 已取消"
- **批量操作**（`upload -r`、`download -r`、`cp -r`、`mv`、`rename`、`rm -r`）：中止当前正在传输的文件，跳过剩余文件，已处理完的文件不受影响

### 其他命令

| 命令 | 描述 |
|---------|-------------|
| `help, ?` | 显示帮助 |
| `history` | 显示命令历史 |
| `exit, quit` | 退出程序 |

## 本地文件操作（`--local` 参数）

许多命令支持 `--local` 参数以便在本地文件系统上操作，而非 S3。
本地工作目录（CWD）初始化为 Shell 的当前目录，可通过 `cd --local` 修改。

| 命令 | 描述 |
|---------|-------------|
| `pwd --local` | 显示本地 CWD |
| `cd --local <dir>` | 切换本地目录 |
| `ls --local [--no-page] [--page <n>] [--pageSize <n>]` | 列出本地目录（支持分页） |
| `mkdir --local <dir>` | 创建本地目录 |
| `cat --local <file>` | 显示本地文件内容 |
| `rm --local [options] <target>` | 删除本地文件/目录 |
| `cp --local <src> <dst>` | 复制本地文件 |

## 分页显示

在 `ls` 交互式分页模式下：

| 按键 | 操作 |
|-----|--------|
| `n` 或 Enter | 下一页 |
| `p` | 上一页 |
| `g <page>` | 跳转到指定页 |
| `q` | 退出浏览 |

非交互式分页（静默模式，或使用了 `--page`/`--pageSize` 参数）：
```
=== Page X/Y (Total Z items, page size: W) ===
```

## 键盘快捷键

| 快捷键 | 操作 |
|----------|--------|
| ↑ / ↓ | 浏览命令历史 |
| Ctrl+U | 清除当前输入行 |
| Ctrl+C | 取消当前输入或中止正在进行的传输 |

## 交互式模式示例会话

```bash
$ s3file
╔══════════════════════════════════════════════════════════════════════════╗
║                        S3 File System CLI v0.5.1                         ║
║      Supports StoreFS, MinIO, AWS S3, and all S3-compatible services     ║
╚══════════════════════════════════════════════════════════════════════════╝
Type 'help' for available commands

Available buckets:
  - mybucket
  - testbucket

Tips: Use ↑/↓ arrows for history, Ctrl+U to clear line, Ctrl+C to cancel

no bucket selected> buckets
Available buckets:
  - mybucket
  - testbucket

no bucket selected> cd s3://mybucket
Switched to bucket: mybucket

s3://mybucket/> ls
Documents/
Images/
notes.txt

s3://mybucket/> cd Documents/

s3://mybucket/Documents/> upload /local/path/report.pdf report.pdf

s3://mybucket/Documents/> download report.pdf /local/downloads/report.pdf

s3://mybucket/Documents/> cat notes.txt
[notes.txt 的内容]

s3://mybucket/Documents/> cd /
Changed to root directory.

s3://mybucket/> cd ..
Exited bucket. No bucket selected.

no bucket selected> exit
Goodbye!
```

## 静默模式示例

```bash
# 列出存储桶
s3file --silent --command 'buckets'

# 使用 ; 分隔执行多条命令
s3file --silent --connect localhost:9000 --command 'cd s3://mybucket; ls'

# 静默模式 ls 使用分页（默认 page=1, pageSize=20）
s3file --silent --command 'cd s3://mybucket; ls --page 1 --pageSize 50'

# 上传文件
s3file --silent --command 'cd s3://mybucket; upload /path/to/local.txt remote.txt'

# 下载文件
s3file --silent --command 'cd s3://mybucket; download remote.txt /path/to/local.txt'

# 检查退出码（0=成功，1=错误）
s3file --silent --command 'buckets'
echo $?
```

## 使用提示

- **多条命令**：在静默模式的 `--command` 中使用 `;` 分隔多条命令
- **通配符**：在 `rm`、`mv`、`rename` 等命令中使用 `*` 和 `?` 进行模糊匹配
- **命令历史**：使用 ↑/↓ 箭头浏览之前执行过的命令
- **路径格式**：使用 `s3://bucket/path` 直接引用存储桶和路径
- **分页**：`ls` 默认使用分页；`--no-page` 输出原始结果，`--page`/`--pageSize` 进行非交互式翻页
- **页面大小默认值**：使用 `pagesize <n>` 设置默认值，使用 `--pageSize <n>` 单次覆盖
- **本地操作**：在命令后追加 `--local` 可在本地文件系统执行
- **环境变量**：使用环境变量来存储敏感凭证，而不是通过命令行参数
- **本地 CWD**：本地工作目录初始化为 Shell 当前目录，使用 `cd --local <dir>` 切换
- **取消传输**：按 Ctrl+C 随时取消正在进行的上传、下载或批量操作
- **进度条**：上传和下载显示实时进度条：`[========>       ]  45.2%`
- **Dry-Run 预览**：`upload -r --dry-run`、`download -r --dry-run`、`cp -r --dry-run`、`mv --dry-run`、`rename --dry-run`、`rm --dry-run` 可预览操作结果而不实际执行
- **目录上传提示**：尝试上传目录时如果没有加 `-r` 参数，s3file 会提示 "Use '-r' to upload recursively"

## 许可证

本工具是 StoreFS 项目的一部分。