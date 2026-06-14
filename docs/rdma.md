# RDMA Support for S3 Storage

## Overview

This S3-compatible storage system supports RDMA (Remote Direct Memory Access) for high-performance data transfers, including PutObject, GetObject, and multipart upload (UploadPart) over RDMA. RDMA bypasses the operating system kernel and TCP/IP stack, enabling extremely low-latency and high-throughput data transfers between nodes.

**IMPORTANT**: RDMA support is **Linux-only**. It does not work on macOS, Windows, or other operating systems.

## Available Binary Versions

Two Linux binary versions are available:

### 1. RDMA Version (`storefs_linux_rdma`)
- **IMPORTANT**: Will CRASH immediately if `libibverbs` is not installed on target system!
- **Requires**: `libibverbs` must be installed on target system
- **Features**: Full RDMA support (PutObject, GetObject, multipart UploadPart) + normal S3 functionality
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

When using the RDMA-enabled clients (`s3rdmaput`, `s3rdmaget`, and `s3rdmamultipart`), you can specify the RDMA device via the `-rdma-dev` flag:

```bash
# For Put
./s3rdmaput -rdma-dev rxe0 -bucket mybucket -object myobject -file myfile.dat

# For Get
./s3rdmaget -rdma-dev rxe0 -bucket mybucket -object myobject -file myfile.dat

# For multipart upload
./s3rdmamultipart -rdma-dev rxe0 -bucket mybucket -object bigfile.dat -file ./bigfile.dat -action all
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

### S3 UploadPart with RDMA

Multipart UploadPart reuses the same RDMA upload data path as PutObject. The multipart control operations (`CreateMultipartUpload`, `CompleteMultipartUpload`, `ListParts`, and `AbortMultipartUpload`) remain normal S3 API calls. For each part:

1. Establish a new `/rdma-ctrl` control session with a unique Request ID.
2. Send a token whose `Length` is the size of that part.
3. Send the S3 `UploadPart` request with `uploadId`, `partNumber`, `X-RDMA-Request-ID`, and an empty HTTP body (`Content-Length: 0`).
4. The server uses the token length as the actual part size and performs RDMA READ from the client's memory.

Do not reuse one Request ID for multiple parts. If a part is uploaded again with the same `partNumber`, normal multipart replacement semantics apply.

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

PutObject, GetObject, and multipart UploadPart use the following special HTTP header:

| Header | Description | Required |
|--------|-------------|----------|
| `X-RDMA-Request-ID` | A unique identifier that links the S3 request to the previously established RDMA control channel | Yes |

**Important**: The `X-RDMA-Request-ID` must be unique for each RDMA transfer. The same Request ID is used in both the WebSocket `register_request` message and the subsequent S3 API request header to associate them together. Using duplicate Request IDs may result in incorrect operation or failed transfers. In production environments, it is recommended to use cryptographically random UUIDs for Request ID generation.

### PutObject Specific Notes

- **Content-Length**: Set to `0` even when transferring data. The actual object size is conveyed via the Token message over WebSocket.

### UploadPart Specific Notes

- **Content-Length**: Set to `0` for the HTTP UploadPart request. The actual part size is conveyed by the Token `Length` field over WebSocket.
- **Request ID**: Use a unique `X-RDMA-Request-ID` for each part transfer.
- **Multipart control plane**: Create, complete, list, and abort multipart upload operations do not require RDMA.

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
# Navigate to the go directory
cd examples/rdma/go

# Compile s3rdmaput (Go version)
go build -o s3rdmaput s3rdmaput.go rdmalib.go

# Compile s3rdmaget (Go version)
go build -o s3rdmaget s3rdmaget.go rdmalib.go

# Compile s3rdmamultipart (Go version)
go build -o s3rdmamultipart s3rdmamultipart.go rdmalib.go
```

Or build both from the project root:

