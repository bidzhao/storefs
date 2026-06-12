/* verbs_shim.c - thin C shim around libibverbs exposing simple,
 * ctypes-friendly functions. Build into librdmaverbs.so (see Makefile).
 *
 * This mirrors the static helper functions used in the C client's
 * rdmalib.c, just exported as plain extern "C" functions with primitive
 * argument/return types so they can be called directly from Python via
 * ctypes.
 */
#include <infiniband/verbs.h>
#include <stdint.h>
#include <string.h>

struct ibv_context *rdma_open_device(const char *dev_name) {
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

void rdma_close_device(struct ibv_context *ctx) {
    if (ctx) ibv_close_device(ctx);
}

struct ibv_pd *rdma_alloc_pd(struct ibv_context *ctx) {
    return ibv_alloc_pd(ctx);
}

void rdma_dealloc_pd(struct ibv_pd *pd) {
    if (pd) ibv_dealloc_pd(pd);
}

struct ibv_cq *rdma_create_cq(struct ibv_context *ctx, int cqe) {
    return ibv_create_cq(ctx, cqe, NULL, NULL, 0);
}

void rdma_destroy_cq(struct ibv_cq *cq) {
    if (cq) ibv_destroy_cq(cq);
}

struct ibv_qp *rdma_create_rc_qp(struct ibv_pd *pd, struct ibv_cq *cq,
                                  uint32_t max_send, uint32_t max_recv, uint32_t max_inline) {
    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.send_cq = cq;
    attr.recv_cq = cq;
    attr.cap.max_send_wr = max_send;
    attr.cap.max_recv_wr = max_recv;
    attr.cap.max_send_sge = 1;
    attr.cap.max_recv_sge = 1;
    attr.cap.max_inline_data = max_inline;
    attr.qp_type = IBV_QPT_RC;
    attr.sq_sig_all = 0;
    return ibv_create_qp(pd, &attr);
}

void rdma_destroy_qp(struct ibv_qp *qp) {
    if (qp) ibv_destroy_qp(qp);
}

int rdma_qp_to_init(struct ibv_qp *qp, uint8_t port) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = port;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
}

int rdma_query_port_mtu_enum(struct ibv_context *ctx, uint8_t port) {
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

int rdma_query_gid(struct ibv_context *ctx, uint8_t port, int gid_index, uint8_t *gid_out /* 16 bytes */) {
    union ibv_gid gid;
    if (ibv_query_gid(ctx, port, gid_index, &gid)) return -1;
    memcpy(gid_out, gid.raw, 16);
    return 0;
}

int rdma_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid,
                   const uint8_t *dgid /* 16 bytes */, uint8_t port, uint8_t gid_index, int mtu_enum) {
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

int rdma_qp_to_rts(struct ibv_qp *qp) {
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

/* receive side: server RDMA-writes into this MR */
struct ibv_mr *rdma_reg_mr_dst(struct ibv_pd *pd, void *addr, size_t len) {
    return ibv_reg_mr(pd, addr, len,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
}

/* send side: server RDMA-reads from this MR */
struct ibv_mr *rdma_reg_mr_src(struct ibv_pd *pd, void *addr, size_t len) {
    return ibv_reg_mr(pd, addr, len, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
}

void rdma_dereg_mr(struct ibv_mr *mr) {
    if (mr) ibv_dereg_mr(mr);
}

uint32_t rdma_qp_num(struct ibv_qp *qp) { return qp->qp_num; }
uint32_t rdma_mr_lkey(struct ibv_mr *mr) { return mr->lkey; }
uint32_t rdma_mr_rkey(struct ibv_mr *mr) { return mr->rkey; }
