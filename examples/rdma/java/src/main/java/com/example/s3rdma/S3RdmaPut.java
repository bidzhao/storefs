package com.example.s3rdma;

import java.net.URI;
import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * s3rdmaput - upload a file to S3 via RDMA, using RdmaSession + S3Client.
 *
 * Usage:
 *   java -Djna.library.path=native -cp target/s3rdma.jar com.example.s3rdma.S3RdmaPut \
 *        -bucket <bucket> -object <key> -file <path>
 *        [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak <ak>] [-sk <sk>]
 */
public class S3RdmaPut {

    private static void usage() {
        System.err.println("Usage: S3RdmaPut -bucket <bucket> -object <key> -file <path> "
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

        // Step 1: read the file into memory
        byte[] data;
        try {
            data = Files.readAllBytes(Paths.get(filePath));
        } catch (Exception e) {
            System.err.println("Failed to read file: " + e.getMessage());
            System.exit(1);
            return;
        }

        String requestId = "rdma-" + ProcessHandle.current().pid();

        // Step 2: RDMA handshake (register, QP setup, copy data into MR, send token)
        String wsUrl = wsUrlFromEndpoint(endpoint);
        System.err.println("Initializing RDMA resources and exchanging QP info...");
        RdmaSession sess;
        try {
            sess = RdmaSession.sendSetup(wsUrl, rdmaDev, RdmaSession.RDMA_PORT, RdmaSession.RDMA_GID_IDX,
                    requestId, data);
        } catch (Exception e) {
            System.err.println("RDMA setup failed: " + e.getMessage());
            System.exit(1);
            return;
        }

        // Step 3: trigger the server-side RDMA read via S3 PutObject (empty body)
        System.err.println("Sending S3 PutObject request...");
        try {
            cfg.putObject(bucket, object, "X-RDMA-Request-ID", requestId);
        } catch (Exception e) {
            System.err.println("PutObject error: " + e.getMessage());
            sess.destroy();
            System.exit(1);
            return;
        }

        System.err.println("Successfully uploaded " + filePath + " to s3://" + bucket + "/" + object
                + " via RDMA (HTTP 200 OK)");

        // Step 4: RDMA read is complete now; release RDMA resources
        sess.destroy();
    }
}
