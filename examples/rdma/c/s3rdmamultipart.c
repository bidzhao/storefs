// s3rdmamultipart.c - multipart upload to S3 via RDMA, using rdmalib.
//
// Usage examples:
//   ./s3rdmamultipart -action all -bucket <bucket> -object <key> -file <path>
//   ./s3rdmamultipart -action create -bucket <bucket> -object <key>
//   ./s3rdmamultipart -action upload -bucket <bucket> -object <key> -file <path> -uploadid <id> [-part 2]
//   ./s3rdmamultipart -action complete -bucket <bucket> -object <key> -uploadid <id>
//   ./s3rdmamultipart -action abort -bucket <bucket> -object <key> -uploadid <id>
#include "rdmalib.h"
#include "s3client.h"

#include <errno.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

static const uint8_t RDMA_PORT = 1;
static const uint8_t RDMA_GID_IDX = 0;

typedef struct {
    int part_number;
    char etag[256];
    long long size;
} completed_part_t;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} response_buf_t;

typedef struct {
    const char *action;
    const char *bucket;
    const char *object;
    const char *file_path;
    const char *upload_id;
    const char *endpoint;
    const char *rdma_dev;
    const char *ak;
    const char *sk;
    const char *prefix;
    const char *delimiter;
    const char *key_marker;
    const char *upload_id_marker;
    long long part_size;
    long long max_uploads;
    int part;
} options_t;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -action <all|create|upload|list|list-uploads|complete|abort> -bucket <bucket> [options]\n"
        "\n"
        "Common options:\n"
        "  -bucket <bucket>              Bucket name (required)\n"
        "  -object <key>                 Object key (required except list-uploads)\n"
        "  -file <path>                  Local file (required for all/upload)\n"
        "  -uploadid <id>                Upload ID (required for upload/list/complete/abort)\n"
        "  -part <n>                     Upload one part; 0 means all parts (default)\n"
        "  -partsize <bytes>             Part size, default 5242880\n"
        "  -endpoint <url>               S3 endpoint, default http://127.0.0.1:8901\n"
        "  -rdma-dev <dev>               RDMA device, default rxe0\n"
        "  -ak <access-key>              Access key, default admin-ak\n"
        "  -sk <secret-key>              Secret key, default admin-sk\n"
        "\n"
        "List uploads options:\n"
        "  -prefix <prefix> -delimiter <delimiter> -max-uploads <n>\n"
        "  -key-marker <key> -upload-id-marker <id>\n",
        prog);
    exit(1);
}

static void init_options(options_t *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->action = "all";
    opts->endpoint = "http://127.0.0.1:8901";
    opts->rdma_dev = "rxe0";
    opts->ak = "admin-ak";
    opts->sk = "admin-sk";
    opts->part_size = 5LL * 1024 * 1024;
    opts->max_uploads = 1000;
}

static void parse_args(int argc, char **argv, options_t *opts) {
    init_options(opts);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-action") && i + 1 < argc) opts->action = argv[++i];
        else if (!strcmp(argv[i], "-bucket") && i + 1 < argc) opts->bucket = argv[++i];
        else if (!strcmp(argv[i], "-object") && i + 1 < argc) opts->object = argv[++i];
        else if (!strcmp(argv[i], "-file") && i + 1 < argc) opts->file_path = argv[++i];
        else if (!strcmp(argv[i], "-uploadid") && i + 1 < argc) opts->upload_id = argv[++i];
        else if (!strcmp(argv[i], "-part") && i + 1 < argc) opts->part = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-partsize") && i + 1 < argc) opts->part_size = atoll(argv[++i]);
        else if (!strcmp(argv[i], "-endpoint") && i + 1 < argc) opts->endpoint = argv[++i];
        else if (!strcmp(argv[i], "-rdma-dev") && i + 1 < argc) opts->rdma_dev = argv[++i];
        else if (!strcmp(argv[i], "-ak") && i + 1 < argc) opts->ak = argv[++i];
        else if (!strcmp(argv[i], "-sk") && i + 1 < argc) opts->sk = argv[++i];
        else if (!strcmp(argv[i], "-prefix") && i + 1 < argc) opts->prefix = argv[++i];
        else if (!strcmp(argv[i], "-delimiter") && i + 1 < argc) opts->delimiter = argv[++i];
        else if (!strcmp(argv[i], "-max-uploads") && i + 1 < argc) opts->max_uploads = atoll(argv[++i]);
        else if (!strcmp(argv[i], "-key-marker") && i + 1 < argc) opts->key_marker = argv[++i];
        else if (!strcmp(argv[i], "-upload-id-marker") && i + 1 < argc) opts->upload_id_marker = argv[++i];
        else usage(argv[0]);
    }

    if (!opts->bucket) usage(argv[0]);
    if (strcmp(opts->action, "list-uploads") != 0 && !opts->object) usage(argv[0]);
    if (opts->part_size <= 0) {
        fprintf(stderr, "partsize must be greater than 0\n");
        exit(1);
    }
}