```bash
# Build s3rdmaput
go build -o examples/rdma/go/s3rdmaput ./examples/rdma/go/s3rdmaput.go ./examples/rdma/go/rdmalib.go

# Build s3rdmaget
go build -o examples/rdma/go/s3rdmaget ./examples/rdma/go/s3rdmaget.go ./examples/rdma/go/rdmalib.go

# Build s3rdmamultipart
go build -o examples/rdma/go/s3rdmamultipart ./examples/rdma/go/s3rdmamultipart.go ./examples/rdma/go/rdmalib.go
```

### C Version Clients

The C version of the RDMA clients (`s3rdmaget.c`, `s3rdmaput.c`, and `s3rdmamultipart.c`) are also available.

**Dependencies**:
- `libibverbs-dev`
- `libcurl4-openssl-dev`
- `libssl-dev`
- `zlib1g-dev`

**Compilation**:

```bash
# Navigate to the c directory
cd examples/rdma/c

# Compile using make
make

# Or compile manually
gcc -o s3rdmaget s3rdmaget.c rdmalib.c s3client.c -libverbs -lcurl -lcrypto -lz
gcc -o s3rdmaput s3rdmaput.c rdmalib.c s3client.c -libverbs -lcurl -lcrypto -lz
gcc -o s3rdmamultipart s3rdmamultipart.c rdmalib.c s3client.c -libverbs -lcurl -lcrypto -lz
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

# Multipart upload over RDMA
./s3rdmamultipart -action all -bucket <bucket> -object <key> -file <path> \
    [-partsize 5242880] [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] \
    [-ak admin-ak] [-sk admin-sk]
```

**Client Library Usage**:

For integrating RDMA S3 functionality into your own C applications:

1. Call `rdma_recv_setup()` once - this handles WebSocket registration, QP establishment (RTR/RTS), memory registration, and token sending
2. Call `s3_get_object()` or `s3_head_object()` for data transfers
3. Call `rdma_session_destroy()` after transfer completes to clean up resources

### Python Version Clients

Python (2.7 and 3.x compatible) re-implementation of the C s3rdmaget / s3rdmaput tools.

The libibverbs (RDMA) operations are done via a tiny C shim (`verbs_shim.c`) compiled into `librdmaverbs.so` and called from Python with `ctypes`. Everything else (WebSocket control channel, JSON protocol, SigV4 signing, S3 HTTP requests) is pure Python.

**Build**:

Requires libibverbs headers (`libibverbs-dev` on Debian/Ubuntu):

```bash
cd examples/rdma/python/py_rdma
make
mv librdmaverbs.so ../
```

This produces `librdmaverbs.so` in the python directory (rdmalib.py loads it via a path relative to itself).

**Python dependencies**:

```bash
pip install -r examples/rdma/python/requirements.txt --break-system-packages
```

(`websocket-client` only; everything else is stdlib.)

**Usage**:

```bash
python examples/rdma/python/s3rdmaget.py -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]

python examples/rdma/python/s3rdmaput.py -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]

python examples/rdma/python/s3rdmamultipart.py -action all -bucket <bucket> -object <key> -file <path> \
    [-partsize 5242880] [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]
```

### Java Version Clients

Java re-implementation of the C / Python s3rdmaget / s3rdmaput tools. Requires Java 11+.

The libibverbs (RDMA) operations are done via the same tiny C shim used by the Python version (`native/verbs_shim.c`), compiled into `native/librdmaverbs.so` and called from Java with JNA (`com.sun.jna`). Everything else (WebSocket control channel via `java.net.http.WebSocket`, JSON via `org.json`, SigV4 signing, raw-socket HTTP) is plain Java.

**Build**:

1. Compile the native verbs shim (requires `libibverbs-dev`):

```bash
cd examples/rdma/java/native
make
cd ..
```

This produces `native/librdmaverbs.so`.

2. Build the jar:

```bash
mvn package
```

This produces `target/s3rdma.jar` with JNA and org.json bundled.

**Usage**:

