# S3存储的RDMA支持

## 概述

这个兼容S3的存储系统支持RDMA（远程直接内存访问），用于高性能数据传输。RDMA绕过操作系统内核和TCP/IP协议栈，实现节点间极低延迟和极高吞吐量的数据传输。

**重要提示**：RDMA支持**仅适用于Linux**。在macOS、Windows或其他操作系统上无法使用。

## 可用的二进制版本

提供两个Linux二进制版本：

### 1. RDMA版（`storefs_linux_rdma`）
- **重要警告**：如果目标系统没有安装 `libibverbs`，程序会立即崩溃！
- **运行要求**：目标系统必须安装 `libibverbs`
- **功能**：完整RDMA支持 + 普通S3功能
- **适用场景**：需要高性能RDMA传输时

### 2. 标准版（`storefs_linux`）
- **运行要求**：无特殊依赖！可在任何Linux系统上运行
- **功能**：仅普通S3功能（RDMA功能优雅降级）
- **适用场景**：不需要RDMA或目标系统缺少libibverbs时

## 如何选择？

| 场景 | 使用版本 |
|------|----------|
| 系统有RDMA硬件 | `storefs_linux_rdma` |
| 系统已安装libibverbs | `storefs_linux_rdma` |
| 需要最高性能 | `storefs_linux_rdma` |
| 部署到未知系统 | `storefs_linux` |
| 希望零依赖 | `storefs_linux` |

**检查系统是否有libibverbs**：
```bash
ldconfig -p | grep libibverbs
```
如果有输出，说明系统有libibverbs，可以使用`storefs_linux_rdma`！

---

## 前置条件

### 安装RDMA依赖库

#### Ubuntu / Debian

```bash
# 安装RDMA库和工具
sudo apt-get update
sudo apt-get install -y libibverbs-dev librdmacm-dev rdma-core ibverbs-utils

# 安装Soft-RoCE（软件仿真，用于在没有真实RDMA硬件的情况下测试）
sudo apt-get install -y rdmacm-utils infiniband-diags perftest

# 加载Soft-RoCE内核模块
sudo modprobe rdma_rxe
```

#### CentOS / RHEL

```bash
# 安装RDMA库和工具
sudo yum install -y libibverbs-devel librdmacm-devel rdma-core libibverbs-utils

# 安装Soft-RoCE
sudo yum install -y rdmacm-utils infiniband-diags perftest

# 加载Soft-RoCE内核模块
sudo modprobe rdma_rxe
```

#### Fedora

```bash
# 安装RDMA库和工具
sudo dnf install -y libibverbs-devel librdmacm-devel rdma-core libibverbs-utils

# 安装Soft-RoCE
sudo dnf install -y rdmacm-utils infiniband-diags perftest

# 加载Soft-RoCE内核模块
sudo modprobe rdma_rxe
```

### 验证RDMA设备

检查RDMA设备是否可用：

```bash
# 列出所有RDMA设备
ibv_devices

# 检查设备状态
ibv_devinfo

# 对于Soft-RoCE，检查rxe设备是否存在
ls /sys/class/infiniband/
```

## 配置

### 服务端配置

在YAML配置文件中，可以指定要使用的RDMA设备：

```yaml
cluster:
  name: mycluster
  db:
    # ... 数据库配置 ...
  node:
    name: node1
    num: 1
    ip: 127.0.0.1
    port: 7946
    rdma_dev: rxe0  # 在此设置RDMA设备名称（如rxe0、mlx5_0等）
    disks:
      - path: /path/to/disk1
        weight: 1
    s3:
      host: 127.0.0.1
      port: 8901
  seeds:
    - 127.0.0.1:7946
```

`rdma_dev`字段指定RDMA设备名称（Soft-RoCE通常为`rxe0`，Mellanox ConnectX-5通常为`mlx5_0`等）。如果未指定，默认为`rxe0`。

### 客户端配置

使用支持RDMA的客户端（`s3rdmaput`和`s3rdmaget`）时，可以通过`-rdma-dev`标志指定RDMA设备：

