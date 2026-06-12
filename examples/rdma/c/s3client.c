#include "s3client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>

/* ================= SHA256 ================= */

static void sha256_hex(const unsigned char *data, size_t len, char out[65]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data, len, hash);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(out + i * 2, "%02x", hash[i]);
    out[64] = 0;
}

static void hmac_sha256(const unsigned char *key, size_t keylen,
                        const unsigned char *data, size_t datalen,
                        unsigned char out[32]) {
    unsigned int outlen = 0;
    HMAC(EVP_sha256(), key, keylen, data, datalen, out, &outlen);
}

/* ================= Signing Key ================= */

static void derive_signing_key(const char *secret,
                               const char *date,
                               const char *region,
                               unsigned char out[32]) {
    unsigned char kDate[32], kRegion[32], kService[32];
    char kSecret[160];

    snprintf(kSecret, sizeof(kSecret), "AWS4%s", secret);

    hmac_sha256((unsigned char*)kSecret, strlen(kSecret),
                (unsigned char*)date, strlen(date), kDate);

    hmac_sha256(kDate, 32,
                (unsigned char*)region, strlen(region), kRegion);

    hmac_sha256(kRegion, 32,
                (unsigned char*)"s3", 2, kService);

    hmac_sha256(kService, 32,
                (unsigned char*)"aws4_request", 12, out);
}

/* ================= Host ================= */

static void host_from_endpoint(const char *endpoint,
                               char *host,
                               size_t hostlen) {
    const char *p = strstr(endpoint, "://");
    p = p ? p + 3 : endpoint;

    const char *slash = strchr(p, '/');
    size_t n = slash ? (size_t)(slash - p) : strlen(p);

    if (n >= hostlen) n = hostlen - 1;

    memcpy(host, p, n);
    host[n] = 0;
}

/* ================= Canonical Headers ================= */

typedef struct {
    const char *k;
    const char *v;
} header_kv;

static int cmp_header(const void *a, const void *b) {
    return strcmp(((header_kv*)a)->k, ((header_kv*)b)->k);
}

static void to_lower_inplace(char *s) {
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            *s += 32;
}

static void build_canonical_headers(header_kv *h, int n,
                                     char *out,
                                     char *signed_headers) {
    qsort(h, n, sizeof(header_kv), cmp_header);

    int pos = 0;
    int sh = 0;

    for (int i = 0; i < n; i++) {
        char keybuf[128];
        strncpy(keybuf, h[i].k, sizeof(keybuf));
        keybuf[sizeof(keybuf)-1] = 0;
        to_lower_inplace(keybuf);

        pos += snprintf(out + pos, 4096 - pos,
                        "%s:%s\n", keybuf, h[i].v);

        sh += snprintf(signed_headers + sh, 512 - sh,
                        "%s%s", (i ? ";" : ""), keybuf);
    }
}

/* ================= SigV4 Core ================= */

static void sign_request(const s3_config_t *cfg,
                         const char *method,
                         const char *bucket,
                         const char *key,
                         char *host,
                         size_t hostlen,
                         char *amzdate,
                         size_t amzdate_len,
                         char *payload_hash,
                         size_t hashlen,
                         char *authorization,
                         size_t authlen) {

    host_from_endpoint(cfg->endpoint, host, hostlen);

    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);

    char datestamp[16];
    strftime(amzdate, amzdate_len, "%Y%m%dT%H%M%SZ", &tmv);
    strftime(datestamp, sizeof(datestamp), "%Y%m%d", &tmv);

    strncpy(payload_hash, "UNSIGNED-PAYLOAD", hashlen);
    payload_hash[hashlen-1] = 0;

    char canonical_uri[2048];
    if (key == NULL || key[0] == '\0') {
        snprintf(canonical_uri, sizeof(canonical_uri), "/%s", bucket);
    } else {
        snprintf(canonical_uri, sizeof(canonical_uri), "/%s/%s", bucket, key);
    }

    header_kv headers[2] = {
        {"Host", host},
        {"x-amz-date", amzdate}
    };

    char canonical_headers[4096] = {0};
    char signed_headers[512] = {0};
    build_canonical_headers(headers, 2, canonical_headers, signed_headers);

    char canonical_request[8192];
    int cr_len = snprintf(canonical_request, sizeof(canonical_request),
                         "%s\n%s\n\n%s\n%s\n%s",
                         method, canonical_uri, canonical_headers, signed_headers, payload_hash);

    char creq_hash[65];
    sha256_hex((unsigned char*)canonical_request, cr_len, creq_hash);

    char scope[128];
    snprintf(scope, sizeof(scope), "%s/%s/s3/aws4_request", datestamp, cfg->region);

    char string_to_sign[4096];
    int sts_len = snprintf(string_to_sign, sizeof(string_to_sign),
                          "AWS4-HMAC-SHA256\n%s\n%s\n%s",
                          amzdate, scope, creq_hash);

    unsigned char signing_key[32];
    derive_signing_key(cfg->secret_key, datestamp, cfg->region, signing_key);

    unsigned char sig[32];
    hmac_sha256(signing_key, 32,
                (unsigned char*)string_to_sign, sts_len, sig);

    char sighex[65];
    for (int i = 0; i < 32; i++) sprintf(sighex + i*2, "%02x", sig[i]);
    sighex[64] = 0;

    snprintf(authorization, authlen,
             "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
             cfg->access_key, scope, signed_headers, sighex);
}

