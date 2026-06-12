# RDMA Support for S3 Storage

## Overview

This S3-compatible storage system supports RDMA (Remote Direct Memory Access) for high-performance data transfers. RDMA bypasses the operating system kernel and TCP/IP stack, enabling extremely low-latency and high-throughput data transfers between nodes.

**IMPORTANT**: RDMA support is **Linux-only**. It does not work on macOS, Windows, or other operating systems.

## Available Binary Versions

Two Linux binary versions are available:

### 1. RDMA Version (`storefs_linux_rdma`)
- **IMPORTANT**: Will CRASH immediately if `libibverbs` is not installed on target system!
- **Requires**: `libibverbs` must be installed on target system
- **Features**: Full RDMA support + normal S3 functionality
- **Use when**: You need high-performance RDMA transfers

### 2. Standard Version (`storefs_linux`)
- **Requires**: No special dependencies! Works on any Linux system
- **Features**: Normal S3 functionality only (RDMA functions gracefully degrade)
- **Use when**: You don't need RDMA or target system lacks libibverbs

## Which Should You Use?

| Scenario | Use |
|----------|-----|
| Your system has RDMA hardware | `storefs_linux_rdma` |
| Your system has libibverbs installed | `storefs_linux_rdma` |
| You need maximum performance | `storefs_linux_rdma` |
| Deploying to unknown systems | `storefs_linux` |
| Want zero dependencies | `storefs_linux` |

**How to check for libibverbs**:
```bash
ldconfig -p | grep libibverbs
```
If you see output, you have libibverbs and can use `storefs_linux_rdma`!

---

## Prerequisites

## Prerequisites

### Installing RDMA Dependencies

#### Ubuntu / Debian

```bash
# Install RDMA libraries and utilities
sudo apt-get update
sudo apt-get install -y libibverbs-dev librdmacm-dev rdma-core ibverbs-utils

# For Soft-RoCE (software emulation, useful for testing without real RDMA hardware)
sudo apt-get install -y rdmacm-utils infiniband-diags perftest

# Load Soft-RoCE kernel module
sudo modprobe rdma_rxe
```

#### CentOS / RHEL

```bash
# Install RDMA libraries and utilities
sudo yum install -y libibverbs-devel librdmacm-devel rdma-core libibverbs-utils

# For Soft-RoCE
sudo yum install -y rdmacm-utils infiniband-diags perftest

# Load Soft-RoCE kernel module
sudo modprobe rdma_rxe
```

#### Fedora

```bash
# Install RDMA libraries and utilities
sudo dnf install -y libibverbs-devel librdmacm-devel rdma-core libibverbs-utils

# For Soft-RoCE
sudo dnf install -y rdmacm-utils infiniband-diags perftest

# Load Soft-RoCE kernel module
sudo modprobe rdma_rxe
```

### Verifying RDMA Device

Check if your RDMA device is available:

```bash
# List all RDMA devices
ibv_devices

# Check device status
ibv_devinfo

# For Soft-RoCE, check if rxe devices exist
ls /sys/class/infiniband/
```

## Configuration

### Server Configuration

In your YAML configuration file, you can specify the RDMA device to use:

```yaml
cluster:
  name: mycluster
  db:
    # ... database config ...
  node:
    name: node1
    num: 1
    ip: 127.0.0.1
    port: 7946
    rdma_dev: rxe0  # Set your RDMA device name here (e.g., rxe0, mlx5_0, etc.)
    disks:
      - path: /path/to/disk1
        weight: 1
    s3:
      host: 127.0.0.1
      port: 8901
  seeds:
    - 127.0.0.1:7946
```

The `rdma_dev` field specifies the RDMA device name (typically `rxe0` for Soft-RoCE, `mlx5_0` for Mellanox ConnectX-5, etc.). If not specified, it defaults to `rxe0`.

### Client Configuration

When using the RDMA-enabled clients (`s3rdmaput` and `s3rdmaget`), you can specify the RDMA device via the `-rdma-dev` flag:

