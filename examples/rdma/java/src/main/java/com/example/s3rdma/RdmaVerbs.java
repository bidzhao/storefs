package com.example.s3rdma;

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;

/**
 * ctypes-style binding to native/librdmaverbs.so (verbs_shim.c).
 * Mirrors the function set used by rdmalib.{h,c} / rdmalib.py.
 *
 * Loading: JNA looks up "librdmaverbs.so" via jna.library.path,
 * java.library.path, LD_LIBRARY_PATH, etc. Point one of those at the
 * directory containing the built native/librdmaverbs.so, e.g.:
 *   java -Djna.library.path=native -jar target/s3rdma.jar ...
 */
public interface RdmaVerbs extends Library {

    RdmaVerbs INSTANCE = Native.load("rdmaverbs", RdmaVerbs.class);

    Pointer rdma_open_device(String devName);

    void rdma_close_device(Pointer ctx);

    Pointer rdma_alloc_pd(Pointer ctx);

    void rdma_dealloc_pd(Pointer pd);

    Pointer rdma_create_cq(Pointer ctx, int cqe);

    void rdma_destroy_cq(Pointer cq);

    Pointer rdma_create_rc_qp(Pointer pd, Pointer cq, int maxSend, int maxRecv, int maxInline);

    void rdma_destroy_qp(Pointer qp);

    int rdma_qp_to_init(Pointer qp, byte port);

    int rdma_query_port_mtu_enum(Pointer ctx, byte port);

    int rdma_query_gid(Pointer ctx, byte port, int gidIndex, byte[] gidOut /* 16 bytes */);

    int rdma_qp_to_rtr(Pointer qp, int remoteQpn, short dlid, byte[] dgid /* 16 bytes */,
                       byte port, byte gidIndex, int mtuEnum);

    int rdma_qp_to_rts(Pointer qp);

    /** Receive side: server RDMA-writes into this MR. */
    Pointer rdma_reg_mr_dst(Pointer pd, Pointer addr, long len);

    /** Send side: server RDMA-reads from this MR. */
    Pointer rdma_reg_mr_src(Pointer pd, Pointer addr, long len);

    void rdma_dereg_mr(Pointer mr);

    int rdma_qp_num(Pointer qp);

    int rdma_mr_lkey(Pointer mr);

    int rdma_mr_rkey(Pointer mr);
}
