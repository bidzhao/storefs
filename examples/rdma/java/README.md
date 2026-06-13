# s3rdmaget / s3rdmaput (Java / Maven)

Java re-implementation of the C / Python s3rdmaget / s3rdmaput tools.
Requires Java 11+.

The libibverbs (RDMA) operations are done via the same tiny C shim used by
the Python version (`native/verbs_shim.c`), compiled into
`native/librdmaverbs.so` and called from Java with JNA (`com.sun.jna`).
Everything else (websocket control channel via `java.net.http.WebSocket`,
JSON via `org.json`, SigV4 signing, raw-socket HTTP) is plain Java.

## Build

1. Compile the native verbs shim (requires `libibverbs-dev`):

   ```sh
   cd native && make && cd ..
   ```

   This produces `native/librdmaverbs.so`.

2. Build the jar:

   ```sh
   mvn package
   ```

   This produces `target/s3rdma.jar` with JNA and org.json bundled.

## Usage

```sh
java -Djna.library.path=native -cp target/s3rdma.jar com.example.s3rdma.S3RdmaGet \
    -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]

java -Djna.library.path=native -cp target/s3rdma.jar com.example.s3rdma.S3RdmaPut \
    -bucket <bucket> -object <key> -file <path> \
    [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak admin-ak] [-sk admin-sk]
```

`-Djna.library.path=native` (or any directory containing
`librdmaverbs.so`, or add it to `LD_LIBRARY_PATH`) is required so JNA can
find the native verbs shim.