```bash
# 上传文件
./s3rdmaput -rdma-dev rxe0 -bucket mybucket -object myobject -file myfile.dat

# 下载文件
./s3rdmaget -rdma-dev rxe0 -bucket mybucket -object myobject -file myfile.dat
```

## RDMA协议流程

### 概述

RDMA传输使用两个通道：
1. **WebSocket控制通道** - 用于交换RDMA连接参数（QP信息、令牌等）
2. **RDMA数据通道** - 用于通过RDMA操作进行实际数据传输

### 使用RDMA的S3 PutObject

以下是支持RDMA的PutObject的详细流程：

```
┌─────────┐                                    ┌─────────┐
│ 客户端  │                                    │ 服务端  │
└────┬────┘                                    └────┬────┘
     │                                              │
     │  1. WebSocket连接 (/rdma-ctrl)              │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  2. RegisterRequest (RequestID)                │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  3. RegisterResponse (Success)             │
     │ <──────────────────────────────────────────  │
     │                                              │
     │  4. 初始化RDMA资源                        │
     │  (创建QP、分配PD等)                        │
     │                                              │
     │  5. QPInfoClient (客户端的QP参数)            │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │     6. 初始化服务端的RDMA资源               │
     │        (创建QP、分配PD等)                        │
     │                                              │
     │  7. QPInfoServer (服务端的QP参数)            │
     │ <──────────────────────────────────────────  │
     │                                              │
     │  8. 将QP转换到RTR → RTS状态                │
     │                                              │
     │  9. 注册内存区域和令牌                       │
     │     (地址、RKey、长度)                       │
     │                                              │
     │ 10. 令牌消息                                │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │ 11. 提前关闭WebSocket                       │
     │                                              │
     │ 12. S3 PutObject请求                       │
     │     (X-RDMA-Request-ID: <request-id>)     │
     │     (Content-Length: 0)                      │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │     13. 服务端执行RDMA READ操作             │
     │        从客户端内存读取                       │
     │                                              │
     │ 14. HTTP 200 OK响应                        │
     │ <──────────────────────────────────────────  │
     │                                              │
```

### 使用RDMA的S3 GetObject

以下是支持RDMA的GetObject的详细流程：

```
┌─────────┐                                    ┌─────────┐
│ 客户端  │                                    │ 服务端  │
└────┬────┘                                    └────┬────┘
     │                                              │
     │  1. S3 HeadObject请求（获取对象大小）         │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  2. HeadObject响应（Content-Length）        │
     │ <──────────────────────────────────────────  │
     │                                              │
     │  3. WebSocket连接 (/rdma-ctrl)              │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  4. RegisterRequest (RequestID)                │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  5. RegisterResponse (Success)             │
     │ <──────────────────────────────────────────  │
     │                                              │
     │  6. 初始化RDMA资源                          │
     │  (创建QP、分配PD等)                        │
     │                                              │
     │  7. QPInfoClient (客户端的QP参数)            │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │     8. 初始化服务端的RDMA资源               │
     │        (创建QP、分配PD等)                        │
     │                                              │
     │  9. QPInfoServer (服务端的QP参数)            │
     │ <──────────────────────────────────────────  │
     │                                              │
     │ 10. 将QP转换到RTR → RTS状态                │
     │                                              │
     │ 11. 注册用于写入的内存区域                  │
     │     (地址、RKey、长度)                       │
     │                                              │
     │ 12. 令牌消息                                │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │ 13. 提前关闭WebSocket                       │
     │                                              │
     │ 14. S3 GetObject请求                       │
     │     (X-RDMA-Request-ID: <request-id>)        │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │     15. 服务端执行RDMA WRITE操作             │
     │        写入到客户端内存                       │
     │                                              │
     │ 16. HTTP 200 OK响应（包含元数据）          │
     │ <──────────────────────────────────────────  │
     │                                              │
```

## S3请求头

### 通用RDMA请求头

PutObject和GetObject都使用以下特殊HTTP请求头：