static void ws_url_from_endpoint(const char *endpoint, char *out, size_t outlen) {
    const char *p = strstr(endpoint, "://");
    const char *scheme = "ws";
    if (p) {
        if (strncmp(endpoint, "https://", 8) == 0) scheme = "wss";
        p += 3;
    } else {
        p = endpoint;
    }
    snprintf(out, outlen, "%s://%s/rdma-ctrl", scheme, p);
}

static void sha256_hex(const unsigned char *data, size_t len, char out[65]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data, len, hash);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) sprintf(out + i * 2, "%02x", hash[i]);
    out[64] = 0;
}

static void hmac_sha256(const unsigned char *key, size_t keylen, const unsigned char *data, size_t datalen, unsigned char out[32]) {
    unsigned int outlen = 0;
    HMAC(EVP_sha256(), key, keylen, data, datalen, out, &outlen);
}

static void derive_signing_key(const char *secret, const char *date, const char *region, unsigned char out[32]) {
    unsigned char k_date[32], k_region[32], k_service[32];
    char k_secret[160];
    snprintf(k_secret, sizeof(k_secret), "AWS4%s", secret);
    hmac_sha256((unsigned char*)k_secret, strlen(k_secret), (unsigned char*)date, strlen(date), k_date);
    hmac_sha256(k_date, 32, (unsigned char*)region, strlen(region), k_region);
    hmac_sha256(k_region, 32, (unsigned char*)"s3", 2, k_service);
    hmac_sha256(k_service, 32, (unsigned char*)"aws4_request", 12, out);
}

static void host_from_endpoint(const char *endpoint, char *host, size_t hostlen) {
    const char *p = strstr(endpoint, "://");
    p = p ? p + 3 : endpoint;
    const char *slash = strchr(p, '/');
    size_t n = slash ? (size_t)(slash - p) : strlen(p);
    if (n >= hostlen) n = hostlen - 1;
    memcpy(host, p, n);
    host[n] = 0;
}

static void sign_request(const s3_config_t *cfg, const char *method, const char *bucket,
                         const char *key, const char *canonical_query, long long content_length,
                         char *host, size_t hostlen, char *amzdate, size_t amzdate_len,
                         char *authorization, size_t authlen) {
    host_from_endpoint(cfg->endpoint, host, hostlen);

    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);

    char datestamp[16];
    strftime(amzdate, amzdate_len, "%Y%m%dT%H%M%SZ", &tmv);
    strftime(datestamp, sizeof(datestamp), "%Y%m%d", &tmv);

    char canonical_uri[2048];
    if (key == NULL || key[0] == '\0') snprintf(canonical_uri, sizeof(canonical_uri), "/%s", bucket);
    else snprintf(canonical_uri, sizeof(canonical_uri), "/%s/%s", bucket, key);

    char canonical_headers[4096];
    const char *signed_headers = "host;x-amz-date";
    if (content_length >= 0) {
        snprintf(canonical_headers, sizeof(canonical_headers),
                 "content-length:%lld\nhost:%s\nx-amz-date:%s\n", content_length, host, amzdate);
        signed_headers = "content-length;host;x-amz-date";
    } else {
        snprintf(canonical_headers, sizeof(canonical_headers), "host:%s\nx-amz-date:%s\n", host, amzdate);
    }
    const char *payload_hash = "UNSIGNED-PAYLOAD";

    char canonical_request[8192];
    int cr_len = snprintf(canonical_request, sizeof(canonical_request), "%s\n%s\n%s\n%s\n%s\n%s",
                          method, canonical_uri, canonical_query ? canonical_query : "",
                          canonical_headers, signed_headers, payload_hash);

    char creq_hash[65];
    sha256_hex((unsigned char*)canonical_request, (size_t)cr_len, creq_hash);

    char scope[128];
    snprintf(scope, sizeof(scope), "%s/%s/s3/aws4_request", datestamp, cfg->region);

    char string_to_sign[4096];
    int sts_len = snprintf(string_to_sign, sizeof(string_to_sign), "AWS4-HMAC-SHA256\n%s\n%s\n%s", amzdate, scope, creq_hash);

    unsigned char signing_key[32];
    derive_signing_key(cfg->secret_key, datestamp, cfg->region, signing_key);

    unsigned char sig[32];
    hmac_sha256(signing_key, 32, (unsigned char*)string_to_sign, (size_t)sts_len, sig);

    char sighex[65];
    for (int i = 0; i < 32; i++) sprintf(sighex + i * 2, "%02x", sig[i]);
    sighex[64] = 0;

    snprintf(authorization, authlen, "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
             cfg->access_key, scope, signed_headers, sighex);
}

