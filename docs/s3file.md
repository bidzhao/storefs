**[查看中文版](s3file_cn.md)**

# s3file - S3 File System CLI

A command-line tool for interacting with S3-compatible storage services, featuring both interactive and silent modes.

## Features

- **Interactive Shell Mode**: Navigate S3 storage like a local file system
- **Silent Mode**: Execute commands programmatically with script-friendly output
- **Local File Operations**: Use `--local` flag to run commands on the local filesystem
- **Multi-Provider Support**: Works with StoreFS, MinIO, AWS S3, and all S3-compatible services
- **Pagination Support**: Browse large directories with interactive or non-interactive pagination
- **Command History**: Navigate through previous commands
- **Auto-completion**: Tab completion for commands with flag hints
- **Wildcard Support**: Use `*` and `?` for fuzzy matching
- **Cancel Support**: Press Ctrl+C during any transfer to abort in-flight operations
- **Progress Display**: Real-time progress bar and percentage during uploads and downloads
- **Batch Operations**: Recursive directory upload/download, batch rename/move, dry-run preview

## Installation

```bash
# Run directly
s3file --help
```

## Usage

### Interactive Mode

```bash
# Start interactive mode (connects to localhost:8901 by default)
s3file

# Connect to AWS S3
s3file --endpoint https://s3.amazonaws.com --region us-east-1 --ssl

# Connect to MinIO
s3file --endpoint play.min.io --access-key Q3AM3UQ867SPQQA43P2F --secret-key zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG --ssl
```

### Silent Mode

Silent mode is designed for scripting and automation. Multiple commands can be separated by `;`:

```bash
# List all buckets
s3file --silent --command 'buckets'

# Execute multiple commands with ; separator
s3file --silent --connect localhost:9000 --command 'cd s3://mybucket; ls'

# Silent mode ls uses pagination with defaults: page=1, pageSize=20
s3file --silent --command 'cd s3://mybucket; ls --page 1 --pageSize 50'

# Exit codes: 0 on success, 1 on error
s3file --silent --command 'buckets' && echo "Success"

# Upload file using silent mode
s3file --silent --command 'cd s3://mybucket; upload /path/to/local.txt remote.txt'

# Download file using silent mode
s3file --silent --command 'cd s3://mybucket; download remote.txt /path/to/local.txt'
```

## Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--help, -h` | Show help message | - |
| `--silent` | Run in silent mode (non-interactive) | - |
| `--command <cmd>` | Command(s) to execute in silent mode (use `;` for multiple) | - |
| `--connect <url>` | Connect to endpoint before executing command | - |
| `--endpoint <url>` | S3 endpoint URL | `localhost:8901` |
| `--region <region>` | AWS region | `us-east-1` |
| `--access-key <key>` | Access key | `admin-ak` |
| `--secret-key <key>` | Secret key | `admin-sk` |
| `--ssl` | Use SSL/TLS for connection | `false` |
| `--no-banner` | Suppress banner display | - |

## StoreFS Detection

When connecting to a StoreFS server, s3file automatically detects it by checking the `X-StoreFS-Version` header in S3 API responses. This enables optimized features:

### Fast Directory Listing

When StoreFS is detected, `ls` uses the **Admin API** (`/api/buckets/{name}/entries`) instead of S3 `ListObjectsV2` to list directory contents. This provides significantly faster listing performance because it queries the metadata database directly rather than scanning object storage.

The fast path is used automatically — no additional configuration is needed. If the Admin API is unavailable, s3file gracefully falls back to the standard S3 `ListObjectsV2` method.

### Connection Info

Use the `info` command to check StoreFS detection status:

```
no bucket selected> info
────────────────────────────────────────────────────────────
Endpoint:       http://127.0.0.1:8901
Region:         us-east-1
...
StoreFS:        Detected (version v0.4.1)
Admin API:      http://127.0.0.1:7946
Admin API:      Connected
────────────────────────────────────────────────────────────
```

If the server is not a StoreFS instance, it will show:
```
StoreFS:        Not detected
```

## Hidden Files

By default, `ls` in StoreFS mode hides internal system files (names starting with `_sys_`). Use `ls --all` or `ls -a` to display all entries including system files.

## Environment Variables

| Variable | Description |
|----------|-------------|
| `AWS_ENDPOINT_URL` | S3 endpoint URL |
| `AWS_REGION` | AWS region |
| `AWS_ACCESS_KEY_ID` | Access key |
| `AWS_SECRET_ACCESS_KEY` | Secret key |

## Interactive Commands

### Basic Navigation

| Command | Description |
|---------|-------------|
| `ls [--no-page] [--page <n>] [--pageSize <n>]` | List current directory contents (paginated by default) |
| `cd <directory>` | Change directory (supports `s3://bucket`, `..`, `/`) |
| `pwd` | Print current path, or local CWD with `--local` |
| `mkdir <dir>` | Create directory, or locally with `--local` |
| `clear` | Clear screen |