```bash
# For Put
./s3rdmaput -rdma-dev rxe0 -bucket mybucket -object myobject -file myfile.dat

# For Get
./s3rdmaget -rdma-dev rxe0 -bucket mybucket -object myobject -file myfile.dat
```

## RDMA Protocol Flow

### Overview

The RDMA transfer uses two channels:
1. **WebSocket Control Channel** - For exchanging RDMA connection parameters (QP info, tokens, etc.)
2. **RDMA Data Channel** - For actual data transfer via RDMA operations

### S3 PutObject with RDMA

Here's the detailed flow for an RDMA-enabled PutObject:

```
┌─────────┐                                    ┌─────────┐
│ Client  │                                    │ Server  │
└────┬────┘                                    └────┬────┘
     │                                              │
     │  1. WebSocket Connect (/rdma-ctrl)           │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  2. RegisterRequest (RequestID)              │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  3. RegisterResponse (Success)               │
     │ <──────────────────────────────────────────  │
     │                                              │
     │  4. Initialize RDMA Resources                │
     │  (Create QP, Alloc PD, etc.)                 │
     │                                              │
     │  5. QPInfoClient (Client's QP parameters)    │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │     6. Initialize Server's RDMA Resources    │
     │        (Create QP, Alloc PD, etc.)           │
     │                                              │
     │  7. QPInfoServer (Server's QP parameters)    │
     │ <──────────────────────────────────────────  │
     │                                              │
     │  8. Transition QP to RTR → RTS               │
     │                                              │
     │  9. Register Memory Region & Token           │
     │     (Addr, RKey, Length)                     │
     │                                              │
     │ 10. Token Message                            │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │ 11. Close WebSocket (early)                  │
     │                                              │
     │ 12. S3 PutObject Request                     │
     │     (X-RDMA-Request-ID: <request-id>)        │
     │     (Content-Length: 0)                      │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │     13. Server performs RDMA READ            │
     │        from client's memory                  │
     │                                              │
     │ 14. HTTP 200 OK Response                     │
     │ <──────────────────────────────────────────  │
     │                                              │
```

### S3 GetObject with RDMA

Here's the detailed flow for an RDMA-enabled GetObject:

```
┌─────────┐                                    ┌─────────┐
│ Client  │                                    │ Server  │
└────┬────┘                                    └────┬────┘
     │                                              │
     │  1. S3 HeadObject Request (Get Object Size)  │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  2. HeadObject Response (Content-Length)     │
     │ <──────────────────────────────────────────  │
     │                                              │
     │  3. WebSocket Connect (/rdma-ctrl)           │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  4. RegisterRequest (RequestID)              │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │  5. RegisterResponse (Success)               │
     │ <──────────────────────────────────────────  │
     │                                              │
     │  6. Initialize RDMA Resources                │
     │  (Create QP, Alloc PD, etc.)                 │
     │                                              │
     │  7. QPInfoClient (Client's QP parameters)    │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │     8. Initialize Server's RDMA Resources    │
     │        (Create QP, Alloc PD, etc.)           │
     │                                              │
     │  9. QPInfoServer (Server's QP parameters)    │
     │ <──────────────────────────────────────────  │
     │                                              │
     │ 10. Transition QP to RTR → RTS               │
     │                                              │
     │ 11. Register Memory Region for Write         │
     │     (Addr, RKey, Length)                     │
     │                                              │
     │ 12. Token Message                            │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │ 13. Close WebSocket (early)                  │
     │                                              │
     │ 14. S3 GetObject Request                     │
     │     (X-RDMA-Request-ID: <request-id>)        │
     │ ──────────────────────────────────────────>  │
     │                                              │
     │     15. Server performs RDMA WRITE           │
     │        to client's memory                    │
     │                                              │
     │ 16. HTTP 200 OK Response (with metadata)     │
     │ <──────────────────────────────────────────  │
     │                                              │
```

