package com.example.s3rdma;

import com.sun.jna.Native;
import com.sun.jna.Pointer;
import org.json.JSONArray;
import org.json.JSONObject;

import java.nio.ByteBuffer;
import java.util.zip.CRC32;

/**
 * Holds the RDMA resources for one transfer. Mirrors rdmalib.h / rdmalib.py:
 *   - recvSetup() ~= rdma_recv_setup() / RDMASession.recv_setup()
 *   - sendSetup() ~= rdma_send_setup() / RDMASession.send_setup()
 *   - destroy()   ~= rdma_session_destroy() / sess.destroy()
 *   - crc32()     ~= rdma_crc32()
 */
public class RdmaSession implements AutoCloseable {

    public static final byte RDMA_PORT = 1;
    public static final byte RDMA_GID_IDX = 0;

    private Pointer ctx;
    private Pointer pd;
    private Pointer cq;
    private Pointer qp;
    private Pointer mr;
    private ByteBuffer buf;

    private RdmaSession() {
    }

    /** The memory region used for this transfer (direct ByteBuffer). */
    public ByteBuffer getBuffer() {
        return buf;
    }

    // ---------------- internal helpers ----------------

    private void cleanup() {
        RdmaVerbs lib = RdmaVerbs.INSTANCE;
        if (mr != null) {
            lib.rdma_dereg_mr(mr);
            mr = null;
        }
        if (qp != null) {
            lib.rdma_destroy_qp(qp);
            qp = null;
        }
        if (cq != null) {
            lib.rdma_destroy_cq(cq);
            cq = null;
        }
        if (pd != null) {
            lib.rdma_dealloc_pd(pd);
            pd = null;
        }
        if (ctx != null) {
            lib.rdma_close_device(ctx);
            ctx = null;
        }
        buf = null; // direct buffer is reclaimed by GC
    }

    /** Release all RDMA resources (dereg MR, destroy QP/CQ/PD, close device). */
    public void destroy() {
        cleanup();
    }

    @Override
    public void close() {
        destroy();
    }

    private static void sendJson(SyncWebSocket ws, String type, JSONObject data) {
        JSONObject msg = new JSONObject();
        msg.put("type", type);
        msg.put("data", data);
        ws.sendText(msg.toString());
    }

    private static JSONObject recvJson(SyncWebSocket ws, String expectedType) throws Exception {
        JSONObject msg = new JSONObject(ws.recvText());
        String type = msg.optString("type", "");
        if (!expectedType.equals(type)) {
            throw new RuntimeException("unexpected message type: " + type + ", wanted: " + expectedType);
        }
        JSONObject data = msg.optJSONObject("data");
        return data != null ? data : new JSONObject();
    }

    /**
     * Connect to ws_url, register request_id, and bring the RC QP up to RTS.
     * Returns the open websocket connection (caller sends the token and
     * closes it).
     */
    private SyncWebSocket handshake(String wsUrl, String devName, byte port, byte gidIndex, String requestId)
            throws Exception {
        SyncWebSocket ws = new SyncWebSocket(wsUrl);

        JSONObject regReq = new JSONObject();
        regReq.put("request_id", requestId);
        sendJson(ws, "register_request", regReq);

        JSONObject regResp = recvJson(ws, "register_response");
        if (!regResp.optBoolean("success", false)) {
            throw new RuntimeException("registration failed: " + regResp.optString("error", ""));
        }

        RdmaVerbs lib = RdmaVerbs.INSTANCE;

        ctx = lib.rdma_open_device(devName);
        if (ctx == null) {
            throw new RuntimeException("open_device(" + devName + ") failed");
        }

        pd = lib.rdma_alloc_pd(ctx);
        if (pd == null) {
            throw new RuntimeException("ibv_alloc_pd failed");
        }

        cq = lib.rdma_create_cq(ctx, 2);
        if (cq == null) {
            throw new RuntimeException("ibv_create_cq failed");
        }

        qp = lib.rdma_create_rc_qp(pd, cq, 1, 1, 0);
        if (qp == null) {
            throw new RuntimeException("ibv_create_qp failed");
        }

        if (lib.rdma_qp_to_init(qp, port) != 0) {
            throw new RuntimeException("qp_to_init failed");
        }

        byte[] gid = new byte[16];
        if (lib.rdma_query_gid(ctx, port, gidIndex, gid) != 0) {
            throw new RuntimeException("query_gid failed");
        }

        int mtuEnum = lib.rdma_query_port_mtu_enum(ctx, port);
        if (mtuEnum < 0) {
            throw new RuntimeException("query_port mtu failed");
        }

        long qpn = Integer.toUnsignedLong(lib.rdma_qp_num(qp));

        JSONArray gidArr = new JSONArray();
        for (byte b : gid) {
            gidArr.put(b & 0xFF);
        }

        JSONObject qpInfo = new JSONObject();
        qpInfo.put("qpn", qpn);
        qpInfo.put("lid", 0);
        qpInfo.put("gid", gidArr);
        qpInfo.put("gid_idx", gidIndex & 0xFF);
        qpInfo.put("mtu_enum", mtuEnum);
        sendJson(ws, "qpinfo_client", qpInfo);

        JSONObject srv = recvJson(ws, "qpinfo_server");
        int srvQpn = (int) srv.getLong("qpn");
        short srvLid = (short) srv.getInt("lid");
        JSONArray srvGidArr = srv.getJSONArray("gid");
        byte[] srvGid = new byte[16];
        for (int i = 0; i < 16; i++) {
            srvGid[i] = (byte) srvGidArr.getInt(i);
        }
        byte srvGidIdx = (byte) srv.getInt("gid_idx");
        int srvMtuEnum = srv.getInt("mtu_enum");

        if (lib.rdma_qp_to_rtr(qp, srvQpn, srvLid, srvGid, port, srvGidIdx, srvMtuEnum) != 0) {
            throw new RuntimeException("qp_to_rtr failed");
        }
        if (lib.rdma_qp_to_rts(qp) != 0) {
            throw new RuntimeException("qp_to_rts failed");
        }

        return ws;
    }