**cd command details:**

- `cd s3://bucket` — switch to bucket
- `cd s3://bucket/path` — switch to bucket and change to subdirectory
- `cd /` — at root: exit bucket; in subdirectory: go to root
- `cd ..` — go to parent directory or exit bucket if at root
- `cd` with `--local` — change local working directory

### File Operations

| Command | Description |
|---------|-------------|
| `cp <src> <dst>` | Copy file/directory (auto-detects upload/download), or locally with `--local` |
| `cat <file>` | Display file contents, or locally with `--local` |
| `upload, up <local> <remote>` | Upload a file or directory |
| `download, dl <remote> <local>` | Download a file or directory |
| `mv <source> <destination>` | Rename or move S3 objects (single, directory, or wildcard) |
| `rename <pattern> <replacement>` | Batch rename using wildcard pattern matching |
| `lv <object>` | List all versions of an object (reverse chronological order) |

#### cp Command Options

| Option | Description |
|--------|-------------|
| `-u, --upload` | Force upload mode |
| `-d, --download` | Force download mode |
| `-r, --recursive` | Recursively copy directories |
| `-f, --force` | Force overwrite without confirmation |
| `-n, --no-clobber` | Do not overwrite existing files |
| `--versionId <id>` | Download a specific version of the object (download mode only) |
| `--dry-run` | Preview recursive copy |

#### upload/download Options

| Option | Description |
|--------|-------------|
| `-r, --recursive` | Recursively upload/download a directory |
| `-f, --force` | Force overwrite without confirmation |
| `-n, --no-clobber` | Do not overwrite existing files |
| `--versionId <id>` | Download a specific version of the object (download only) |
| `--dry-run` | Preview what would be transferred |

**Path resolution for local files:**
- `upload` and `cp --upload`: local source path is resolved relative to local CWD
- `download` and `cp --download`: local destination path is resolved relative to local CWD
- Download destination `./` or `./dir/` automatically appends the remote filename

#### mv Command Options

| Option | Description |
|--------|-------------|
| `-f, --force` | Force overwrite if destination exists |
| `--dry-run` | Only show what would be moved |

**mv Examples:**

```bash
mv old.txt new.txt              # Single file rename
mv olddir/ newdir/              # Directory (prefix) rename (trailing / optional)
mv olddir newdir                 # Same — auto-detected as directory prefix
mv '*.log' /logs/               # Wildcard: move matching files
```

#### rename Command Options

| Option | Description |
|--------|-------------|
| `-f, --force` | Force overwrite if destination exists |
| `--dry-run` | Only show what would be renamed |

**rename Examples:**

The pattern is matched against the relative object name (not full path).
Use `$1`, `$2` etc. in the replacement to reference wildcard matches.

```bash
rename '*.log' '*.bak'                  # Change extension
rename 'report-*.txt' 'backup-$1.txt'   # Prefix substitution
```

### Delete Operations

| Command | Description |
|---------|-------------|
| `rm [options] <target>` | Delete file or directory, or locally with `--local` |

#### rm Command Options

| Option | Description |
|--------|-------------|
| `-r, --recursive` | Recursively delete directory and its contents |
| `-f, --force` | Force deletion without confirmation |
| `--dry-run` | Only show what would be deleted (queries S3 to list matching files) |

#### rm Examples

```bash
rm file.txt                    # Delete a single file
rm -r mydir/                   # Recursively delete directory
rm file1.txt file2.txt         # Delete multiple files
rm *.txt                       # Delete all txt files
rm -r --dry-run mydir/         # Preview deletion (lists actual files from S3)
```

### Bucket Management

| Command | Description |
|---------|-------------|
| `buckets` | List all buckets |
| `use <bucket>` | Switch to specified bucket |
| `mb <bucket> [--versioning]` | Create a new bucket (optionally with versioning enabled) |
| `exit-bucket, root` | Exit current bucket, return to no-bucket state |

### Connection Management

| Command | Description |
|---------|-------------|
| `connect <endpoint> [region] [access-key] [secret-key] [--ssl]` | Connect to a different S3 service |
| `connect -i, --interactive` | Interactive mode for connecting |
| `info, connection` | Show current connection information |

#### connect Examples

```bash
connect localhost:9000
connect localhost:9000 us-east-1 admin admin123
connect https://s3.amazonaws.com us-east-1 AKIA... SK... --ssl
connect play.min.io us-east-1 Q3AM3UQ867SPQQA43P2F zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG --ssl
connect --interactive
```

### Configuration

| Command | Description |
|---------|-------------|
| `pagesize [num]` | Set or view items per page (5-100). Serves as default for `ls --pageSize`. |

