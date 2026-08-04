**[查看中文版](gateway_cn.md)**

# Gateway (NFS/SMB)

StoreFS provides NFSv3 and SMB 3.1.1 protocol gateways that allow you to mount and access your S3 buckets as a standard POSIX filesystem. This enables legacy applications, file servers, and operating systems to interact with StoreFS object storage without any S3 SDK integration.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        StoreFS Node                             │
│                                                                │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌─────────────┐ │
│  │ NFSv3    │   │ SMB 3.1.1│   │ S3 API   │   │ Admin API   │ │
│  │ Gateway  │   │ Gateway  │   │ Server   │   │ Server      │ │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘   └──────┬──────┘ │
│       │              │              │                 │         │
│       └──────────────┴──────────────┴─────────────────┘         │
│                              │                                  │
│                    ┌─────────▼──────────┐                       │
│                    │   Gateway VFS      │                       │
│                    │  (S3FS Adapter)    │                       │
│                    └─────────┬──────────┘                       │
│                              │                                  │
│                    ┌─────────▼──────────┐                       │
│                    │  Store Dispatcher  │                       │
│                    └─────────┬──────────┘                       │
│                              │                                  │
│              ┌───────────────┴───────────────┐                   │
│              │                               │                  │
│         ┌────▼────┐                    ┌─────▼─────┐            │
│         │  Disk 1 │    ......          │  Disk N   │            │
│         └─────────┘                    └───────────┘            │
└─────────────────────────────────────────────────────────────────┘
```

### How It Works

Both NFS and SMB gateways share a common **VFS (Virtual Filesystem) layer** that translates POSIX filesystem operations into S3 object operations:

1. **POSIX → S3 Translation**: File paths like `/mybucket/dir/file.txt` are parsed into bucket name (`mybucket`) and object key (`dir/file.txt`).
2. **Directory Emulation**: S3 is a flat object store, so directories are emulated using zero-byte marker objects (e.g., `dir/`) to represent directory entries.
3. **Shared Caching**: A directory listing cache (5-second TTL) reduces redundant S3 list operations.
4. **User Mapping**: All NFS/SMB client requests are mapped to a specific StoreFS user for authentication and authorization.

### Key Benefits

- **Zero Integration**: Legacy applications can access object storage without any code changes
- **Unified Namespace**: Same data accessible via S3 API, NFS, and SMB simultaneously
- **Consistency**: Changes made through one protocol are immediately visible through the others
- **Familiar Tools**: Use standard POSIX commands (`cp`, `mv`, `ls`, `cat`, `find`) to manage objects

## NFS Gateway

### Overview

The NFS gateway implements **NFSv3 protocol** with MOUNT protocol support. It serves StoreFS buckets as a standard NFS export that can be mounted by any NFSv3-compatible client.

### Features

- **NFSv3 Protocol**: Full NFSv3 RPC implementation with MOUNT protocol
- **Multiple Auth Types**: Supports `none` (no auth), `sys` (AUTH_UNIX), and `krb5` (Kerberos — declared but requires RPCSEC_GSS support)
- **User Mapping**: Maps NFS client credentials to a configured StoreFS user
- **Export Path Control**: Restrict client mounts to a specific sub-path
- **Read-Only Mode**: Export the filesystem as read-only
- **Connection Limits**: Configurable maximum concurrent client connections
- **File Handle Caching**: LRU-based file handle cache with configurable limit
- **Object Read Cache**: 512 MB default whole-object read cache

### Configuration

```yaml
gateway:
  nfs:
    enabled: true                 # Enable NFS gateway
    port: 2049                    # NFS service port (NFSv3 + MOUNT protocol)
    export_path: "/"              # Exported root path for NFS clients to mount
    auth_type: "none"             # Authentication type: "none" | "sys" | "krb5"
                                  #   - none: No authentication, use map_user for all clients
                                  #   - sys: UNIX authentication (AUTH_UNIX)
                                  #   - krb5: Kerberos authentication (requires RPCSEC_GSS)
    map_user: "user"              # StoreFS user mapped for NFS clients
    map_uid: 1000                 # UID exposed to NFS clients
    map_gid: 1000                 # GID exposed to NFS clients
    uid_mappings: {}              # UID → StoreFS username mappings (for auth_type=sys)
    read_only: false              # true disables write/delete operations
    max_connections: 1024         # Maximum cached NFS client connections (LRU eviction)
