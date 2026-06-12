// s3rdmaget.c - download an S3 object via RDMA, using rdmalib + s3client.
//
// Usage:
//   ./s3rdmaget -bucket <bucket> -object <key> -file <path>
//                [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0]
//                [-ak <access-key>] [-sk <secret-key>]
#include "rdmalib.h"
#include "s3client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <curl/curl.h>

static const uint8_t RDMA_PORT = 1;
static const uint8_t RDMA_GID_IDX = 0;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -bucket <bucket> -object <key> -file <path> "
        "[-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0] [-ak <ak>] [-sk <sk>]\n", prog);
    exit(1);
}

/* build "ws://host:port/rdma-ctrl" from "http://host:port" */
static void ws_url_from_endpoint(const char *endpoint, char *out, size_t outlen) {
    const char *p = strstr(endpoint, "://");
    p = p ? p + 3 : endpoint;
    snprintf(out, outlen, "ws://%s/rdma-ctrl", p);
}

int main(int argc, char **argv) {
    const char *bucket = NULL, *object = NULL, *filePath = NULL;
    const char *endpoint = "http://127.0.0.1:8901";
    const char *rdmaDev = "rxe0";
    const char *ak = "admin-ak", *sk = "admin-sk";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-bucket") && i + 1 < argc) bucket = argv[++i];
        else if (!strcmp(argv[i], "-object") && i + 1 < argc) object = argv[++i];
        else if (!strcmp(argv[i], "-file") && i + 1 < argc) filePath = argv[++i];
        else if (!strcmp(argv[i], "-endpoint") && i + 1 < argc) endpoint = argv[++i];
        else if (!strcmp(argv[i], "-rdma-dev") && i + 1 < argc) rdmaDev = argv[++i];
        else if (!strcmp(argv[i], "-ak") && i + 1 < argc) ak = argv[++i];
        else if (!strcmp(argv[i], "-sk") && i + 1 < argc) sk = argv[++i];
        else usage(argv[0]);
    }
    if (!bucket || !object || !filePath) usage(argv[0]);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    s3_config_t cfg = {
        .endpoint = endpoint,
        .region = "us-east-1",
        .access_key = ak,
        .secret_key = sk,
    };

    char err[256];

    // Step 1: HEAD to get object size
    fprintf(stderr, "Getting object info first...\n");
    long long fileSize = 0;
    if (s3_head_object(&cfg, bucket, object, &fileSize, err, sizeof(err)) != 0) {
        fprintf(stderr, "Failed to get object info: %s\n", err);
        return 1;
    }
    fprintf(stderr, "Object size: %lld bytes\n", fileSize);

    char requestID[64];
    snprintf(requestID, sizeof(requestID), "rdma-%d", getpid());

    // Step 2: mmap a buffer to receive the data via RDMA write
    void *buf = mmap(NULL, (size_t)fileSize, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap: %s\n", strerror(errno));
        return 1;
    }

    // Step 3: RDMA handshake (register, QP setup, send token)
    char wsURL[300];
    ws_url_from_endpoint(endpoint, wsURL, sizeof(wsURL));

    fprintf(stderr, "Initializing RDMA resources and exchanging QP info...\n");
    rdma_session_t *sess = rdma_recv_setup(wsURL, rdmaDev, RDMA_PORT, RDMA_GID_IDX,
                                            requestID, buf, (size_t)fileSize,
                                            err, sizeof(err));
    if (!sess) {
        fprintf(stderr, "RDMA setup failed: %s\n", err);
        munmap(buf, (size_t)fileSize);
        return 1;
    }

    // Step 4: trigger the server-side RDMA write via S3 GetObject
    fprintf(stderr, "Sending S3 GetObject request...\n");
    if (s3_get_object(&cfg, bucket, object, "X-RDMA-Request-ID", requestID, err, sizeof(err)) != 0) {
        fprintf(stderr, "GetObject error: %s\n", err);
        rdma_session_destroy(sess);
        munmap(buf, (size_t)fileSize);
        return 1;
    }
    fprintf(stderr, "Successfully requested %s from s3://%s/%s via RDMA (HTTP 200 OK)\n",
            filePath, bucket, object);

    // Step 5: RDMA write is complete now; release RDMA resources
    rdma_session_destroy(sess);

    // Step 6: verify CRC and save to file
    uint32_t crc = rdma_crc32(buf, (size_t)fileSize);
    fprintf(stderr, "Data CRC32: 0x%08x\n", crc);

    fprintf(stderr, "Saving data to %s...\n", filePath);
    FILE *f = fopen(filePath, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open %s: %s\n", filePath, strerror(errno));
        munmap(buf, (size_t)fileSize);
        return 1;
    }
    if (fwrite(buf, 1, (size_t)fileSize, f) != (size_t)fileSize) {
        fprintf(stderr, "Failed to write file: %s\n", strerror(errno));
        fclose(f);
        munmap(buf, (size_t)fileSize);
        return 1;
    }
    fclose(f);
    munmap(buf, (size_t)fileSize);

    fprintf(stderr, "Download complete! File saved to %s\n", filePath);
    return 0;
}
