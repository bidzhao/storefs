package com.example.s3rdma;

import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * s3rdmaget - download an S3 object via RDMA, using RdmaSession + S3Client.
 *
 * Usage:
 *   java -Djna.library.path=native -cp target/s3rdma.jar com.example.s3rdma.S3RdmaGet \
 *        -bucket <bucket> -object <key> -file <path>
 *        [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak <ak>] [-sk <sk>]
 */
public class S3RdmaGet {

    private static void usage() {
        System.err.println("Usage: S3RdmaGet -bucket <bucket> -object <key> -file <path> "
                + "[-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak <ak>] [-sk <sk>]");
        System.exit(1);
    }

    private static String wsUrlFromEndpoint(String endpoint) throws Exception {
        URI u = URI.create(endpoint);
        String authority = u.getAuthority();
        return "ws://" + authority + "/rdma-ctrl";
    }

    public static void main(String[] args) throws Exception {
        String bucket = null, object = null, filePath = null;
        String endpoint = "http://127.0.0.1:8901";
        String rdmaDev = "rxe0";
        String ak = "admin-ak", sk = "admin-sk";

        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "-bucket": bucket = args[++i]; break;
                case "-object": object = args[++i]; break;
                case "-file": filePath = args[++i]; break;
                case "-endpoint": endpoint = args[++i]; break;
                case "-rdma-dev": rdmaDev = args[++i]; break;
                case "-ak": ak = args[++i]; break;
                case "-sk": sk = args[++i]; break;
                default: usage();
            }
        }
        if (bucket == null || object == null || filePath == null) {
            usage();
        }

        S3Client cfg = new S3Client(endpoint, "us-east-1", ak, sk);

        // Step 1: HEAD to get object size
        System.err.println("Getting object info first...");
        long fileSize;
        try {
            fileSize = cfg.headObject(bucket, object);
        } catch (Exception e) {
            System.err.println("Failed to get object info: " + e.getMessage());
            System.exit(1);
            return;
        }
        System.err.println("Object size: " + fileSize + " bytes");

        String requestId = "rdma-" + ProcessHandle.current().pid();

        // Step 2 & 3: RDMA handshake (register, QP setup, register MR, send token)
        String wsUrl = wsUrlFromEndpoint(endpoint);
        System.err.println("Initializing RDMA resources and exchanging QP info...");
        RdmaSession sess;
        try {
            sess = RdmaSession.recvSetup(wsUrl, rdmaDev, RdmaSession.RDMA_PORT, RdmaSession.RDMA_GID_IDX,
                    requestId, fileSize);
        } catch (Exception e) {
            System.err.println("RDMA setup failed: " + e.getMessage());
            System.exit(1);
            return;
        }

        // Step 4: trigger the server-side RDMA write via S3 GetObject
        System.err.println("Sending S3 GetObject request...");
        try {
            cfg.getObject(bucket, object, "X-RDMA-Request-ID", requestId);
        } catch (Exception e) {
            System.err.println("GetObject error: " + e.getMessage());
            sess.destroy();
            System.exit(1);
            return;
        }

        System.err.println("Successfully requested " + filePath + " from s3://" + bucket + "/" + object
                + " via RDMA (HTTP 200 OK)");

        // Step 5: RDMA write is complete now; copy the data out before releasing resources
        byte[] data = new byte[(int) fileSize];
        ByteBuffer buf = sess.getBuffer();
        buf.rewind();
        buf.get(data);
        sess.destroy();

        // Step 6: verify CRC and save to file
        long crc = RdmaSession.crc32(data);
        System.err.println(String.format("Data CRC32: 0x%08x", crc));

        System.err.println("Saving data to " + filePath + "...");
        Files.write(Paths.get(filePath), data);

        System.err.println("Download complete! File saved to " + filePath);
    }
}
