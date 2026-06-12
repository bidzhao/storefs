//go:build linux

package main

/*
#cgo CFLAGS:  -I/usr/include/infiniband -I/usr/include
#cgo LDFLAGS: -libverbs

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <infiniband/verbs.h>

static struct ibv_context* open_device(const char* dev_name) {
    int n = 0;
    struct ibv_device** list = ibv_get_device_list(&n);
    if (!list || n == 0) return NULL;
    struct ibv_context* ctx = NULL;
    for (int i = 0; i < n; i++) {
        const char* name = ibv_get_device_name(list[i]);
        if (!dev_name || strcmp(name, dev_name) == 0) {
            ctx = ibv_open_device(list[i]);
            break;
        }
    }
    ibv_free_device_list(list);
    return ctx;
}

static struct ibv_pd* alloc_pd(struct ibv_context* ctx) {
    return ibv_alloc_pd(ctx);
}

static struct ibv_cq* create_cq(struct ibv_context* ctx, int cqe) {
    return ibv_create_cq(ctx, cqe, NULL, NULL, 0);
}

static struct ibv_qp* create_rc_qp(struct ibv_pd* pd, struct ibv_cq* cq,
                                    uint32_t max_send, uint32_t max_recv,
                                    uint32_t max_inline) {
    struct ibv_qp_init_attr attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .cap = {
            .max_send_wr     = max_send,
            .max_recv_wr     = max_recv,
            .max_send_sge    = 1,
            .max_recv_sge    = 1,
            .max_inline_data = max_inline,
        },
        .qp_type    = IBV_QPT_RC,
        .sq_sig_all = 0,
    };
    return ibv_create_qp(pd, &attr);
}

static int qp_to_init(struct ibv_qp* qp, uint8_t port) {
    struct ibv_qp_attr attr = {
        .qp_state        = IBV_QPS_INIT,
        .pkey_index      = 0,
        .port_num        = port,
        .qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE,
    };
    return ibv_modify_qp(qp, &attr,
                         IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
}

static int query_port_mtu_enum(struct ibv_context* ctx, uint8_t port) {
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

static int query_gid(struct ibv_context* ctx, uint8_t port,
                     int gid_index, uint8_t* gid_out) {
    union ibv_gid gid;
    if (ibv_query_gid(ctx, port, gid_index, &gid)) return -1;
    memcpy(gid_out, gid.raw, 16);
    return 0;
}

static int qp_to_rtr(struct ibv_qp* qp,
                     uint32_t remote_qpn, uint16_t dlid,
                     uint8_t* dgid,
                     uint8_t port, uint8_t gid_index,
                     int mtu_enum) {
    struct ibv_qp_attr attr = {
        .qp_state           = IBV_QPS_RTR,
        .path_mtu           = (enum ibv_mtu)mtu_enum,
        .dest_qp_num        = remote_qpn,
        .rq_psn             = 0,
        .max_dest_rd_atomic = 1,
        .min_rnr_timer      = 12,
        .ah_attr = {
            .is_global  = 1,
            .dlid       = dlid,
            .sl         = 0,
            .port_num   = port,
            .grh = {
                .sgid_index    = gid_index,
                .hop_limit     = 64,
                .traffic_class = 0,
                .flow_label    = 0,
            },
        },
    };
    memcpy(&attr.ah_attr.grh.dgid.raw, dgid, 16);
    return ibv_modify_qp(qp, &attr,
                         IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                         IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                         IBV_QP_MIN_RNR_TIMER);
}

static int qp_to_rts(struct ibv_qp* qp) {
    struct ibv_qp_attr attr = {
        .qp_state        = IBV_QPS_RTS,
        .timeout         = 14,
        .retry_cnt      = 7,
        .rnr_retry      = 7,
        .sq_psn         = 0,
        .max_rd_atomic  = 1,
    };
    return ibv_modify_qp(qp, &attr,
                         IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                         IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
}

static struct ibv_mr* reg_mr_src(struct ibv_pd* pd, void* addr, size_t len) {
    return ibv_reg_mr(pd, addr, len, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
}

static struct ibv_mr* reg_mr_dst(struct ibv_pd* pd, void* addr, size_t len) {
    return ibv_reg_mr(pd, addr, len,
                      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
}

static int post_rdma_write(struct ibv_qp* qp,
                          uint64_t local_addr, uint32_t lkey,
                          uint64_t remote_addr, uint32_t rkey,
                          uint32_t length, uint64_t wr_id) {
    struct ibv_sge sge = { .addr = local_addr, .length = length, .lkey = lkey };
    struct ibv_send_wr wr = {
        .wr_id      = wr_id,
        .sg_list    = &sge,
        .num_sge    = 1,
        .opcode     = IBV_WR_RDMA_WRITE,
        .send_flags = IBV_SEND_SIGNALED,
        .wr.rdma    = { .remote_addr = remote_addr, .rkey = rkey },
    };
    struct ibv_send_wr* bad = NULL;
    return ibv_post_send(qp, &wr, &bad);
}

static int poll_cq_one(struct ibv_cq* cq, uint64_t wr_id, int max_polls, int* out_status) {
    struct ibv_wc wc;
    if (max_polls == 0) max_polls = 0x7FFFFFFF;
    for (int i = 0; i < max_polls; i++) {
        int n = ibv_poll_cq(cq, 1, &wc);
        if (n < 0) return -1;
        if (n == 0) continue;
        if (wc.wr_id == wr_id) {
            *out_status = (wc.status == IBV_WC_SUCCESS) ? 0 : (int)wc.status;
            return 0;
        }
    }
    return -1;
}

static uint32_t qp_num(struct ibv_qp* qp) {
    return qp->qp_num;
}

static uint32_t mr_lkey(struct ibv_mr* mr) {
    return mr->lkey;
}

static uint32_t mr_rkey(struct ibv_mr* mr) {
    return mr->rkey;
}
*/
import "C"