static size_t write_response(void *ptr, size_t size, size_t nmemb, void *userdata) {
    response_buf_t *buf = (response_buf_t*)userdata;
    size_t n = size * nmemb;
    if (buf->len + n + 1 > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap * 2 : 4096;
        while (new_cap < buf->len + n + 1) new_cap *= 2;
        char *p = (char*)realloc(buf->data, new_cap);
        if (!p) return 0;
        buf->data = p;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, ptr, n);
    buf->len += n;
    buf->data[buf->len] = 0;
    return n;
}

static size_t empty_read(char *b, size_t s, size_t n, void *u) {
    (void)b; (void)s; (void)n; (void)u;
    return 0;
}

static int s3_request(const s3_config_t *cfg, const char *method, const char *bucket,
                      const char *key, const char *query, const char *canonical_query,
                      const char *body, size_t body_len,
                      const char *extra_header_name, const char *extra_header_value,
                      long *http_out, response_buf_t *response, char *err, size_t errlen) {
    char host[256], amzdate[64], auth[2048];
    sign_request(cfg, method, bucket, key, canonical_query, body ? (long long)body_len : -1, host, sizeof(host), amzdate, sizeof(amzdate), auth, sizeof(auth));

    char url[4096];
    if (key == NULL || key[0] == '\0') snprintf(url, sizeof(url), "%s/%s", cfg->endpoint, bucket);
    else snprintf(url, sizeof(url), "%s/%s/%s", cfg->endpoint, bucket, key);
    if (query && query[0]) {
        size_t used = strlen(url);
        snprintf(url + used, sizeof(url) - used, "?%s", query);
    }

    struct curl_slist *headers = NULL;
    char hbuf[2300];
    snprintf(hbuf, sizeof(hbuf), "Host: %s", host);
    headers = curl_slist_append(headers, hbuf);
    snprintf(hbuf, sizeof(hbuf), "x-amz-date: %s", amzdate);
    headers = curl_slist_append(headers, hbuf);
    snprintf(hbuf, sizeof(hbuf), "Authorization: %s", auth);
    headers = curl_slist_append(headers, hbuf);
    headers = curl_slist_append(headers, "Accept:");
    headers = curl_slist_append(headers, "Content-Type:");
    if (extra_header_name && extra_header_value) {
        snprintf(hbuf, sizeof(hbuf), "%s: %s", extra_header_name, extra_header_value);
        headers = curl_slist_append(headers, hbuf);
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_slist_free_all(headers);
        snprintf(err, errlen, "curl_easy_init failed");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);

    if (!strcmp(method, "GET")) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (!strcmp(method, "POST")) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
    } else if (!strcmp(method, "PUT")) {
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)0);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, empty_read);
    } else if (!strcmp(method, "DELETE")) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    if (http_out) *http_out = http;

    int ret = 0;
    if (rc != CURLE_OK || http < 200 || http >= 300) {
        snprintf(err, errlen, "%s failed: curl=%s http=%ld body=%s", method, curl_easy_strerror(rc), http,
                 response && response->data ? response->data : "");
        ret = -1;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ret;
}