```

### Mounting NFS

#### Linux

```bash
# Install nfs-common if not already installed
sudo apt-get install nfs-common   # Debian/Ubuntu
sudo yum install nfs-utils        # CentOS/RHEL

# Mount the NFS export
sudo mount -t nfs -o vers=3,port=2049,noresvport <storefs-host>:/ /mnt/storefs

# Mount with specific export path
sudo mount -t nfs -o vers=3,port=2049 <storefs-host>:/exports /mnt/storefs

# Verify
df -h /mnt/storefs
ls -la /mnt/storefs/
```

**Note**: The `noresvport` option is recommended for better NFS reconnection behavior.

#### macOS

```bash
# Mount via Finder or command line
mkdir -p /mnt/storefs
mount_nfs -o vers=3,port=2049,mountport=2049 <storefs-host>:/ /mnt/storefs
```

#### Windows

Windows NFS client supports NFSv3:

```bash
# Enable NFS client (Windows Features → Services for NFS)
mount -o nolock -o mtype=hard \\<storefs-host>\ /mnt/storefs
```

### Usage Examples

```bash
# List buckets (top-level directories)
ls /mnt/storefs/

# List objects in a bucket
ls /mnt/storefs/mybucket/
ls -la /mnt/storefs/mybucket/

# Create a new directory (creates a zero-byte marker object)
mkdir /mnt/storefs/mybucket/projects

# Upload a file via NFS (equivalent to S3 PutObject)
cp report.pdf /mnt/storefs/mybucket/projects/

# Download a file via NFS (equivalent to S3 GetObject)
cp /mnt/storefs/mybucket/projects/report.pdf .

# Read file content
cat /mnt/storefs/mybucket/readme.txt

# View file attributes
stat /mnt/storefs/mybucket/file.txt

# Delete a file (equivalent to S3 DeleteObject)
rm /mnt/storefs/mybucket/old-file.txt

# Copy/move files (NFS rename)
mv /mnt/storefs/mybucket/file.txt /mnt/storefs/mybucket/renamed.txt
cp /mnt/storefs/mybucket/file.txt /mnt/storefs/mybucket/copy.txt

# Find files by name
find /mnt/storefs/mybucket/ -name "*.pdf"

# Check disk usage
du -sh /mnt/storefs/mybucket/
```

## SMB Gateway

### Overview

The SMB gateway implements **SMB 3.1.1 protocol** (with SMB 2.1 backward compatibility). It exports StoreFS buckets as an SMB share that can be mounted by Windows, Linux, and macOS clients.

### Features

- **SMB 3.1.1 Protocol**: Full SMB 3.1.1 implementation with SMB 2.1 backward compatibility
- **NTLM Authentication**: NTLMSSP-based authentication with username/password
- **Guest Access**: Optional anonymous/guest access (mapped to a configured StoreFS user)
- **Share-Level Export**: Single share name (`storefs` by default) exports the entire StoreFS namespace
- **Read-Only Mode**: Export the share as read-only
- **Workgroup Support**: Configurable NetBIOS workgroup/domain name
- **Concurrent Connections**: Configurable maximum concurrent client connections
- **Buffer Pooling**: 64 KB buffer pool for efficient data transfer

### Configuration

```yaml
gateway:
  smb:
    enabled: true                     # Enable SMB 3.1.1 gateway
    port: 4445                        # SMB service port (445 requires root)
    server_name: "STOREFS"            # NetBIOS server name advertised to clients
    workgroup: "WORKGROUP"            # Workgroup/domain name for NTLMSSP auth
    share_name: "storefs"             # Share name that clients mount
    guest_allowed: true               # Allow anonymous/guest access
    read_only: false                  # Export share as read-only
    map_user: "user"                  # StoreFS user for guest/anonymous connections
    max_connections: 1024             # Maximum concurrent client connections