### Version Management

| Command | Description |
|---------|-------------|
| `lv <object>` | List all versions of an object in reverse chronological order |
| `download --versionId <id> <remote> <local>` | Download a specific version of an object |
| `cp --versionId <id> <source> <destination>` | Copy a specific version of an object (download mode only) |
| `mb <bucket> --versioning` | Create a bucket with versioning enabled |

The `lv` command displays:
- All versions of the specified object
- Which version is the latest (marked with ✓ yes)
- Last modified time for each version
- Size of each version
- Delete markers (if any)

### Cancel Operations

Press **Ctrl+C** at any time during file transfers to abort:

- **Single file upload/download**: the in-flight HTTP request is cancelled immediately, and a "⏹ Cancelled" message is shown.
- **Batch operations** (`upload -r`, `download -r`, `cp -r`, `mv`, `rename`, `rm -r`): the current in-flight file transfer is aborted, and remaining files are skipped. Already processed files are preserved.

### Other Commands

| Command | Description |
|---------|-------------|
| `help, ?` | Show help |
| `history` | Show command history |
| `exit, quit` | Exit program |

## Local File Operations (`--local` flag)

Many commands support the `--local` flag to operate on the local filesystem instead of S3.
The local working directory (CWD) is initialized from the shell's current directory and can be changed with `cd --local`.

| Command | Description |
|---------|-------------|
| `pwd --local` | Show local CWD |
| `cd --local <dir>` | Change local directory |
| `ls --local [--no-page] [--page <n>] [--pageSize <n>]` | List local directory (supports pagination) |
| `mkdir --local <dir>` | Create local directory |
| `cat --local <file>` | Show local file contents |
| `rm --local [options] <target>` | Delete local files/directories |
| `cp --local <src> <dst>` | Copy local files |

## Pagination Display

During `ls` in paginated mode:

| Key | Action |
|-----|--------|
| `n` or Enter | Next page |
| `p` | Previous page |
| `g <page>` | Go to specific page |
| `q` | Quit browsing |

Non-interactive pagination (silent mode, or `--page`/`--pageSize` flags):
```
=== Page X/Y (Total Z items, page size: W) ===
```

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| ↑ / ↓ | Navigate through command history |
| Ctrl+U | Clear current input line |
| Ctrl+C | Cancel current input or abort in-flight transfer |

## Interactive Mode Example Session

```bash
$ s3file
╔══════════════════════════════════════════════════════════════════════════╗
║                        S3 File System CLI v0.4.1                         ║
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
[Contents of notes.txt]

s3://mybucket/Documents/> cd /
Changed to root directory.

s3://mybucket/> cd ..
Exited bucket. No bucket selected.

no bucket selected> exit
Goodbye!
```

## Silent Mode Examples

```bash
# List buckets
s3file --silent --command 'buckets'

# Execute multiple commands with ; separator
s3file --silent --connect localhost:9000 --command 'cd s3://mybucket; ls'

# Silent mode ls uses pagination (default page=1, pageSize=20)
s3file --silent --command 'cd s3://mybucket; ls --page 1 --pageSize 50'

# Upload file
s3file --silent --command 'cd s3://mybucket; upload /path/to/local.txt remote.txt'

# Download file
s3file --silent --command 'cd s3://mybucket; download remote.txt /path/to/local.txt'

# Check exit code (0 = success, 1 = error)
s3file --silent --command 'buckets'
echo $?
```

## Tips

- **Multi-command**: Use `;` to separate multiple commands in `--command` for silent mode
- **Wildcards**: Use `*` and `?` for fuzzy matching in commands like `rm`, `mv`, and `rename`
- **Command History**: Use ↑/↓ arrows to navigate through previous commands
- **Path Formats**: Use `s3://bucket/path` to directly reference buckets and paths
- **Pagination**: `ls` paginates by default; use `--no-page` for raw output, `--page`/`--pageSize` for non-interactive page control
- **Page Size Default**: Set default with `pagesize <n>`, override per-command with `--pageSize <n>`
- **Local Operations**: Append `--local` to use commands on the local filesystem
- **Environment Variables**: Use environment variables for sensitive credentials instead of command line arguments
- **Local CWD**: Local working directory starts from the shell's current directory; change with `cd --local <dir>`
- **Cancel**: Press Ctrl+C to abort any upload, download, or batch operation in progress
- **Progress**: Uploads and downloads show a real-time progress bar: `[========>       ] 45.2%`
- **Dry Run**: Use `--dry-run` with `upload -r`, `download -r`, `cp -r`, `mv`, `rename`, or `rm` to preview changes without actually performing them
- **Directory Upload**: If you try to upload a directory without `-r`, s3file will tell you: "Use '-r' to upload recursively"

## License

This tool is part of the StoreFS project.