static int find_tag_text(const char *xml, const char *tag, char *out, size_t outlen) {
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *p = strstr(xml, open);
    if (!p) return -1;
    p += strlen(open);
    const char *q = strstr(p, close);
    if (!q) return -1;
    size_t n = (size_t)(q - p);
    if (n >= outlen) n = outlen - 1;
    memcpy(out, p, n);
    out[n] = 0;
    return 0;
}

static int parse_upload_id(const char *xml, char *upload_id, size_t upload_id_len) {
    return find_tag_text(xml, "UploadId", upload_id, upload_id_len);
}

static int parse_parts(const char *xml, completed_part_t **parts_out, int *count_out) {
    int cap = 8, count = 0;
    completed_part_t *parts = (completed_part_t*)calloc((size_t)cap, sizeof(completed_part_t));
    if (!parts) return -1;

    const char *p = xml;
    while ((p = strstr(p, "<Part>")) != NULL) {
        const char *end = strstr(p, "</Part>");
        if (!end) break;
        size_t block_len = (size_t)(end - p + strlen("</Part>"));
        char *block = (char*)malloc(block_len + 1);
        if (!block) {
            free(parts);
            return -1;
        }
        memcpy(block, p, block_len);
        block[block_len] = 0;

        char num[32] = {0};
        char etag[256] = {0};
        char sizebuf[64] = {0};
        if (find_tag_text(block, "PartNumber", num, sizeof(num)) == 0 &&
            find_tag_text(block, "ETag", etag, sizeof(etag)) == 0) {
            if (count == cap) {
                cap *= 2;
                completed_part_t *np = (completed_part_t*)realloc(parts, (size_t)cap * sizeof(completed_part_t));
                if (!np) {
                    free(block);
                    free(parts);
                    return -1;
                }
                parts = np;
            }
            parts[count].part_number = atoi(num);
            strncpy(parts[count].etag, etag, sizeof(parts[count].etag) - 1);
            if (find_tag_text(block, "Size", sizebuf, sizeof(sizebuf)) == 0) parts[count].size = atoll(sizebuf);
            count++;
        }
        free(block);
        p = end + strlen("</Part>");
    }

    *parts_out = parts;
    *count_out = count;
    return 0;
}

static int s3_create_multipart(const s3_config_t *cfg, const char *bucket, const char *object, char *upload_id, size_t upload_id_len, char *err, size_t errlen) {
    response_buf_t resp = {0};
    long http = 0;
    int rc = s3_request(cfg, "POST", bucket, object, "uploads=", "uploads=", NULL, 0, NULL, NULL, &http, &resp, err, errlen);
    if (rc == 0 && parse_upload_id(resp.data ? resp.data : "", upload_id, upload_id_len) != 0) {
        snprintf(err, errlen, "CreateMultipartUpload response missing UploadId: %s", resp.data ? resp.data : "");
        rc = -1;
    }
    free(resp.data);
    return rc;
}

static int s3_upload_part_empty(const s3_config_t *cfg, const char *bucket, const char *object,
                                const char *upload_id, int part_number, const char *request_id,
                                char *etag, size_t etag_len, char *err, size_t errlen) {
    char query[512];
    snprintf(query, sizeof(query), "partNumber=%d&uploadId=%s", part_number, upload_id);

    response_buf_t resp = {0};
    long http = 0;
    int rc = s3_request(cfg, "PUT", bucket, object, query, query, NULL, 0,
                        "X-RDMA-Request-ID", request_id, &http, &resp, err, errlen);
    if (rc == 0) snprintf(etag, etag_len, "\"part-%d\"", part_number);
    free(resp.data);
    return rc;
}

static int s3_list_parts(const s3_config_t *cfg, const char *bucket, const char *object,
                         const char *upload_id, completed_part_t **parts, int *count,
                         char *err, size_t errlen) {
    char query[512];
    snprintf(query, sizeof(query), "uploadId=%s", upload_id);

    response_buf_t resp = {0};
    long http = 0;
    int rc = s3_request(cfg, "GET", bucket, object, query, query, NULL, 0, NULL, NULL, &http, &resp, err, errlen);
    if (rc == 0 && parse_parts(resp.data ? resp.data : "", parts, count) != 0) {
        snprintf(err, errlen, "failed to parse ListParts response");
        rc = -1;
    }
    free(resp.data);
    return rc;
}