    private static void sendToken(SyncWebSocket ws, long addr, long rkey, long length) {
        JSONObject token = new JSONObject();
        token.put("addr", addr);
        token.put("rkey", rkey);
        token.put("start", 0);
        token.put("length", length);
        sendJson(ws, "token", token);
    }

    // ---------------- public factory methods ----------------

    /**
     * Receive side: register {@code length} bytes of memory as
     * remote-writable, bring up the QP, and send the rkey/address token to
     * the server so it can RDMA-write into it. The data can be read from
     * {@link #getBuffer()} after the server's write completes (and before
     * calling {@link #destroy()}).
     */
    public static RdmaSession recvSetup(String wsUrl, String devName, byte port, byte gidIndex,
                                         String requestId, long length) throws Exception {
        RdmaSession sess = new RdmaSession();
        SyncWebSocket ws = null;
        try {
            ws = sess.handshake(wsUrl, devName, port, gidIndex, requestId);

            int capacity = (int) Math.max(length, 1);
            sess.buf = ByteBuffer.allocateDirect(capacity);
            Pointer addr = Native.getDirectBufferPointer(sess.buf);

            sess.mr = RdmaVerbs.INSTANCE.rdma_reg_mr_dst(sess.pd, addr, length);
            if (sess.mr == null) {
                throw new RuntimeException("ibv_reg_mr failed");
            }

            long rkey = Integer.toUnsignedLong(RdmaVerbs.INSTANCE.rdma_mr_rkey(sess.mr));
            long address = Pointer.nativeValue(addr);

            sendToken(ws, address, rkey, length);

            ws.close();
            return sess;
        } catch (Exception e) {
            if (ws != null) {
                try {
                    ws.close();
                } catch (Exception ignore) {
                }
            }
            sess.cleanup();
            throw e;
        }
    }

    /**
     * Send side: copy {@code data} into a remote-readable memory region,
     * bring up the QP, and send the rkey/address token to the server so it
     * can RDMA-read it.
     */
    public static RdmaSession sendSetup(String wsUrl, String devName, byte port, byte gidIndex,
                                         String requestId, byte[] data) throws Exception {
        RdmaSession sess = new RdmaSession();
        SyncWebSocket ws = null;
        try {
            ws = sess.handshake(wsUrl, devName, port, gidIndex, requestId);

            int length = data.length;
            int capacity = Math.max(length, 1);
            sess.buf = ByteBuffer.allocateDirect(capacity);
            if (length > 0) {
                sess.buf.put(data);
                sess.buf.rewind();
            }
            Pointer addr = Native.getDirectBufferPointer(sess.buf);

            sess.mr = RdmaVerbs.INSTANCE.rdma_reg_mr_src(sess.pd, addr, length);
            if (sess.mr == null) {
                throw new RuntimeException("ibv_reg_mr failed");
            }

            long rkey = Integer.toUnsignedLong(RdmaVerbs.INSTANCE.rdma_mr_rkey(sess.mr));
            long address = Pointer.nativeValue(addr);

            sendToken(ws, address, rkey, length);

            ws.close();
            return sess;
        } catch (Exception e) {
            if (ws != null) {
                try {
                    ws.close();
                } catch (Exception ignore) {
                }
            }
            sess.cleanup();
            throw e;
        }
    }

    /** CRC32 (IEEE), equivalent to Go's hash/crc32.ChecksumIEEE. */
    public static long crc32(byte[] data) {
        CRC32 crc = new CRC32();
        crc.update(data);
        return crc.getValue();
    }
}
