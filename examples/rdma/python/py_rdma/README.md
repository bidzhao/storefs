# s3rdmaget / s3rdmaput (Python)

Python (2.7 and 3.x compatible) re-implementation of the C s3rdmaget /
s3rdmaput tools.

The libibverbs (RDMA) operations are done via a tiny C shim
(`verbs_shim.c`) compiled into `librdmaverbs.so` and called from Python
with `ctypes`. Everything else (websocket control channel, JSON protocol,
SigV4 signing, S3 HTTP requests) is pure Python.

## Build

Requires libibverbs headers (`libibverbs-dev` on Debian/Ubuntu):

```sh
make
mv librdmaverbs.so ../
```

This produces `librdmaverbs.so` in this directory (rdmalib.py loads it via
a path relative to itself).

## Python dependencies

```sh
pip install -r requirements.txt --break-system-packages
```

(`websocket-client` only; everything else is stdlib.)

## Usage

```sh
python s3rdmaget.py -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]

python s3rdmaput.py -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]
```