/* ================= CURL helpers ================= */

static size_t discard(void *p, size_t s, size_t n, void *u) {
    (void)p; (void)u;
    return s*n;
}

static size_t header_cb(char *b, size_t s, size_t n, void *u) {
    long long *len = (long long*)u;
    size_t sz = s*n;
    if (sz > 15 && strncasecmp(b,"Content-Length:",15)==0)
        *len = atoll(b+15);
    return sz;
}

static size_t empty_read(char *b,size_t s,size_t n,void*u){
    (void)b;(void)s;(void)n;(void)u;
    return 0;
}

/* ================= CURL ================= */

static CURL *make_curl(const s3_config_t *cfg,
                       const char *bucket,
                       const char *key,
                       const char *method,
                       struct curl_slist **out_headers) {

    char host[256], amzdate[64], payload[64], auth[2048];
    sign_request(cfg, method, bucket, key, host, sizeof(host), amzdate, sizeof(amzdate), payload, sizeof(payload), auth, sizeof(auth));

    char url[2048];
    if (key == NULL || key[0] == '\0') {
        snprintf(url, sizeof(url), "%s/%s", cfg->endpoint, bucket);
    } else {
        snprintf(url, sizeof(url), "%s/%s/%s", cfg->endpoint, bucket, key);
    }

    struct curl_slist *h = NULL;
    char buf[512];

    snprintf(buf, sizeof(buf), "Host: %s", host);
    h = curl_slist_append(h, buf);

    snprintf(buf, sizeof(buf), "x-amz-date: %s", amzdate);
    h = curl_slist_append(h, buf);

    snprintf(buf, sizeof(buf), "Authorization: %s", auth);
    h = curl_slist_append(h, buf);

    /* libcurl adds "Accept: STAR/STAR" by default; AWS SDK v4's signer is not
     * told to ignore it, so it would end up in the server's recomputed
     * SignedHeaders unless we suppress it here. */
    h = curl_slist_append(h, "Accept:");

    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, discard);

    *out_headers = h;
    return c;
}

/* ================= APIs ================= */

int s3_head_object(const s3_config_t *cfg, const char *bucket, const char *key, long long *size_out, char *err, size_t errlen) {
    struct curl_slist *h = NULL;
    CURL *c = make_curl(cfg, bucket, key, "HEAD", &h);
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);

    long long len = -1;
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &len);

    CURLcode rc = curl_easy_perform(c);

    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);

    int ret = 0;
    if (rc != CURLE_OK || http != 200) {
        snprintf(err, errlen, "HEAD failed: curl=%s http=%ld", curl_easy_strerror(rc), http);
        ret = -1;
    } else {
        *size_out = len;
    }

    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return ret;
}

int s3_get_object(const s3_config_t *cfg, const char *bucket, const char *key, const char *hn, const char *hv, char *err, size_t errlen) {
    struct curl_slist *h = NULL;
    CURL *c = make_curl(cfg, bucket, key, "GET", &h);
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);

    /* The server reports the real object size in Content-Length but the
     * actual HTTP body is short/empty (the data is delivered via RDMA, not
     * the HTTP body). Without this, curl treats that as CURLE_PARTIAL_FILE
     * even though the request succeeded (HTTP 200). */
    curl_easy_setopt(c, CURLOPT_IGNORE_CONTENT_LENGTH, 1L);

    if (hn && hv) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s: %s", hn, hv);
        h = curl_slist_append(h, buf);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    }

    CURLcode rc = curl_easy_perform(c);

    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);

    int ret = (rc == CURLE_OK && http == 200) ? 0 : -1;
    if (ret) snprintf(err, errlen, "GET failed: curl=%s http=%ld", curl_easy_strerror(rc), http);

    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return ret;
}

int s3_put_object(const s3_config_t *cfg, const char *bucket, const char *key, const char *hn, const char *hv, char *err, size_t errlen) {
    struct curl_slist *h = NULL;
    CURL *c = make_curl(cfg, bucket, key, "PUT", &h);

    if (hn && hv) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s: %s", hn, hv);
        h = curl_slist_append(h, buf);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    }

    curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, (curl_off_t)0);
    curl_easy_setopt(c, CURLOPT_READFUNCTION, empty_read);

    CURLcode rc = curl_easy_perform(c);

    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);

    int ret = (rc == CURLE_OK && (http == 200 || http == 201)) ? 0 : -1;
    if (ret) snprintf(err, errlen, "PUT failed: curl=%s http=%ld", curl_easy_strerror(rc), http);

    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return ret;
}
