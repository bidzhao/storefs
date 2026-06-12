// s3rdmaput.c - upload a file to S3 via RDMA, using rdmalib + s3client.
//
// Usage:
//   ./s3rdmaput -bucket <bucket> -object <key> -file <path>
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
#include <sys/stat.h>
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

    // Step 1: read the file into memory
    FILE *f = fopen(filePath, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open %s: %s\n", filePath, strerror(errno));
        return 1;
    }
    struct stat st;
    if (fstat(fileno(f), &st) != 0) {
        fprintf(stderr, "Failed to stat %s: %s\n", filePath, strerror(errno));
        fclose(f);
        return 1;
    }
    size_t fileSize = (size_t)st.st_size;

    // Step 2: mmap a buffer and copy the file contents into it (so it can be
    // registered as an RDMA memory region)
    void *buf = mmap(NULL, fileSize ? fileSize : 1, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap: %s\n", strerror(errno));
        fclose(f);
        return 1;
    }
    if (fileSize > 0 && fread(buf, 1, fileSize, f) != fileSize) {
        fprintf(stderr, "Failed to read file: %s\n", strerror(errno));
        fclose(f);
        munmap(buf, fileSize ? fileSize : 1);
        return 1;
    }
    fclose(f);

    char requestID[64];
    snprintf(requestID, sizeof(requestID), "rdma-%d", getpid());

    // Step 3: RDMA handshake (register, QP setup, send token)
    char wsURL[300];
    ws_url_from_endpoint(endpoint, wsURL, sizeof(wsURL));

    fprintf(stderr, "Initializing RDMA resources and exchanging QP info...\n");
    rdma_session_t *sess = rdma_send_setup(wsURL, rdmaDev, RDMA_PORT, RDMA_GID_IDX,
                                            requestID, buf, fileSize,
                                            err, sizeof(err));
    if (!sess) {
        fprintf(stderr, "RDMA setup failed: %s\n", err);
        munmap(buf, fileSize ? fileSize : 1);
        return 1;
    }

    // Step 4: trigger the server-side RDMA read via S3 PutObject (empty body)
    fprintf(stderr, "Sending S3 PutObject request...\n");
    if (s3_put_object(&cfg, bucket, object, "X-RDMA-Request-ID", requestID, err, sizeof(err)) != 0) {
        fprintf(stderr, "PutObject error: %s\n", err);
        rdma_session_destroy(sess);
        munmap(buf, fileSize ? fileSize : 1);
        return 1;
    }
    fprintf(stderr, "Successfully uploaded %s to s3://%s/%s via RDMA (HTTP 200 OK)\n",
            filePath, bucket, object);

    // Step 5: RDMA read is complete now; release RDMA resources
    rdma_session_destroy(sess);
    munmap(buf, fileSize ? fileSize : 1);

    return 0;
}
