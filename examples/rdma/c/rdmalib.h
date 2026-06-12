#ifndef RDMALIB_H
#define RDMALIB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rdma_session rdma_session_t;

/*
 * Full RDMA receive-side handshake over a websocket control channel:
 *   1. connect to ws_url and register request_id
 *   2. bring up an RC queue pair on dev_name/port/gid_index
 *   3. register `buf` (length `len`) as a remote-writable memory region
 *   4. send the rkey/address "token" to the server
 *   5. close the websocket connection
 *
 * Returns an opaque session handle on success, NULL on failure (err filled
 * in if non-NULL). The handle MUST be released with rdma_session_destroy()
 * after the server's RDMA write into `buf` has completed (e.g. after the
 * S3 GetObject call has returned).
 */
rdma_session_t *rdma_recv_setup(const char *ws_url,
                                 const char *dev_name,
                                 uint8_t port,
                                 uint8_t gid_index,
                                 const char *request_id,
                                 void *buf, size_t len,
                                 char *err, size_t errlen);

/*
 * Send-side counterpart of rdma_recv_setup(): registers `buf` (length `len`,
 * already filled with the data to upload) as a remote-readable memory
 * region, exchanges QP info, brings the QP up, and sends the rkey/address
 * token to the server so it can RDMA-read the data.
 *
 * Returns an opaque session handle on success, NULL on failure (err filled
 * in if non-NULL). The handle MUST be released with rdma_session_destroy()
 * after the server's RDMA read from `buf` has completed (e.g. after the
 * S3 PutObject call has returned).
 */
rdma_session_t *rdma_send_setup(const char *ws_url,
                                 const char *dev_name,
                                 uint8_t port,
                                 uint8_t gid_index,
                                 const char *request_id,
                                 void *buf, size_t len,
                                 char *err, size_t errlen);

/* Release all RDMA resources held by the session (dereg MR, destroy QP/CQ/PD, close device). */
void rdma_session_destroy(rdma_session_t *s);

/* CRC32 (IEEE), equivalent to Go's hash/crc32.ChecksumIEEE */
uint32_t rdma_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* RDMALIB_H */