```bash
java -Djna.library.path=examples/rdma/java/native -cp examples/rdma/java/target/s3rdma.jar com.example.s3rdma.S3RdmaGet \
    -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]

java -Djna.library.path=examples/rdma/java/native -cp examples/rdma/java/target/s3rdma.jar com.example.s3rdma.S3RdmaPut \
    -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]

java -Djna.library.path=examples/rdma/java/native -cp examples/rdma/java/target/s3rdma.jar com.example.s3rdma.S3RdmaMultipart \
    -action all -bucket <bucket> -object <key> -file <path> \
    [-partsize 5242880] [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]
```

`-Djna.library.path=native` (or any directory containing `librdmaverbs.so`, or add it to `LD_LIBRARY_PATH`) is required so JNA can find the native verbs shim.

### s3rdmaput

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

### s3rdmamultipart

`s3rdmamultipart` is a command-line client that performs S3 multipart uploads with each `UploadPart` transferred over RDMA. Multipart control operations are normal S3 requests, while part data moves through RDMA.

**Usage**:
```bash
./s3rdmamultipart -action <all|create|upload|list|list-uploads|complete|abort> \
    -bucket <bucket_name> [-object <object_name>] [-file <file_path>] \
    [-uploadid <upload_id>] [-part <part_number>] [-partsize <bytes>] \
    [-endpoint <s3_endpoint>] [-rdma-dev <rdma_device>] \
    [-ak <access_key>] [-sk <secret_key>]
```

**Examples**:
```bash
# Create, upload all parts via RDMA, and complete the multipart upload
./s3rdmamultipart -action all -bucket mybucket -object bigfile.dat -file ./bigfile.dat \
    -partsize 5242880 -endpoint http://127.0.0.1:8901 -rdma-dev rxe0 \
    -ak admin-ak -sk admin-sk

# Manual flow: create upload, upload one or all parts, list parts, complete
./s3rdmamultipart -action create -bucket mybucket -object bigfile.dat
./s3rdmamultipart -action upload -bucket mybucket -object bigfile.dat -file ./bigfile.dat \
    -uploadid <upload-id> -part 1
./s3rdmamultipart -action list -bucket mybucket -object bigfile.dat -uploadid <upload-id>
./s3rdmamultipart -action complete -bucket mybucket -object bigfile.dat -uploadid <upload-id>

# Abort an unfinished multipart upload
./s3rdmamultipart -action abort -bucket mybucket -object bigfile.dat -uploadid <upload-id>
```

**Actions**:

| Action | Description |
|--------|-------------|
| `all` | Create a multipart upload, upload all parts over RDMA, then complete it |
| `create` | Create a multipart upload and print the Upload ID |
| `upload` | Upload all parts, or one part when `-part <n>` is specified, over RDMA |
| `list` | List uploaded parts for an Upload ID |
| `list-uploads` | List in-progress multipart uploads, optionally filtered by prefix/delimiter |
| `complete` | Complete an upload using the currently listed parts |
| `abort` | Abort an unfinished multipart upload |

**Key Implementation Details**:

1. **Multipart Control Plane**: Uses normal S3 APIs for `CreateMultipartUpload`, `ListParts`, `ListMultipartUploads`, `CompleteMultipartUpload`, and `AbortMultipartUpload`.
2. **Part Splitting**: Splits the local file by `-partsize` (default 5 MiB). Empty files are not supported by the RDMA multipart client.
3. **Per-Part RDMA Setup**: For each part, creates a unique Request ID and establishes a new `/rdma-ctrl` WebSocket session.
4. **Memory Registration**: Maps or loads the part data and registers the memory for REMOTE_READ.
5. **UploadPart Request**: Sends S3 `UploadPart` with `partNumber`, `uploadId`, `X-RDMA-Request-ID`, and `Content-Length: 0`; the server reads the part bytes from the registered memory by RDMA.
6. **Completion**: Collects uploaded part ETags and sends `CompleteMultipartUpload`.

### s3rdmaget

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
