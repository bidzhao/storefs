// rdmalib.c - RDMA (libibverbs) + minimal websocket JSON control channel
#include "rdmalib.h"

#include <infiniband/verbs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <zlib.h>

/* ==================== verbs helpers ==================== */

static struct ibv_context *v_open_device(const char *dev_name) {
    int n = 0;
    struct ibv_device **list = ibv_get_device_list(&n);
    if (!list || n == 0) return NULL;
    struct ibv_context *ctx = NULL;
    for (int i = 0; i < n; i++) {
        const char *name = ibv_get_device_name(list[i]);
        if (!dev_name || dev_name[0] == 0 || strcmp(name, dev_name) == 0) {
            ctx = ibv_open_device(list[i]);
            break;
        }
    }
    ibv_free_device_list(list);
    return ctx;
}

static int v_qp_to_init(struct ibv_qp *qp, uint8_t port) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = port;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
}

static int v_query_mtu_enum(struct ibv_context *ctx, uint8_t port) {
    struct ibv_port_attr attr;
    memset(&attr, 0, sizeof(attr));
    if (ibv_query_port(ctx, port, &attr)) return -1;
    switch ((int)attr.active_mtu) {
        case IBV_MTU_256:  return IBV_MTU_256;
        case IBV_MTU_512:  return IBV_MTU_512;
        case IBV_MTU_1024: return IBV_MTU_1024;
        case IBV_MTU_2048: return IBV_MTU_2048;
        case IBV_MTU_4096: return IBV_MTU_4096;
        default:           return IBV_MTU_1024;
    }
}

static int v_query_gid(struct ibv_context *ctx, uint8_t port, int gid_index, uint8_t *out) {
    union ibv_gid gid;
    if (ibv_query_gid(ctx, port, gid_index, &gid)) return -1;
    memcpy(out, gid.raw, 16);
    return 0;
}

static int v_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid,
                        const uint8_t *dgid, uint8_t port, uint8_t gid_index, int mtu_enum) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = (enum ibv_mtu)mtu_enum;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.is_global = 1;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.port_num = port;
    attr.ah_attr.grh.sgid_index = gid_index;
    attr.ah_attr.grh.hop_limit = 64;
    attr.ah_attr.grh.traffic_class = 0;
    attr.ah_attr.grh.flow_label = 0;
    memcpy(&attr.ah_attr.grh.dgid.raw, dgid, 16);
    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
        IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
}

static int v_qp_to_rts(struct ibv_qp *qp) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
        IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
}

/* ==================== tiny JSON helpers (fixed protocol, flat objects) ==================== */

static const char *json_find_key(const char *json, const char *key) {
    char pat[160];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ') p++;
    return p;
}

static int json_get_u64(const char *json, const char *key, uint64_t *out) {
    const char *p = json_find_key(json, key);
    if (!p) return -1;
    *out = strtoull(p, NULL, 10);
    return 0;
}

static int json_get_bool(const char *json, const char *key, int *out) {
    const char *p = json_find_key(json, key);
    if (!p) return -1;
    *out = (strncmp(p, "true", 4) == 0);
    return 0;
}

static int json_get_string(const char *json, const char *key, char *buf, size_t buflen) {
    const char *p = json_find_key(json, key);
    if (!p || *p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < buflen) buf[i++] = *p++;
    buf[i] = 0;
    return 0;
}

static int json_get_bytes16(const char *json, const char *key, uint8_t out[16]) {
    const char *p = json_find_key(json, key);
    if (!p || *p != '[') return -1;
    p++;
    for (int i = 0; i < 16; i++) {
        while (*p == ' ' || *p == ',') p++;
        out[i] = (uint8_t)strtoul(p, (char **)&p, 10);
    }
    return 0;
}

/* ==================== minimal websocket client ==================== */