import (
	"encoding/json"
	"fmt"
	"hash/crc32"
	"unsafe"

	"github.com/gorilla/websocket"
)

// ==================== WebSocket Protocol ====================

const (
	MsgTypeRegisterRequest  = "register_request"
	MsgTypeRegisterResponse = "register_response"
	MsgTypeQPInfoClient     = "qpinfo_client"
	MsgTypeQPInfoServer     = "qpinfo_server"
	MsgTypeToken            = "token"
	MsgTypeAck              = "ack"
	MsgTypeError            = "error"

	AckOK      uint32 = 0
	AckRDMAErr uint32 = 2
)

// WebSocketMessage wraps all messages exchanged over WebSocket
type WebSocketMessage struct {
	Type string          `json:"type"`
	Data json.RawMessage `json:"data"`
}

// RegisterRequest is sent by client first to register its RequestID
type RegisterRequest struct {
	RequestID string `json:"request_id"`
}

// RegisterResponse acknowledges registration
type RegisterResponse struct {
	Success bool   `json:"success"`
	Error   string `json:"error,omitempty"`
}

// QPInfo carries Queue Pair information for RDMA connection setup
type QPInfo struct {
	QPN     uint32   `json:"qpn"`
	LID     uint16   `json:"lid"`
	GID     [16]byte `json:"gid"`
	GIDIdx  uint8    `json:"gid_idx"`
	MTUEnum int32    `json:"mtu_enum"`
}

// Token carries the RDMA credentials of a memory region
type Token struct {
	Addr   uint64 `json:"addr"`   // base VA of the registered MR
	Rkey   uint32 `json:"rkey"`   // remote key
	Start  uint64 `json:"start"`  // byte offset from Addr where data starts
	Length uint64 `json:"length"` // byte count of the data region
}

// Ack is sent after all RDMA operations complete
type Ack struct {
	Status        uint32 `json:"status"`
	CRC32Computed uint32 `json:"crc32_computed,omitempty"`
	BytesWritten  uint64 `json:"bytes_written,omitempty"`
}

// ErrorMessage carries an error
type ErrorMessage struct {
	Error string `json:"error"`
}

// ==================== RDMA Types ====================

type Context struct {
	c *C.struct_ibv_context
}

type PD struct {
	c *C.struct_ibv_pd
}

type MR struct {
	c    *C.struct_ibv_mr
	Lkey uint32
	Rkey uint32
}

type CQ struct {
	c *C.struct_ibv_cq
}

type QP struct {
	c   *C.struct_ibv_qp
	Num uint32
}

// ==================== RDMA Functions ====================

func OpenDevice(devName string) (*Context, error) {
	var cs *C.char
	if devName != "" {
		cs = C.CString(devName)
		defer C.free(unsafe.Pointer(cs))
	}
	ctx := C.open_device(cs)
	if ctx == nil {
		return nil, fmt.Errorf("open_device(%s) failed", devName)
	}
	return &Context{c: ctx}, nil
}

func (ctx *Context) Close() {
	C.ibv_close_device(ctx.c)
}

func (ctx *Context) AllocPD() (*PD, error) {
	pd := C.alloc_pd(ctx.c)
	if pd == nil {
		return nil, fmt.Errorf("ibv_alloc_pd failed")
	}
	return &PD{c: pd}, nil
}

func (pd *PD) Dealloc() {
	C.ibv_dealloc_pd(pd.c)
}

func (ctx *Context) CreateCQ(cqe int) (*CQ, error) {
	cq := C.create_cq(ctx.c, C.int(cqe))
	if cq == nil {
		return nil, fmt.Errorf("ibv_create_cq failed")
	}
	return &CQ{c: cq}, nil
}

func (cq *CQ) Destroy() {
	C.ibv_destroy_cq(cq.c)
}