```

### Mounting SMB

#### Linux

```bash
# Install cifs-utils if not already installed
sudo apt-get install cifs-utils    # Debian/Ubuntu
sudo yum install cifs-utils        # CentOS/RHEL

# Mount with credentials
sudo mount -t cifs //<storefs-host>:4445/storefs /mnt/storefs \
  -o username=<smb-user>,password=<smb-pass>,vers=3.1.1

# Mount with SMB 2.1 fallback
sudo mount -t cifs //<storefs-host>:4445/storefs /mnt/storefs \
  -o username=<smb-user>,password=<smb-pass>,vers=2.1

# Mount as guest
sudo mount -t cifs //<storefs-host>:4445/storefs /mnt/storefs \
  -o guest,vers=3.1.1

# Verify
df -h /mnt/storefs
ls -la /mnt/storefs/
```

#### macOS

```bash
# Connect via Finder
# Go → Connect to Server (Cmd+K)
# Enter: smb://<storefs-host>:4445/storefs

# Or via command line
mkdir -p /mnt/storefs
mount_smbfs //<smb-user>:<smb-pass>@<storefs-host>:4445/storefs /mnt/storefs
```

#### Windows

```bash
# Map network drive via command line
net use Z: \\<storefs-host>\storefs /user:<smb-user> <smb-pass>

# Or via File Explorer
# \\<storefs-host>\storefs
```

### Usage Examples

```bash
# List buckets (top-level directories)
ls /mnt/storefs/

# List objects in a bucket
ls /mnt/storefs/mybucket/

# Create a directory
mkdir /mnt/storefs/mybucket/data

# Upload a file
cp data.csv /mnt/storefs/mybucket/data/

# Download a file
cp /mnt/storefs/mybucket/data.csv .

# Read file content
cat /mnt/storefs/mybucket/readme.txt

# Delete a file
rm /mnt/storefs/mybucket/old-data.csv

# Rename/move file
mv /mnt/storefs/mybucket/data.csv /mnt/storefs/mybucket/archive/data.csv
```

## S3 ↔ NFS/SMB Interoperability

A key feature of the gateway is that data written through one protocol is immediately accessible through the others. This enables powerful hybrid workflows:

### S3 → NFS/SMB Workflow

Objects uploaded via the S3 API (PutObject, MultipartUpload) are immediately visible and readable through NFS or SMB mounts:

```bash
# Step 1: Upload via S3 API (using AWS CLI)
aws s3 cp report.pdf s3://mybucket/reports/report.pdf --endpoint-url http://127.0.0.1:8901

# Step 2: Read via NFS (immediately visible)
cat /mnt/storefs/mybucket/reports/report.pdf
```

### NFS/SMB → S3 Workflow

Files written through NFS or SMB mounts are immediately accessible via the S3 API:

```bash
# Step 1: Write via NFS
cp data.csv /mnt/storefs/mybucket/smb-nfs/data.csv