static void base64_encode(const unsigned char *in, int inlen, char *out) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, j;
    for (i = 0, j = 0; i < inlen; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < inlen) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < inlen) v |= in[i + 2];
        out[j++] = tbl[(v >> 18) & 0x3F];
        out[j++] = tbl[(v >> 12) & 0x3F];
        out[j++] = (i + 1 < inlen) ? tbl[(v >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < inlen) ? tbl[v & 0x3F] : '=';
    }
    out[j] = 0;
}

static ssize_t read_full(int fd, void *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

/* parse "ws://host[:port]/path" */
static int ws_parse_url(const char *url, char *host, size_t hostlen, int *port, char *path, size_t pathlen) {
    const char *p = strstr(url, "://");
    if (!p) return -1;
    p += 3;
    const char *slash = strchr(p, '/');
    char hostport[300];
    size_t n = slash ? (size_t)(slash - p) : strlen(p);
    if (n >= sizeof(hostport)) return -1;
    memcpy(hostport, p, n);
    hostport[n] = 0;
    snprintf(path, pathlen, "%s", slash ? slash : "/");

    char *colon = strchr(hostport, ':');
    if (colon) {
        *colon = 0;
        *port = atoi(colon + 1);
    } else {
        *port = 80;
    }
    snprintf(host, hostlen, "%s", hostport);
    return 0;
}

static int ws_dial(const char *url, char *err, size_t errlen) {
    char host[256], path[256];
    int port = 80;
    if (ws_parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        if (err) snprintf(err, errlen, "invalid websocket url: %s", url);
        return -1;
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) {
        if (err) snprintf(err, errlen, "getaddrinfo(%s) failed", host);
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        if (err) snprintf(err, errlen, "connect to %s:%d failed", host, port);
        return -1;
    }

    unsigned char keybytes[16];
    for (int i = 0; i < 16; i++) keybytes[i] = (unsigned char)rand();
    char key_b64[32];
    base64_encode(keybytes, 16, key_b64);

    char req[512];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n", path, host, port, key_b64);
    if (write(fd, req, n) != n) {
        if (err) snprintf(err, errlen, "failed to send websocket handshake");
        close(fd);
        return -1;
    }

    char resp[2048];
    int total = 0;
    while (total < (int)sizeof(resp) - 1) {
        char c;
        if (read(fd, &c, 1) != 1) {
            if (err) snprintf(err, errlen, "websocket handshake read failed");
            close(fd);
            return -1;
        }
        resp[total++] = c;
        if (total >= 4 && memcmp(resp + total - 4, "\r\n\r\n", 4) == 0) break;
    }
    resp[total] = 0;
    if (strstr(resp, " 101 ") == NULL && strstr(resp, "101 Switching") == NULL) {
        if (err) snprintf(err, errlen, "websocket handshake rejected: %.*s", total, resp);
        close(fd);
        return -1;
    }
    return fd;
}

static int ws_send_text(int fd, const char *msg) {
    size_t len = strlen(msg);
    if (len >= 65536) return -1;
    unsigned char hdr[8];
    int hlen = 0;
    hdr[hlen++] = 0x81; /* FIN + text */
    if (len < 126) {
        hdr[hlen++] = (unsigned char)(0x80 | len);
    } else {
        hdr[hlen++] = 0x80 | 126;
        hdr[hlen++] = (unsigned char)((len >> 8) & 0xFF);
        hdr[hlen++] = (unsigned char)(len & 0xFF);
    }
    unsigned char mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (unsigned char)rand();
    memcpy(hdr + hlen, mask, 4);
    hlen += 4;
    if (write(fd, hdr, hlen) != hlen) return -1;

    unsigned char *masked = malloc(len);
    if (!masked) return -1;
    for (size_t i = 0; i < len; i++) masked[i] = (unsigned char)msg[i] ^ mask[i % 4];
    ssize_t w = write(fd, masked, len);
    free(masked);
    return (w == (ssize_t)len) ? 0 : -1;
}

static int ws_recv_text(int fd, char *buf, size_t bufsize) {
    unsigned char hdr[2];
    if (read_full(fd, hdr, 2) != 2) return -1;
    int opcode = hdr[0] & 0x0F;
    int masked = hdr[1] & 0x80;
    uint64_t len = hdr[1] & 0x7F;
    if (len == 126) {
        unsigned char ext[2];
        if (read_full(fd, ext, 2) != 2) return -1;
        len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (len == 127) {
        unsigned char ext[8];
        if (read_full(fd, ext, 8) != 8) return -1;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
    }
    unsigned char maskkey[4] = {0};
    if (masked) {
        if (read_full(fd, maskkey, 4) != 4) return -1;
    }
    if (len >= bufsize) return -1;
    if (len > 0 && read_full(fd, buf, len) != (ssize_t)len) return -1;
    if (masked) {
        for (uint64_t i = 0; i < len; i++) buf[i] ^= maskkey[i % 4];
    }
    buf[len] = 0;
    if (opcode == 0x8) return -1; /* close frame */
    return (int)len;
}

static void ws_close(int fd) {
    unsigned char hdr[6] = {0x88, 0x80}; /* close, masked, empty payload */
    for (int i = 0; i < 4; i++) hdr[2 + i] = (unsigned char)rand();
    write(fd, hdr, 6);
    close(fd);
}

/* ==================== protocol messages ==================== */

static int send_register_request(int fd, const char *request_id) {
    char msg[256];
    snprintf(msg, sizeof(msg),
        "{\"type\":\"register_request\",\"data\":{\"request_id\":\"%s\"}}", request_id);
    return ws_send_text(fd, msg);
}

static int recv_register_response(int fd, char *err, size_t errlen) {
    char buf[1024];
    int n = ws_recv_text(fd, buf, sizeof(buf));
    if (n < 0) {
        if (err) snprintf(err, errlen, "failed to receive register response");
        return -1;
    }
    if (!strstr(buf, "\"register_response\"")) {
        if (err) snprintf(err, errlen, "unexpected message: %s", buf);
        return -1;
    }
    int success = 0;
    json_get_bool(buf, "success", &success);
    if (!success) {
        char emsg[256] = "";
        json_get_string(buf, "error", emsg, sizeof(emsg));
        if (err) snprintf(err, errlen, "registration failed: %s", emsg);
        return -1;
    }
    return 0;
}

static int send_qpinfo_client(int fd, uint32_t qpn, uint16_t lid,
                               const uint8_t gid[16], uint8_t gid_idx, int32_t mtu_enum) {
    char msg[512];
    int off = snprintf(msg, sizeof(msg),
        "{\"type\":\"qpinfo_client\",\"data\":{\"qpn\":%u,\"lid\":%u,\"gid\":[",
        qpn, lid);
    for (int i = 0; i < 16; i++) {
        off += snprintf(msg + off, sizeof(msg) - off, "%s%u", i ? "," : "", gid[i]);
    }
    snprintf(msg + off, sizeof(msg) - off,
        "],\"gid_idx\":%u,\"mtu_enum\":%d}}", gid_idx, mtu_enum);
    return ws_send_text(fd, msg);
}

typedef struct {
    uint32_t qpn;
    uint16_t lid;
    uint8_t gid[16];
    uint8_t gid_idx;
    int32_t mtu_enum;
} qpinfo_t;

static int recv_qpinfo_server(int fd, qpinfo_t *out, char *err, size_t errlen) {
    char buf[1024];
    int n = ws_recv_text(fd, buf, sizeof(buf));
    if (n < 0) {
        if (err) snprintf(err, errlen, "failed to receive server qp info");
        return -1;
    }
    if (!strstr(buf, "\"qpinfo_server\"")) {
        if (err) snprintf(err, errlen, "unexpected message: %s", buf);
        return -1;
    }
    uint64_t tmp;
    if (json_get_u64(buf, "qpn", &tmp) != 0) return -1;
    out->qpn = (uint32_t)tmp;
    if (json_get_u64(buf, "lid", &tmp) != 0) return -1;
    out->lid = (uint16_t)tmp;
    if (json_get_bytes16(buf, "gid", out->gid) != 0) return -1;
    if (json_get_u64(buf, "gid_idx", &tmp) != 0) return -1;
    out->gid_idx = (uint8_t)tmp;
    if (json_get_u64(buf, "mtu_enum", &tmp) != 0) return -1;
    out->mtu_enum = (int32_t)tmp;
    return 0;
}

static int send_token(int fd, uint64_t addr, uint32_t rkey, uint64_t start, uint64_t length) {
    char msg[256];
    snprintf(msg, sizeof(msg),
        "{\"type\":\"token\",\"data\":{\"addr\":%llu,\"rkey\":%u,\"start\":%llu,\"length\":%llu}}",
        (unsigned long long)addr, rkey, (unsigned long long)start, (unsigned long long)length);
    return ws_send_text(fd, msg);
}

/* ==================== session ==================== */

struct rdma_session {
    struct ibv_context *ctx;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    struct ibv_mr *mr;
};

static void cleanup(rdma_session_t *s) {
    if (!s) return;
    if (s->mr) ibv_dereg_mr(s->mr);
    if (s->qp) ibv_destroy_qp(s->qp);
    if (s->cq) ibv_destroy_cq(s->cq);
    if (s->pd) ibv_dealloc_pd(s->pd);
    if (s->ctx) ibv_close_device(s->ctx);
    free(s);
}

/* Shared handshake: connect ws, register request_id, bring QP up to RTS.
 * On success returns the open websocket fd and fills *out with ctx/pd/cq/qp.
 * On failure returns -1; *out is left with whatever was partially allocated
 * (caller must cleanup()). */
static int rdma_handshake(const char *ws_url, const char *dev_name,
                           uint8_t port, uint8_t gid_index,
                           const char *request_id,
                           rdma_session_t *s, char *err, size_t errlen) {
    int fd = ws_dial(ws_url, err, errlen);
    if (fd < 0) return -1;

    if (send_register_request(fd, request_id) != 0 ||
        recv_register_response(fd, err, errlen) != 0) {
        ws_close(fd);
        return -1;
    }

    s->ctx = v_open_device(dev_name);
    if (!s->ctx) {
        if (err) snprintf(err, errlen, "open_device(%s) failed", dev_name ? dev_name : "");
        ws_close(fd);
        return -1;
    }
    s->pd = ibv_alloc_pd(s->ctx);
    if (!s->pd) { if (err) snprintf(err, errlen, "ibv_alloc_pd failed"); ws_close(fd); return -1; }
    s->cq = ibv_create_cq(s->ctx, 2, NULL, NULL, 0);
    if (!s->cq) { if (err) snprintf(err, errlen, "ibv_create_cq failed"); ws_close(fd); return -1; }

    {
        struct ibv_qp_init_attr qattr;
        memset(&qattr, 0, sizeof(qattr));
        qattr.send_cq = s->cq;
        qattr.recv_cq = s->cq;
        qattr.cap.max_send_wr = 1;
        qattr.cap.max_recv_wr = 1;
        qattr.cap.max_send_sge = 1;
        qattr.cap.max_recv_sge = 1;
        qattr.cap.max_inline_data = 0;
        qattr.qp_type = IBV_QPT_RC;
        qattr.sq_sig_all = 0;
        s->qp = ibv_create_qp(s->pd, &qattr);
    }
    if (!s->qp) { if (err) snprintf(err, errlen, "ibv_create_qp failed"); ws_close(fd); return -1; }

    if (v_qp_to_init(s->qp, port) != 0) {
        if (err) snprintf(err, errlen, "qp_to_init failed");
        ws_close(fd);
        return -1;
    }

    uint8_t gid[16];
    if (v_query_gid(s->ctx, port, gid_index, gid) != 0) {
        if (err) snprintf(err, errlen, "query_gid failed");
        ws_close(fd);
        return -1;
    }
    int mtu_enum = v_query_mtu_enum(s->ctx, port);
    if (mtu_enum < 0) {
        if (err) snprintf(err, errlen, "query_port mtu failed");
        ws_close(fd);
        return -1;
    }

    uint32_t qpn = s->qp->qp_num;
    if (send_qpinfo_client(fd, qpn, 0, gid, gid_index, (int32_t)mtu_enum) != 0) {
        if (err) snprintf(err, errlen, "failed to send qp info");
        ws_close(fd);
        return -1;
    }

    qpinfo_t srv;
    if (recv_qpinfo_server(fd, &srv, err, errlen) != 0) {
        ws_close(fd);
        return -1;
    }

    if (v_qp_to_rtr(s->qp, srv.qpn, srv.lid, srv.gid, port, srv.gid_idx, (int)srv.mtu_enum) != 0) {
        if (err) snprintf(err, errlen, "qp_to_rtr failed");
        ws_close(fd);
        return -1;
    }
    if (v_qp_to_rts(s->qp) != 0) {
        if (err) snprintf(err, errlen, "qp_to_rts failed");
        ws_close(fd);
        return -1;
    }

    return fd;
}

rdma_session_t *rdma_recv_setup(const char *ws_url,
                                 const char *dev_name,
                                 uint8_t port,
                                 uint8_t gid_index,
                                 const char *request_id,
                                 void *buf, size_t len,
                                 char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    srand((unsigned)(time(NULL) ^ getpid()));

    rdma_session_t *s = calloc(1, sizeof(*s));
    if (!s) { if (err) snprintf(err, errlen, "out of memory"); return NULL; }

    int fd = rdma_handshake(ws_url, dev_name, port, gid_index, request_id, s, err, errlen);
    if (fd < 0) { cleanup(s); return NULL; }

    /* receive side: server RDMA-writes into buf, so it needs remote write access */
    s->mr = ibv_reg_mr(s->pd, buf, len,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!s->mr) {
        if (err) snprintf(err, errlen, "ibv_reg_mr failed");
        ws_close(fd);
        cleanup(s);
        return NULL;
    }

    if (send_token(fd, (uint64_t)(uintptr_t)buf, s->mr->rkey, 0, (uint64_t)len) != 0) {
        if (err) snprintf(err, errlen, "failed to send token");
        ws_close(fd);
        cleanup(s);
        return NULL;
    }

    ws_close(fd);
    return s;
}

rdma_session_t *rdma_send_setup(const char *ws_url,
                                 const char *dev_name,
                                 uint8_t port,
                                 uint8_t gid_index,
                                 const char *request_id,
                                 void *buf, size_t len,
                                 char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    srand((unsigned)(time(NULL) ^ getpid()));

    rdma_session_t *s = calloc(1, sizeof(*s));
    if (!s) { if (err) snprintf(err, errlen, "out of memory"); return NULL; }

    int fd = rdma_handshake(ws_url, dev_name, port, gid_index, request_id, s, err, errlen);
    if (fd < 0) { cleanup(s); return NULL; }

    /* send side: server RDMA-reads from buf, so it only needs remote read access */
    s->mr = ibv_reg_mr(s->pd, buf, len,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
    if (!s->mr) {
        if (err) snprintf(err, errlen, "ibv_reg_mr failed");
        ws_close(fd);
        cleanup(s);
        return NULL;
    }

    if (send_token(fd, (uint64_t)(uintptr_t)buf, s->mr->rkey, 0, (uint64_t)len) != 0) {
        if (err) snprintf(err, errlen, "failed to send token");
        ws_close(fd);
        cleanup(s);
        return NULL;
    }

    ws_close(fd);
    return s;
}

void rdma_session_destroy(rdma_session_t *s) {
    cleanup(s);
}

uint32_t rdma_crc32(const void *data, size_t len) {
    return (uint32_t)crc32(0L, (const unsigned char *)data, (uInt)len);
}