| 请求头 | 描述 | 是否必需 |
|--------|------|----------|
| `X-RDMA-Request-ID` | 将S3请求与先前建立的RDMA控制通道关联的唯一标识符 | 是 |

**重要**：`X-RDMA-Request-ID`对于每个RDMA传输必须是唯一的。同一个请求ID会在WebSocket的`register_request`消息和后续的S3 API请求头中使用，以将它们关联在一起。使用重复的请求ID可能导致操作错误或传输失败。在生产环境中，建议使用加密随机UUID来生成请求ID。

### PutObject特定说明

- **Content-Length**：即使在传输数据时也设置为`0`。实际对象大小通过WebSocket上的Token消息传递。

### GetObject特定说明

- 除了`X-RDMA-Request-ID`外，没有额外的特殊请求头。

## WebSocket协议消息

### 消息封装

所有WebSocket消息使用以下封装格式（JSON）：

```json
{
  "type": "message_type_string",
  "data": { /* 消息特定的JSON对象 */ }
}
```

### 消息类型

| 类型 | 方向 | 描述 |
|------|------|------|
| `register_request` | 客户端 → 服务端 | 注册新的RDMA传输会话 |
| `register_response` | 服务端 → 客户端 | 确认注册 |
| `qpinfo_client` | 客户端 → 服务端 | 发送客户端的队列对参数 |
| `qpinfo_server` | 服务端 → 客户端 | 发送服务端的队列对参数 |
| `token` | 客户端 → 服务端 | 发送用于访问的RDMA内存令牌 |
| `ack` | 双向 | 确认完成（在当前实现中可选） |
| `error` | 双向 | 表示发生错误 |

### 消息详情

#### 1. RegisterRequest

**类型**：`register_request`

**方向**：客户端 → 服务端

**数据格式**：
```json
{
  "request_id": "unique_request_identifier_string"
}
```

**字段**：
- `request_id`：此RDMA传输会话的唯一标识符（通常包含PID或UUID）

#### 2. RegisterResponse

**类型**：`register_response`

**方向**：服务端 → 客户端

**数据格式**：
```json
{
  "success": true,
  "error": "error message if success is false"
}
```

**字段**：
- `success`：布尔值，指示注册是否成功
- `error`：错误消息（仅在`success`为`false`时存在）

#### 3. QPInfoClient

**类型**：`qpinfo_client`

**方向**：客户端 → 服务端

**数据格式**：
```json
{
  "qpn": 12345,
  "lid": 0,
  "gid": [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15],
  "gid_idx": 0,
  "mtu_enum": 4
}
```

**字段**：
- `qpn`：队列对编号（QPN）
- `lid`：本地ID（LID）- 对于RoCE通常为0
- `gid`：全局ID（16字节数组）
- `gid_idx`：GID索引
- `mtu_enum`：MTU枚举值：
  - 1 = 256字节
  - 2 = 512字节
  - 3 = 1024字节
  - 4 = 2048字节
  - 5 = 4096字节

#### 4. QPInfoServer

**类型**：`qpinfo_server`

**方向**：服务端 → 客户端

**数据格式**：与`qpinfo_client`相同

#### 5. Token

**类型**：`token`

**方向**：客户端 → 服务端

**数据格式**：
```json
{
  "addr": 1234567890123,
  "rkey": 987654,
  "start": 0,
  "length": 1048576
}
```

**字段**：
- `addr`：已注册内存区域的虚拟地址
- `rkey`：用于远程访问的远程密钥（RKey）
- `start`：从`addr`开始的数据偏移量
- `length`：数据区域的长度（字节）

**对于PutObject**：服务端使用此令牌从客户端内存执行RDMA READ。

**对于GetObject**：服务端使用此令牌向客户端内存执行RDMA WRITE。

#### 6. Ack

**类型**：`ack`

**方向**：双向

**数据格式**：
```json
{
  "status": 0,
  "crc32_computed": 123456789,
  "bytes_written": 1048576
}
```

