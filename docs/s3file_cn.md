**[English](s3file.md)**

# s3file - S3 文件系统命令行工具

一个用于与 S3 兼容存储服务交互的命令行工具，支持交互式和静默两种模式。

## 功能特性

- **交互式 Shell 模式**：像操作本地文件系统一样浏览 S3 存储
- **静默模式**：编程方式执行命令
- **多供应商支持**：适用于 StoreFS、MinIO、AWS S3 以及所有 S3 兼容服务
- **分页支持**：轻松浏览大型目录
- **命令历史**：浏览之前执行过的命令
- **自动补全**：命令的 Tab 补全功能
- **通配符支持**：使用 * 和 ? 进行模糊匹配

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

```bash
# 列出所有存储桶
s3file --silent --command 'buckets'

# 连接到服务器并列出存储桶
s3file --silent --connect localhost:9000 --command 'buckets'

# 连接到 MinIO 并列出存储桶内容
s3file --silent --connect play.min.io --access-key Q3AM3UQ867SPQQA43P2F --secret-key zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG --ssl --command 'cd s3://mybucket' --command 'ls'
```

## 命令行选项

| 选项 | 描述 | 默认值 |
|--------|-------------|---------|
| `--help, -h` | 显示帮助信息 | - |
| `--silent` | 以静默模式运行（非交互式） | - |
| `--command <cmd>` | 在静默模式下执行的命令 | - |
| `--connect <url>` | 在执行命令前连接到指定端点 | - |
| `--endpoint <url>` | S3 端点 URL | `localhost:8901` |
| `--region <region>` | AWS 区域 | `us-east-1` |
| `--access-key <key>` | 访问密钥 | `admin-ak` |
| `--secret-key <key>` | 秘密密钥 | `admin-sk` |
| `--ssl` | 使用 SSL/TLS 连接 | `false` |
| `--no-banner` | 不显示欢迎横幅 | - |

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
| `ls [--no-page]` | 列出当前目录内容（默认分页） |
| `cd <directory>` | 改变目录（支持 `s3://bucket` 格式） |
| `pwd` | 显示当前路径 |
| `mkdir <dir>` | 创建目录 |
| `clear` | 清除屏幕 |

### 文件操作

| 命令 | 描述 |
|---------|-------------|
| `cp <src> <dst>` | 复制文件（自动检测上传/下载） |
| `cat <file>` | 显示文件内容 |
| `upload, up <local> <remote>` | 上传文件 |
| `download, dl <remote> <local>` | 下载文件 |

#### cp 命令选项

| 选项 | 描述 |
|--------|-------------|
| `-u, --upload` | 强制上传模式 |
| `-d, --download` | 强制下载模式 |
| `-f, --force` | 强制覆盖，不进行确认 |
| `-n, --no-clobber` | 不覆盖已存在的文件 |

#### upload/download 选项

| 选项 | 描述 |
|--------|-------------|
| `-f, --force` | 强制覆盖，不进行确认 |

### 删除操作

| 命令 | 描述 |
|---------|-------------|
| `rm [options] <target>` | 删除文件或目录 |

#### rm 命令选项

| 选项 | 描述 |
|--------|-------------|
| `-r, --recursive` | 递归删除目录及其内容 |
| `-f, --force` | 强制删除，不进行确认 |
| `--dry-run` | 仅预览将要删除的内容 |

#### rm 示例

```bash
rm file.txt                    # 删除单个文件
rm -r mydir/                   # 递归删除目录
rm file1.txt file2.txt         # 删除多个文件
rm *.txt                       # 删除所有 txt 文件
rm -r --dry-run mydir/         # 预览删除操作
```

### 存储桶管理

| 命令 | 描述 |
|---------|-------------|
| `buckets` | 列出所有存储桶 |
| `use <bucket>` | 切换到指定存储桶 |
| `mb <bucket>` | 创建新存储桶 |

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
| `pagesize [num]` | 设置或查看每页显示的项目数（5-100） |

### 其他命令

| 命令 | 描述 |
|---------|-------------|
| `help, ?` | 显示帮助 |
| `history` | 显示命令历史 |
| `exit, quit` | 退出程序 |

## 分页命令

在 `ls` 分页模式下：

| 按键 | 操作 |
|-----|--------|
| `n` 或 Enter | 下一页 |
| `p` | 上一页 |
| `g <page>` | 跳转到指定页 |
| `q` | 退出浏览 |

## 键盘快捷键

| 快捷键 | 操作 |
|----------|--------|
| ↑ / ↓ | 浏览命令历史 |
| Ctrl+U | 清除当前输入行 |
| Ctrl+C | 取消当前输入 |

## 交互式模式示例会话

```bash
$ s3file
╔══════════════════════════════════════════════════════════════════════════╗
║                        S3 File System CLI v1.0                           ║
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

s3://mybucket/Documents/> exit
Goodbye!
```

## 静默模式示例

```bash
# 列出存储桶
s3file --silent --command 'buckets'

# 执行多个命令
s3file --silent --connect localhost:9000 --command 'cd s3://mybucket' --command 'ls'

# 使用静默模式上传文件
s3file --silent --command 'cd s3://mybucket' --command 'upload /path/to/local.txt remote.txt'

# 使用静默模式下载文件
s3file --silent --command 'cd s3://mybucket' --command 'download remote.txt /path/to/local.txt'
```

## 使用提示

- **通配符**：在 `rm` 等命令中使用 `*` 和 `?` 进行模糊匹配
- **命令历史**：使用 ↑/↓ 箭头浏览之前执行过的命令
- **路径格式**：使用 `s3://bucket/path` 直接引用存储桶和路径
- **分页**：对于大型目录，使用不带 `--no-page` 的 `ls` 来浏览分页结果
- **环境变量**：使用环境变量来存储敏感凭证，而不是通过命令行参数

## 许可证

本工具是 StoreFS 项目的一部分。