## S3 Request Headers

### Common RDMA Headers

Both PutObject and GetObject use the following special HTTP header:

| Header | Description | Required |
|--------|-------------|----------|
| `X-RDMA-Request-ID` | A unique identifier that links the S3 request to the previously established RDMA control channel | Yes |

**Important**: The `X-RDMA-Request-ID` must be unique for each RDMA transfer. The same Request ID is used in both the WebSocket `register_request` message and the subsequent S3 API request header to associate them together. Using duplicate Request IDs may result in incorrect operation or failed transfers. In production environments, it is recommended to use cryptographically random UUIDs for Request ID generation.

### PutObject Specific Notes

- **Content-Length**: Set to `0` even when transferring data. The actual object size is conveyed via the Token message over WebSocket.

### GetObject Specific Notes

- No additional special headers beyond `X-RDMA-Request-ID`.

## WebSocket Protocol Messages

### Message Envelope

All WebSocket messages use the following envelope format (JSON):

```json
{
  "type": "message_type_string",
  "data": { /* message-specific JSON object */ }
}
```

### Message Types

| Type | Direction | Description |
|------|-----------|-------------|
| `register_request` | Client → Server | Registers a new RDMA transfer session |
| `register_response` | Server → Client | Acknowledges registration |
| `qpinfo_client` | Client → Server | Sends client's Queue Pair parameters |
| `qpinfo_server` | Server → Client | Sends server's Queue Pair parameters |
| `token` | Client → Server | Sends RDMA memory token for access |
| `ack` | Either | Acknowledges completion (optional in current implementation) |
| `error` | Either | Indicates an error occurred |

### Message Details

#### 1. RegisterRequest

**Type**: `register_request`

**Direction**: Client → Server

**Data Format**:
```json
{
  "request_id": "unique_request_identifier_string"
}
```

**Fields**:
- `request_id`: Unique identifier for this RDMA transfer session (typically includes PID or UUID)

#### 2. RegisterResponse

**Type**: `register_response`

**Direction**: Server → Client

**Data Format**:
```json
{
  "success": true,
  "error": "error message if success is false"
}
```

**Fields**:
- `success`: Boolean indicating if registration succeeded
- `error`: Error message (only present if `success` is `false`)

#### 3. QPInfoClient

**Type**: `qpinfo_client`

**Direction**: Client → Server

**Data Format**:
```json
{
  "qpn": 12345,
  "lid": 0,
  "gid": [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15],
  "gid_idx": 0,
  "mtu_enum": 4
}
```

**Fields**:
- `qpn`: Queue Pair Number (QPN)
- `lid`: Local ID (LID) - typically 0 for RoCE
- `gid`: Global ID (16-byte array)
- `gid_idx`: GID index
- `mtu_enum`: MTU enumeration value:
  - 1 = 256 bytes
  - 2 = 512 bytes
  - 3 = 1024 bytes
  - 4 = 2048 bytes
  - 5 = 4096 bytes

#### 4. QPInfoServer

**Type**: `qpinfo_server`

**Direction**: Server → Client

**Data Format**: Same as `qpinfo_client`

#### 5. Token

**Type**: `token`

**Direction**: Client → Server

**Data Format**:
```json
{
  "addr": 1234567890123,
  "rkey": 987654,
  "start": 0,
  "length": 1048576
}
```

**Fields**:
- `addr`: Virtual address of the registered memory region
- `rkey`: Remote key (RKey) for remote access
- `start`: Offset from `addr` where data starts
- `length`: Length of the data region in bytes

**For PutObject**: Server uses this token to perform RDMA READ from client's memory.

**For GetObject**: Server uses this token to perform RDMA WRITE to client's memory.

#### 6. Ack

**Type**: `ack`

**Direction**: Either

**Data Format**:
```json
{
  "status": 0,
  "crc32_computed": 123456789,
  "bytes_written": 1048576
}
```