**字段**：
- `status`：状态码（0 = OK，1或2 = 错误）
- `crc32_computed`：数据的CRC32校验和（可选，用于验证）
- `bytes_written`：传输的字节数（可选）

**注意**：当前实现不主动使用此消息。相反，HTTP响应作为确认。

#### 7. Error

**类型**：`error`

**方向**：双向

**数据格式**：
```json
{
  "error": "human-readable error message"
}
```

**字段**：
- `error`：人类可读的错误消息

## 示例程序

### 编译示例程序

示例程序应在Linux上编译（因为RDMA仅支持Linux）：

```bash
# 进入s3client目录
cd s3client

# 编译s3rdmaput (Go 版本)
go build -o s3rdmaput s3rdmaput.go rdmalib.go

# 编译s3rdmaget (Go 版本)
go build -o s3rdmaget s3rdmaget.go rdmalib.go
```

或者从项目根目录编译：

```bash
# 编译s3rdmaput
go build -o s3client/s3rdmaput ./s3client/s3rdmaput.go ./s3client/rdmalib.go

# 编译s3rdmaget
go build -o s3client/s3rdmaget ./s3client/s3rdmaget.go ./s3client/rdmalib.go
```

### C 版本客户端

还提供了 C 版本的 RDMA 客户端（`s3rdmaget.c` 和 `s3rdmaput.c`）。

**依赖库**：
- `libibverbs-dev`
- `libcurl4-openssl-dev`
- `libssl-dev`
- `zlib1g-dev`

**编译方法**：

```bash
# 进入 c 目录
cd s3client/c

# 使用 make 编译
make

# 或者手动编译
gcc -o s3rdmaget s3rdmaget.c rdmalib.c s3client.c -libverbs -lcurl -lcrypto -lz
gcc -o s3rdmaput s3rdmaput.c rdmalib.c s3client.c -libverbs -lcurl -lcrypto -lz
```

**使用方法**：

C 版本的使用方法与 Go 版本完全一致：

```bash
# 下载对象
./s3rdmaget -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] \
    [-ak admin-ak] [-sk admin-sk]
```

```bash
# 上传对象
./s3rdmaput -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] \
    [-ak admin-ak] [-sk admin-sk]
```

**客户端库使用方法**：

要将 RDMA S3 功能集成到您自己的 C 应用程序中：

1. 调用 `rdma_recv_setup()` 一次 - 该函数处理 WebSocket 注册、QP 建立（RTR/RTS）、内存注册和令牌发送
2. 调用 `s3_get_object()` 或 `s3_head_object()` 进行数据传输
3. 传输完成后调用 `rdma_session_destroy()` 清理资源

### s3rdmaput.go

`s3rdmaput`是一个命令行客户端，使用RDMA将文件上传到S3存储。

**用法**：
```bash
./s3rdmaput -bucket <bucket_name> -object <object_name> -file <file_path> \
    [-endpoint <s3_endpoint>] [-rdma-dev <rdma_device>] \
    [-ak <access_key>] [-sk <secret_key>]
```

**示例**：
```bash
./s3rdmaput -bucket mybucket -object myfile.dat -file ./localfile.dat \
    -endpoint http://127.0.0.1:8901 -rdma-dev rxe0 \
    -ak admin-ak -sk admin-sk
```

**关键实现细节**：

1. **初始化**：读取命令行参数并将文件加载到内存。
2. **请求ID生成**：使用进程PID创建唯一的请求ID。
3. **WebSocket设置**：
   - 连接到`/rdma-ctrl` WebSocket端点
   - 发送`RegisterRequest`
   - 接收`RegisterResponse`
4. **RDMA资源设置**：
   - 打开RDMA设备（`ibv_open_device`）
   - 分配保护域（`ibv_alloc_pd`）
   - 创建完成队列（`ibv_create_cq`）
   - 创建队列对（`ibv_create_qp`）
   - 将QP转换到INIT状态
   - 查询GID和MTU信息