func (pd *PD) CreateRC(cq *CQ, maxSend, maxRecv, maxInline uint32) (*QP, error) {
	qp := C.create_rc_qp(pd.c, cq.c, C.uint32_t(maxSend), C.uint32_t(maxRecv), C.uint32_t(maxInline))
	if qp == nil {
		return nil, fmt.Errorf("create_rc_qp failed")
	}
	return &QP{c: qp, Num: uint32(C.qp_num(qp))}, nil
}

func (qp *QP) Destroy() {
	C.ibv_destroy_qp(qp.c)
}

func (qp *QP) ToInit(port uint8) error {
	if ret := C.qp_to_init(qp.c, C.uint8_t(port)); ret != 0 {
		return fmt.Errorf("qp_to_init failed: %d", ret)
	}
	return nil
}

func (ctx *Context) QueryMTUEnum(port uint8) (int, error) {
	ret := C.query_port_mtu_enum(ctx.c, C.uint8_t(port))
	if ret < 0 {
		return 0, fmt.Errorf("ibv_query_port failed")
	}
	return int(ret), nil
}

func (ctx *Context) QueryGID(port uint8, idx int) ([16]byte, error) {
	var raw [16]byte
	p := (*C.uint8_t)(unsafe.Pointer(&raw[0]))
	if ret := C.query_gid(ctx.c, C.uint8_t(port), C.int(idx), p); ret != 0 {
		return raw, fmt.Errorf("query_gid failed")
	}
	return raw, nil
}

func (qp *QP) ToRTR(remoteQPN uint32, dlid uint16, dgid [16]byte,
	port, gidIdx uint8, mtuEnum int) error {
	p := (*C.uint8_t)(unsafe.Pointer(&dgid[0]))
	if ret := C.qp_to_rtr(qp.c,
		C.uint32_t(remoteQPN), C.uint16_t(dlid),
		p, C.uint8_t(port), C.uint8_t(gidIdx),
		C.int(mtuEnum)); ret != 0 {
		return fmt.Errorf("qp_to_rtr failed: %d", ret)
	}
	return nil
}

func (qp *QP) ToRTS() error {
	if ret := C.qp_to_rts(qp.c); ret != 0 {
		return fmt.Errorf("qp_to_rts failed: %d", ret)
	}
	return nil
}

func (pd *PD) RegMRSrc(addr unsafe.Pointer, length uint64) (*MR, error) {
	mr := C.reg_mr_src(pd.c, addr, C.size_t(length))
	if mr == nil {
		return nil, fmt.Errorf("reg_mr_src failed")
	}
	return &MR{c: mr, Lkey: uint32(C.mr_lkey(mr)), Rkey: uint32(C.mr_rkey(mr))}, nil
}

func (pd *PD) RegMRDst(addr unsafe.Pointer, length uint64) (*MR, error) {
	mr := C.reg_mr_dst(pd.c, addr, C.size_t(length))
	if mr == nil {
		return nil, fmt.Errorf("reg_mr_dst failed")
	}
	return &MR{c: mr, Lkey: uint32(C.mr_lkey(mr)), Rkey: uint32(C.mr_rkey(mr))}, nil
}

func (mr *MR) Dereg() {
	C.ibv_dereg_mr(mr.c)
}

func (qp *QP) PostRDMAWrite(
	localAddr uint64, lkey uint32,
	remoteAddr uint64, rkey uint32,
	length uint32, wrID uint64,
) error {
	if ret := C.post_rdma_write(qp.c,
		C.uint64_t(localAddr), C.uint32_t(lkey),
		C.uint64_t(remoteAddr), C.uint32_t(rkey),
		C.uint32_t(length), C.uint64_t(wrID)); ret != 0 {
		return fmt.Errorf("post_rdma_write failed: %d", ret)
	}
	return nil
}

func (cq *CQ) PollCQOne(wrID uint64, maxPolls int) error {
	var status C.int
	if ret := C.poll_cq_one(cq.c, C.uint64_t(wrID), C.int(maxPolls), &status); ret != 0 {
		return fmt.Errorf("poll_cq_one timed out (wr_id=%d)", wrID)
	}
	if status != 0 {
		return fmt.Errorf("WC error status %d (wr_id=%d)", int(status), wrID)
	}
	return nil
}

// ==================== WebSocket Helpers ====================

func sendJSON(conn *websocket.Conn, msgType string, data interface{}) error {
	raw, err := json.Marshal(data)
	if err != nil {
		return err
	}
	msg := WebSocketMessage{
		Type: msgType,
		Data: raw,
	}
	return conn.WriteJSON(msg)
}

func recvJSON(conn *websocket.Conn, expectedType string, out interface{}) error {
	var msg WebSocketMessage
	if err := conn.ReadJSON(&msg); err != nil {
		return err
	}
	if msg.Type != expectedType {
		return fmt.Errorf("unexpected message type: %s, wanted: %s", msg.Type, expectedType)
	}
	return json.Unmarshal(msg.Data, out)
}

// ==================== File CRC ====================

func computeCRC32(data []byte) uint32 {
	return crc32.ChecksumIEEE(data)
}