static int s3_complete_multipart(const s3_config_t *cfg, const char *bucket, const char *object,
                                 const char *upload_id, completed_part_t *parts, int count,
                                 char *err, size_t errlen) {
    size_t cap = 256 + (size_t)count * 384;
    char *body = (char*)calloc(1, cap);
    if (!body) {
        snprintf(err, errlen, "calloc complete body failed");
        return -1;
    }

    size_t pos = 0;
    pos += snprintf(body + pos, cap - pos, "<CompleteMultipartUpload>");
    for (int i = 0; i < count; i++) {
        pos += snprintf(body + pos, cap - pos, "<Part><PartNumber>%d</PartNumber><ETag>%s</ETag></Part>",
                        parts[i].part_number, parts[i].etag[0] ? parts[i].etag : "\"\"");
    }
    snprintf(body + pos, cap - pos, "</CompleteMultipartUpload>");

    char query[512];
    snprintf(query, sizeof(query), "uploadId=%s", upload_id);

    response_buf_t resp = {0};
    long http = 0;
    int rc = s3_request(cfg, "POST", bucket, object, query, query, body, strlen(body), NULL, NULL, &http, &resp, err, errlen);
    if (rc == 0) fprintf(stderr, "%s\n", resp.data ? resp.data : "");
    free(resp.data);
    free(body);
    return rc;
}

static int s3_abort_multipart(const s3_config_t *cfg, const char *bucket, const char *object,
                              const char *upload_id, char *err, size_t errlen) {
    char query[512];
    snprintf(query, sizeof(query), "uploadId=%s", upload_id);
    response_buf_t resp = {0};
    long http = 0;
    int rc = s3_request(cfg, "DELETE", bucket, object, query, query, NULL, 0, NULL, NULL, &http, &resp, err, errlen);
    free(resp.data);
    return rc;
}

static int s3_list_uploads(const s3_config_t *cfg, const options_t *opts, char *err, size_t errlen) {
    char query[1024];
    int pos = snprintf(query, sizeof(query), "uploads=");
    if (opts->delimiter) pos += snprintf(query + pos, sizeof(query) - (size_t)pos, "&delimiter=%s", opts->delimiter);
    if (opts->key_marker) pos += snprintf(query + pos, sizeof(query) - (size_t)pos, "&key-marker=%s", opts->key_marker);
    if (opts->max_uploads > 0) pos += snprintf(query + pos, sizeof(query) - (size_t)pos, "&max-uploads=%lld", opts->max_uploads);
    if (opts->prefix) pos += snprintf(query + pos, sizeof(query) - (size_t)pos, "&prefix=%s", opts->prefix);
    if (opts->upload_id_marker) snprintf(query + pos, sizeof(query) - (size_t)pos, "&upload-id-marker=%s", opts->upload_id_marker);

    response_buf_t resp = {0};
    long http = 0;
    int rc = s3_request(cfg, "GET", opts->bucket, NULL, query, query, NULL, 0, NULL, NULL, &http, &resp, err, errlen);
    if (rc == 0) printf("%s\n", resp.data ? resp.data : "");
    free(resp.data);
    return rc;
}

static int mmap_part(FILE *f, long long offset, long long len, void **buf_out, size_t *map_len_out, char *err, size_t errlen) {
    if (len <= 0 || (long long)(size_t)len != len) {
        snprintf(err, errlen, "invalid part size %lld", len);
        return -1;
    }
    size_t map_len = (size_t)len;
    void *buf = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) {
        snprintf(err, errlen, "mmap failed: %s", strerror(errno));
        return -1;
    }
    if (fseeko(f, (off_t)offset, SEEK_SET) != 0) {
        snprintf(err, errlen, "seek failed: %s", strerror(errno));
        munmap(buf, map_len);
        return -1;
    }
    if (fread(buf, 1, map_len, f) != map_len) {
        snprintf(err, errlen, "read part failed: %s", ferror(f) ? strerror(errno) : "short read");
        munmap(buf, map_len);
        return -1;
    }
    *buf_out = buf;
    *map_len_out = map_len;
    return 0;
}