**Fields**:
- `status`: Status code (0 = OK, 1 or 2 = error)
- `crc32_computed`: CRC32 checksum of the data (optional, used for validation)
- `bytes_written`: Number of bytes transferred (optional)

**Note**: The current implementation does not actively use this message. Instead, the HTTP response serves as the acknowledgment.

#### 7. Error

**Type**: `error`

**Direction**: Either

**Data Format**:
```json
{
  "error": "human-readable error message"
}
```

**Fields**:
- `error`: Human-readable error message

## Example Programs

### Compiling the Example Programs

The example programs should be compiled on Linux (since RDMA is Linux-only):

```bash
# Navigate to the s3client directory
cd s3client

# Compile s3rdmaput (Go version)
go build -o s3rdmaput s3rdmaput.go rdmalib.go

# Compile s3rdmaget (Go version)
go build -o s3rdmaget s3rdmaget.go rdmalib.go
```

Or build both from the project root:

```bash
# Build s3rdmaput
go build -o s3client/s3rdmaput ./s3client/s3rdmaput.go ./s3client/rdmalib.go

# Build s3rdmaget
go build -o s3client/s3rdmaget ./s3client/s3rdmaget.go ./s3client/rdmalib.go
```

### C Version Clients

The C version of the RDMA clients (`s3rdmaget.c` and `s3rdmaput.c`) are also available.

**Dependencies**:
- `libibverbs-dev`
- `libcurl4-openssl-dev`
- `libssl-dev`
- `zlib1g-dev`

**Compilation**:

```bash
# Navigate to the c directory
cd s3client/c

# Compile using make
make

# Or compile manually
gcc -o s3rdmaget s3rdmaget.c rdmalib.c s3client.c -libverbs -lcurl -lcrypto -lz
gcc -o s3rdmaput s3rdmaput.c rdmalib.c s3client.c -libverbs -lcurl -lcrypto -lz
```

**Usage**:

The C version usage is identical to the Go version:

```bash
# Get object
./s3rdmaget -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] \
    [-ak admin-ak] [-sk admin-sk]
```

```bash
# Put object
./s3rdmaput -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] \
    [-ak admin-ak] [-sk admin-sk]
```

**Client Library Usage**:

For integrating RDMA S3 functionality into your own C applications:

1. Call `rdma_recv_setup()` once - this handles WebSocket registration, QP establishment (RTR/RTS), memory registration, and token sending
2. Call `s3_get_object()` or `s3_head_object()` for data transfers
3. Call `rdma_session_destroy()` after transfer completes to clean up resources

### s3rdmaput.go

`s3rdmaput` is a command-line client that uploads a file to the S3 storage using RDMA.

**Usage**:
```bash
./s3rdmaput -bucket <bucket_name> -object <object_name> -file <file_path> \
    [-endpoint <s3_endpoint>] [-rdma-dev <rdma_device>] \
    [-ak <access_key>] [-sk <secret_key>]
```

**Example**:
```bash
./s3rdmaput -bucket mybucket -object myfile.dat -file ./localfile.dat \
    -endpoint http://127.0.0.1:8901 -rdma-dev rxe0 \
    -ak admin-ak -sk admin-sk
```

**Key Implementation Details**:

1. **Initialization**: Reads command-line arguments and loads the file into memory.
2. **Request ID Generation**: Creates a unique Request ID using the process PID.
3. **WebSocket Setup**:
   - Connects to `/rdma-ctrl` WebSocket endpoint
   - Sends `RegisterRequest`
   - Receives `RegisterResponse`
4. **RDMA Resource Setup**:
   - Opens the RDMA device (`ibv_open_device`)
   - Allocates a Protection Domain (`ibv_alloc_pd`)
   - Creates a Completion Queue (`ibv_create_cq`)
   - Creates a Queue Pair (`ibv_create_qp`)
   - Transitions QP to INIT state
   - Queries GID and MTU information