5. **QP交换**：
   - 发送客户端的QP信息（`QPInfoClient`）
   - 接收服务端的QP信息（`QPInfoServer`）
   - 将QP转换到RTR（准备接收）
   - 将QP转换到RTS（准备发送）
6. **内存注册**：
   - 分配匿名mmap缓冲区
   - 将文件数据复制到缓冲区
   - 为REMOTE_READ注册内存区域（`ibv_reg_mr`）
7. **令牌交换**：
   - 向服务端发送令牌（地址、RKey、长度）
8. **WebSocket关闭**：提前关闭WebSocket（HTTP响应将确认完成）
9. **S3请求**：
   - 创建自定义HTTP传输，添加`X-RDMA-Request-ID`请求头
   - 发送Content-Length: 0的PutObject请求
   - 等待HTTP 200 OK响应

### s3rdmaget.go

`s3rdmaget`是一个命令行客户端，使用RDMA从S3存储下载文件。

**用法**：
```bash
./s3rdmaget -bucket <bucket_name> -object <object_name> -file <file_path> \
    [-endpoint <s3_endpoint>] [-rdma-dev <rdma_device>] \
    [-ak <access_key>] [-sk <secret_key>]
```

**示例**：
```bash
./s3rdmaget -bucket mybucket -object myfile.dat -file ./downloaded.dat \
    -endpoint http://127.0.0.1:8901 -rdma-dev rxe0 \
    -ak admin-ak -sk admin-sk
```

**关键实现细节**：

1. **首先HeadObject**：
   - 发送正常的S3 HeadObject请求以获取对象大小
   - 使用此大小稍后分配缓冲区
2. **请求ID生成**：使用进程PID创建唯一的请求ID。
3. **WebSocket设置**：（与上传相同）
   - 连接到`/rdma-ctrl` WebSocket端点
   - 发送`RegisterRequest`
   - 接收`RegisterResponse`
4. **RDMA资源设置**：（与上传相同）
   - 打开RDMA设备
   - 分配PD、创建CQ和QP
   - 将QP转换到INIT
   - 查询GID和MTU
5. **QP交换**：（与上传相同）
   - 发送客户端的QP信息
   - 接收服务端的QP信息
   - 将QP转换到RTR，然后到RTS
6. **内存注册**：
   - 分配对象大小的匿名mmap缓冲区
   - 为REMOTE_WRITE注册内存区域（`ibv_reg_mr`）
7. **令牌交换**：
   - 向服务端发送令牌（地址、RKey、长度）
8. **WebSocket关闭**：提前关闭WebSocket
9. **S3请求**：
   - 创建自定义HTTP传输，添加`X-RDMA-Request-ID`请求头
   - 发送GetObject请求
   - 等待HTTP 200 OK响应
10. **数据验证和保存**：
    - 计算接收数据的CRC32
    - 将缓冲区中的数据写入输出文件

## 安全考虑因素

- RDMA内存区域允许直接远程访问。确保：
  - 令牌交换通过安全通道进行（生产环境中WebSocket应使用WSS）
  - 传输完成后立即取消内存区域注册
  - 请求ID不可猜测（生产环境中使用加密随机值）

## 故障排除

### 常见问题

1. **打开RDMA设备时出现"No such device"**：
   - 使用`ibv_devices`验证设备名称
   - 确保已加载内核模块（`lsmod | grep rdma`）

2. **QP转换失败**：
   - 检查GID索引是否正确
   - 验证客户端和服务端之间的MTU设置匹配

3. **RDMA操作超时**：
   - 检查网络连接
   - 验证双方都使用RoCE v2（IPv4）
   - 检查是否有防火墙阻止RDMA流量

### 有用的调试命令

```bash
# 列出RDMA设备
ibv_devices

# 显示详细设备信息
ibv_devinfo -d rxe0

# 显示GID表
show_gids rxe0

# 简单的RDMA ping测试（加载rxe后）
ibv_rc_pingpong -d rxe0 -g 0
```

## 限制

- 仅适用于Linux
- 当前仅使用RC（可靠连接）QP类型
- 块大小固定为2MB
- 每个连接单线程操作