# Step 2: Download via S3 API (immediately available)
aws s3 cp s3://mybucket/smb-nfs/data.csv . --endpoint-url http://127.0.0.1:8901
```

### Use Cases

| Use Case | Description | Recommended Protocol |
|----------|-------------|---------------------|
| Legacy application migration | Applications that only support file system access | NFS (Linux) / SMB (Windows) |
| Media streaming | Video/audio files served to media players via file system | NFS |
| CI/CD pipelines | Build artifacts shared between S3 and build servers | SMB |
| Data backup | Backup software that writes to network shares | SMB |
| Cross-platform file sharing | Teams using different operating systems | SMB |
| High-throughput data processing | Large file transfers with standard POSIX tools | NFS |
| Hybrid cloud storage | Data accessible via both S3 API and file system | Both |

## Authentication

### NFS Authentication

| Auth Type | Description |
|-----------|-------------|
| `none` | No authentication. All clients are mapped to the configured `map_user`. |
| `sys` | AUTH_UNIX authentication. Client provides UID/GID. Currently all clients are still mapped to the configured StoreFS user, with `uid_mappings` reserved for future per-UID user mapping. |
| `krb5` | Kerberos 5 authentication. Declared but requires RPCSEC_GSS support not yet available. |

### SMB Authentication

| Auth Method | Description |
|-------------|-------------|
| NTLMSSP | Standard NTLM authentication with username and password. The credentials are validated against StoreFS users. |
| Guest | Anonymous access. When enabled, guest connections are mapped to the configured `map_user`. |

### Common User Mapping

Both gateways map external client identities to internal StoreFS users:

1. **NFS**: All NFS clients (regardless of auth type) are mapped to the single `map_user` configured in `nfs` section.
2. **SMB**: Authenticated SMB users are validated against StoreFS user credentials. Guest connections are mapped to `map_user` in `smb` section.

The mapped StoreFS user determines:
- Which buckets the client can access (based on the user's bucket ownership and permissions)
- Authorization for operations (read/write/delete) based on bucket policies

## Performance Considerations

- **Directory Listing Cache**: A 5-second TTL cache reduces redundant S3 LIST operations for directory listings.
- **Object Read Cache**: The NFS gateway includes a 512 MB whole-object read cache to improve repeated read performance.
- **Buffer Pooling**: The SMB gateway uses a 64 KB buffer pool to reduce memory allocation overhead.
- **Concurrent Connections**: Both gateways support configurable maximum concurrent connections to control resource usage.
- **NFS Handle Limit**: The NFS gateway maintains an LRU cache of file handles (default 1024) for efficient handle reuse.

## Limitations

- **NFSv3 Only**: The NFS gateway implements NFSv3, not NFSv4. Clients must use NFSv3 (`vers=3`).
- **No NFSv4 ACLs**: NFSv4 ACLs are not supported. Use StoreFS bucket policies or S3 ACLs for access control.
- **Directory Emulation**: S3 is a flat key-value store. Directories are emulated with zero-byte marker objects and may not behave identically to native filesystem directories.
- **No Hard Link Support**: POSIX hard links are not supported (S3 has no concept of hard links).
- **No Symbolic Link Support**: POSIX symbolic links are not supported (S3 has no concept of symlinks).
- **No File Locking**: POSIX file locking (`flock`, `fcntl`) is not supported.
- **No Extended Attributes**: POSIX extended attributes (xattr) are not supported.
- **SMB Port**: Port 445 (standard SMB) requires root privileges. Use a non-privileged port (e.g., 4445) for testing.

## Troubleshooting

### NFS Mount Issues

**Problem**: `mount.nfs: Connection refused`
**Solution**: Ensure the NFS gateway is enabled in the config and the StoreFS node is running. Verify the port (default 2049) is not blocked by a firewall.

**Problem**: `mount.nfs: access denied by server while mounting`
**Solution**: Check the `export_path` configuration. The client's mount path must match the export path.

**Problem**: Permission denied when accessing files
**Solution**: Verify the `map_user` exists in StoreFS and has appropriate permissions on the target buckets.

### SMB Mount Issues

**Problem**: `mount error(13): Permission denied`
**Solution**: Verify the SMB username and password. Check that the user exists in StoreFS and the password is correct.

**Problem**: `mount error(112): Host is down`
**Solution**: Ensure the SMB gateway is enabled and the port is accessible. If using port 445, verify the service has root privileges.

**Problem**: Unable to write files
**Solution**: Check if `read_only` is set to `true` in the SMB configuration.