5. **QP Exchange**:
   - Sends client's QP info (`QPInfoClient`)
   - Receives server's QP info (`QPInfoServer`)
   - Transitions QP to RTR (Ready to Receive)
   - Transitions QP to RTS (Ready to Send)
6. **Memory Registration**:
   - Allocates an anonymous mmap buffer
   - Copies the file data into the buffer
   - Registers the memory region for REMOTE_READ (`ibv_reg_mr`)
7. **Token Exchange**:
   - Sends the token (address, RKey, length) to server
8. **WebSocket Close**: Closes the WebSocket early (HTTP response will confirm completion)
9. **S3 Request**:
   - Creates a custom HTTP transport that adds the `X-RDMA-Request-ID` header
   - Sends PutObject request with Content-Length: 0
   - Waits for HTTP 200 OK response

### s3rdmaget.go

`s3rdmaget` is a command-line client that downloads a file from the S3 storage using RDMA.

**Usage**:
```bash
./s3rdmaget -bucket <bucket_name> -object <object_name> -file <file_path> \
    [-endpoint <s3_endpoint>] [-rdma-dev <rdma_device>] \
    [-ak <access_key>] [-sk <secret_key>]
```

**Example**:
```bash
./s3rdmaget -bucket mybucket -object myfile.dat -file ./downloaded.dat \
    -endpoint http://127.0.0.1:8901 -rdma-dev rxe0 \
    -ak admin-ak -sk admin-sk
```

**Key Implementation Details**:

1. **HeadObject First**:
   - Sends a normal S3 HeadObject request to get the object size
   - Uses this size to allocate buffer later
2. **Request ID Generation**: Creates a unique Request ID using the process PID.
3. **WebSocket Setup**: (Same as put)
   - Connects to `/rdma-ctrl` WebSocket endpoint
   - Sends `RegisterRequest`
   - Receives `RegisterResponse`
4. **RDMA Resource Setup**: (Same as put)
   - Opens the RDMA device
   - Allocates PD, creates CQ and QP
   - Transitions QP to INIT
   - Queries GID and MTU
5. **QP Exchange**: (Same as put)
   - Sends client's QP info
   - Receives server's QP info
   - Transitions QP to RTR then RTS
6. **Memory Registration**:
   - Allocates an anonymous mmap buffer of the object size
   - Registers the memory region for REMOTE_WRITE (`ibv_reg_mr`)
7. **Token Exchange**:
   - Sends the token (address, RKey, length) to server
8. **WebSocket Close**: Closes the WebSocket early
9. **S3 Request**:
   - Creates a custom HTTP transport that adds the `X-RDMA-Request-ID` header
   - Sends GetObject request
   - Waits for HTTP 200 OK response
10. **Data Verification & Saving**:
    - Computes CRC32 of the received data
    - Writes the data from buffer to the output file

## Security Considerations

- RDMA memory regions allow direct remote access. Ensure that:
  - Token exchange happens over a secure channel (WebSocket should use WSS in production)
  - Memory regions are promptly deregistered after transfer completes
  - Request IDs are not guessable (use cryptographically random values in production)

## Troubleshooting

### Common Issues

1. **"No such device" when opening RDMA device**:
   - Verify the device name with `ibv_devices`
   - Ensure the kernel module is loaded (`lsmod | grep rdma`)

2. **QP transitions fail**:
   - Check if the GID index is correct
   - Verify MTU settings match between client and server

3. **RDMA operations time out**:
   - Check network connectivity
   - Verify that both sides are using RoCE v2 (IPv4)
   - Check if any firewall is blocking RDMA traffic

### Useful Debug Commands

```bash
# List RDMA devices
ibv_devices

# Show detailed device info
ibv_devinfo -d rxe0

# Show GID table
show_gids rxe0

# Simple RDMA ping test (after loading rxe)
ibv_rc_pingpong -d rxe0 -g 0
```

## Limitations

- Linux-only
- Currently uses RC (Reliable Connected) QP type only
- Chunk size is fixed at 2MB
- Single-threaded operation per connection
