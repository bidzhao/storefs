**[查看中文版](s3file_cn.md)**

# s3file - S3 File System CLI

A command-line tool for interacting with S3-compatible storage services, featuring both interactive and silent modes.

## Features

- **Interactive Shell Mode**: Navigate S3 storage like a local file system
- **Silent Mode**: Execute commands programmatically
- **Multi-Provider Support**: Works with StoreFS, MinIO, AWS S3, and all S3-compatible services
- **Pagination Support**: Browse large directories with ease
- **Command History**: Navigate through previous commands
- **Auto-completion**: Tab completion for commands
- **Wildcard Support**: Use * and ? for fuzzy matching

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

```bash
# List all buckets
s3file --silent --command 'buckets'

# Connect to server and list buckets
s3file --silent --connect localhost:9000 --command 'buckets'

# Connect to MinIO and list bucket contents
s3file --silent --connect play.min.io --access-key Q3AM3UQ867SPQQA43P2F --secret-key zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG --ssl --command 'cd s3://mybucket' --command 'ls'
```

## Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--help, -h` | Show help message | - |
| `--silent` | Run in silent mode (non-interactive) | - |
| `--command <cmd>` | Command to execute in silent mode | - |
| `--connect <url>` | Connect to endpoint before executing command | - |
| `--endpoint <url>` | S3 endpoint URL | `localhost:8901` |
| `--region <region>` | AWS region | `us-east-1` |
| `--access-key <key>` | Access key | `admin-ak` |
| `--secret-key <key>` | Secret key | `admin-sk` |
| `--ssl` | Use SSL/TLS for connection | `false` |
| `--no-banner` | Suppress banner display | - |

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
| `ls [--no-page]` | List current directory contents (paginated by default) |
| `cd <directory>` | Change directory (supports `s3://bucket` format) |
| `pwd` | Print current path |
| `mkdir <dir>` | Create directory |
| `clear` | Clear screen |

### File Operations

| Command | Description |
|---------|-------------|
| `cp <src> <dst>` | Copy file (auto-detects upload/download) |
| `cat <file>` | Display file contents |
| `upload, up <local> <remote>` | Upload a file |
| `download, dl <remote> <local>` | Download a file |

#### cp Command Options

| Option | Description |
|--------|-------------|
| `-u, --upload` | Force upload mode |
| `-d, --download` | Force download mode |
| `-f, --force` | Force overwrite without confirmation |
| `-n, --no-clobber` | Do not overwrite existing files |

#### upload/download Options

| Option | Description |
|--------|-------------|
| `-f, --force` | Force overwrite without confirmation |

### Delete Operations

| Command | Description |
|---------|-------------|
| `rm [options] <target>` | Delete file or directory |

#### rm Command Options

| Option | Description |
|--------|-------------|
| `-r, --recursive` | Recursively delete directory and its contents |
| `-f, --force` | Force deletion without confirmation |
| `--dry-run` | Only show what would be deleted |

#### rm Examples

```bash
rm file.txt                    # Delete a single file
rm -r mydir/                   # Recursively delete directory
rm file1.txt file2.txt         # Delete multiple files
rm *.txt                       # Delete all txt files
rm -r --dry-run mydir/         # Preview deletion
```

### Bucket Management

| Command | Description |
|---------|-------------|
| `buckets` | List all buckets |
| `use <bucket>` | Switch to specified bucket |
| `mb <bucket>` | Create a new bucket |

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
| `pagesize [num]` | Set or view items per page (5-100) |

### Other Commands

| Command | Description |
|---------|-------------|
| `help, ?` | Show help |
| `history` | Show command history |
| `exit, quit` | Exit program |

## Pagination Commands

During `ls` in paginated mode:

| Key | Action |
|-----|--------|
| `n` or Enter | Next page |
| `p` | Previous page |
| `g <page>` | Go to specific page |
| `q` | Quit browsing |

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| ↑ / ↓ | Navigate through command history |
| Ctrl+U | Clear current input line |
| Ctrl+C | Cancel current input |

## Interactive Mode Example Session

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
[Contents of notes.txt]

s3://mybucket/Documents/> exit
Goodbye!
```

## Silent Mode Examples

```bash
# List buckets
s3file --silent --command 'buckets'

# Execute multiple commands
s3file --silent --connect localhost:9000 --command 'cd s3://mybucket' --command 'ls'

# Upload file using silent mode
s3file --silent --command 'cd s3://mybucket' --command 'upload /path/to/local.txt remote.txt'

# Download file using silent mode
s3file --silent --command 'cd s3://mybucket' --command 'download remote.txt /path/to/local.txt'
```

## Tips

- **Wildcards**: Use `*` and `?` for fuzzy matching in commands like `rm`
- **Command History**: Use ↑/↓ arrows to navigate through previous commands
- **Path Formats**: Use `s3://bucket/path` to directly reference buckets and paths
- **Pagination**: For large directories, use `ls` without `--no-page` to browse paginated results
- **Environment Variables**: Use environment variables for sensitive credentials instead of command line arguments

## License

This tool is part of the StoreFS project.