static int upload_one_part_rdma(const s3_config_t *cfg, const options_t *opts, FILE *f,
                                const char *upload_id, int part_number, long long offset,
                                long long len, completed_part_t *out, char *err, size_t errlen) {
    void *buf = NULL;
    size_t map_len = 0;
    if (mmap_part(f, offset, len, &buf, &map_len, err, errlen) != 0) return -1;

    char request_id[128];
    snprintf(request_id, sizeof(request_id), "rdma-multipart-%d-part-%d-%ld", getpid(), part_number, (long)time(NULL));

    char ws_url[512];
    ws_url_from_endpoint(opts->endpoint, ws_url, sizeof(ws_url));

    rdma_session_t *sess = rdma_send_setup(ws_url, opts->rdma_dev, RDMA_PORT, RDMA_GID_IDX,
                                            request_id, buf, map_len, err, errlen);
    if (!sess) {
        munmap(buf, map_len);
        return -1;
    }

    char etag[256] = {0};
    int rc = s3_upload_part_empty(cfg, opts->bucket, opts->object, upload_id, part_number, request_id, etag, sizeof(etag), err, errlen);

    rdma_session_destroy(sess);
    munmap(buf, map_len);

    if (rc != 0) return -1;
    out->part_number = part_number;
    out->size = len;
    strncpy(out->etag, etag, sizeof(out->etag) - 1);
    return 0;
}

static int upload_parts_rdma(const s3_config_t *cfg, const options_t *opts, const char *upload_id,
                             completed_part_t **parts_out, int *count_out, char *err, size_t errlen) {
    FILE *f = fopen(opts->file_path, "rb");
    if (!f) {
        snprintf(err, errlen, "open %s failed: %s", opts->file_path, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(fileno(f), &st) != 0) {
        snprintf(err, errlen, "stat %s failed: %s", opts->file_path, strerror(errno));
        fclose(f);
        return -1;
    }
    long long file_size = (long long)st.st_size;
    if (file_size <= 0) {
        snprintf(err, errlen, "multipart RDMA upload does not support empty files");
        fclose(f);
        return -1;
    }
    int num_parts = (int)((file_size + opts->part_size - 1) / opts->part_size);
    if (opts->part < 0 || opts->part > num_parts) {
        snprintf(err, errlen, "part must be between 1 and %d, or 0 for all parts", num_parts);
        fclose(f);
        return -1;
    }

    int start = opts->part > 0 ? opts->part : 1;
    int end = opts->part > 0 ? opts->part : num_parts;
    int count = end - start + 1;
    completed_part_t *parts = (completed_part_t*)calloc((size_t)count, sizeof(completed_part_t));
    if (!parts) {
        snprintf(err, errlen, "calloc parts failed");
        fclose(f);
        return -1;
    }

    for (int part_number = start; part_number <= end; part_number++) {
        long long offset = (long long)(part_number - 1) * opts->part_size;
        long long len = opts->part_size;
        if (file_size - offset < len) len = file_size - offset;
        fprintf(stderr, "Uploading part %d/%d via RDMA, size=%lld...\n", part_number, num_parts, len);
        if (upload_one_part_rdma(cfg, opts, f, upload_id, part_number, offset, len, &parts[part_number - start], err, errlen) != 0) {
            free(parts);
            fclose(f);
            return -1;
        }
        fprintf(stderr, "Uploaded part %d, ETag=%s\n", part_number, parts[part_number - start].etag);
    }

    fclose(f);
    *parts_out = parts;
    *count_out = count;
    return 0;
}

int main(int argc, char **argv) {
    options_t opts;
    parse_args(argc, argv, &opts);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    s3_config_t cfg = {
        .endpoint = opts.endpoint,
        .region = "us-east-1",
        .access_key = opts.ak,
        .secret_key = opts.sk,
    };

    char err[1024] = {0};

    if (!strcmp(opts.action, "create")) {
        char upload_id[256];
        if (s3_create_multipart(&cfg, opts.bucket, opts.object, upload_id, sizeof(upload_id), err, sizeof(err)) != 0) {
            fprintf(stderr, "CreateMultipartUpload error: %s\n", err);
            return 1;
        }
        printf("Successfully created multipart upload\nUpload ID: %s\n", upload_id);
        return 0;
    }

    if (!strcmp(opts.action, "upload")) {
        if (!opts.file_path || !opts.upload_id) usage(argv[0]);
        completed_part_t *parts = NULL;
        int count = 0;
        if (upload_parts_rdma(&cfg, &opts, opts.upload_id, &parts, &count, err, sizeof(err)) != 0) {
            fprintf(stderr, "UploadPart error: %s\n", err);
            return 1;
        }
        for (int i = 0; i < count; i++) printf("Part %d ETag: %s\n", parts[i].part_number, parts[i].etag);
        free(parts);
        return 0;
    }

    if (!strcmp(opts.action, "list")) {
        if (!opts.upload_id) usage(argv[0]);
        completed_part_t *parts = NULL;
        int count = 0;
        if (s3_list_parts(&cfg, opts.bucket, opts.object, opts.upload_id, &parts, &count, err, sizeof(err)) != 0) {
            fprintf(stderr, "ListParts error: %s\n", err);
            return 1;
        }
        printf("Parts uploaded for upload ID %s:\n", opts.upload_id);
        for (int i = 0; i < count; i++) printf("Part %d: ETag=%s, Size=%lld bytes\n", parts[i].part_number, parts[i].etag, parts[i].size);
        free(parts);
        return 0;
    }

    if (!strcmp(opts.action, "list-uploads")) {
        if (s3_list_uploads(&cfg, &opts, err, sizeof(err)) != 0) {
            fprintf(stderr, "ListMultipartUploads error: %s\n", err);
            return 1;
        }
        return 0;
    }

    if (!strcmp(opts.action, "complete")) {
        if (!opts.upload_id) usage(argv[0]);
        completed_part_t *parts = NULL;
        int count = 0;
        if (s3_list_parts(&cfg, opts.bucket, opts.object, opts.upload_id, &parts, &count, err, sizeof(err)) != 0) {
            fprintf(stderr, "ListParts error: %s\n", err);
            return 1;
        }
        if (count == 0) {
            fprintf(stderr, "No parts found for upload ID %s\n", opts.upload_id);
            free(parts);
            return 1;
        }
        if (s3_complete_multipart(&cfg, opts.bucket, opts.object, opts.upload_id, parts, count, err, sizeof(err)) != 0) {
            fprintf(stderr, "CompleteMultipartUpload error: %s\n", err);
            free(parts);
            return 1;
        }
        printf("Successfully completed multipart upload\n");
        free(parts);
        return 0;
    }

    if (!strcmp(opts.action, "abort")) {
        if (!opts.upload_id) usage(argv[0]);
        if (s3_abort_multipart(&cfg, opts.bucket, opts.object, opts.upload_id, err, sizeof(err)) != 0) {
            fprintf(stderr, "AbortMultipartUpload error: %s\n", err);
            return 1;
        }
        printf("Successfully aborted multipart upload\n");
        return 0;
    }

    if (!strcmp(opts.action, "all")) {
        if (!opts.file_path) usage(argv[0]);
        char upload_id[256];
        if (s3_create_multipart(&cfg, opts.bucket, opts.object, upload_id, sizeof(upload_id), err, sizeof(err)) != 0) {
            fprintf(stderr, "CreateMultipartUpload error: %s\n", err);
            return 1;
        }
        printf("Successfully created multipart upload\nUpload ID: %s\n", upload_id);

        completed_part_t *parts = NULL;
        int count = 0;
        if (upload_parts_rdma(&cfg, &opts, upload_id, &parts, &count, err, sizeof(err)) != 0) {
            fprintf(stderr, "Upload failed, aborting multipart upload: %s\n", err);
            char abort_err[1024] = {0};
            if (s3_abort_multipart(&cfg, opts.bucket, opts.object, upload_id, abort_err, sizeof(abort_err)) != 0) {
                fprintf(stderr, "AbortMultipartUpload error: %s\n", abort_err);
            }
            return 1;
        }

        if (s3_complete_multipart(&cfg, opts.bucket, opts.object, upload_id, parts, count, err, sizeof(err)) != 0) {
            fprintf(stderr, "CompleteMultipartUpload error: %s\n", err);
            free(parts);
            return 1;
        }
        printf("Successfully uploaded %s to s3://%s/%s via RDMA multipart\n", opts.file_path, opts.bucket, opts.object);
        free(parts);
        return 0;
    }

    usage(argv[0]);
    return 1;
}
