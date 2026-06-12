# -*- coding: utf-8 -*-
"""rdmalib.py - RDMA (libibverbs via librdmaverbs.so) + websocket JSON control
channel. Python 2.7 / 3.x compatible.

Mirrors rdmalib.h/rdmalib.c from the C implementation:
  - RDMASession.recv_setup(...)  ~= rdma_recv_setup()
  - RDMASession.send_setup(...)  ~= rdma_send_setup()
  - sess.destroy()                ~= rdma_session_destroy()
  - crc32(data)                   ~= rdma_crc32()
"""
from __future__ import print_function

import ctypes
import json
import mmap
import os
import time
import zlib

import websocket  # pip install websocket-client


RDMA_PORT = 1
RDMA_GID_IDX = 0


# ==================== load the verbs shim ====================

_LIBNAME = "librdmaverbs.so"
_libpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), _LIBNAME)
_lib = ctypes.CDLL(_libpath)

_lib.rdma_open_device.argtypes = [ctypes.c_char_p]
_lib.rdma_open_device.restype = ctypes.c_void_p

_lib.rdma_close_device.argtypes = [ctypes.c_void_p]
_lib.rdma_close_device.restype = None

_lib.rdma_alloc_pd.argtypes = [ctypes.c_void_p]
_lib.rdma_alloc_pd.restype = ctypes.c_void_p

_lib.rdma_dealloc_pd.argtypes = [ctypes.c_void_p]
_lib.rdma_dealloc_pd.restype = None

_lib.rdma_create_cq.argtypes = [ctypes.c_void_p, ctypes.c_int]
_lib.rdma_create_cq.restype = ctypes.c_void_p

_lib.rdma_destroy_cq.argtypes = [ctypes.c_void_p]
_lib.rdma_destroy_cq.restype = None

_lib.rdma_create_rc_qp.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                    ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32]
_lib.rdma_create_rc_qp.restype = ctypes.c_void_p

_lib.rdma_destroy_qp.argtypes = [ctypes.c_void_p]
_lib.rdma_destroy_qp.restype = None

_lib.rdma_qp_to_init.argtypes = [ctypes.c_void_p, ctypes.c_uint8]
_lib.rdma_qp_to_init.restype = ctypes.c_int

_lib.rdma_query_port_mtu_enum.argtypes = [ctypes.c_void_p, ctypes.c_uint8]
_lib.rdma_query_port_mtu_enum.restype = ctypes.c_int

_lib.rdma_query_gid.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_int,
                                 ctypes.POINTER(ctypes.c_uint8)]
_lib.rdma_query_gid.restype = ctypes.c_int

_lib.rdma_qp_to_rtr.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint16,
                                 ctypes.POINTER(ctypes.c_uint8),
                                 ctypes.c_uint8, ctypes.c_uint8, ctypes.c_int]
_lib.rdma_qp_to_rtr.restype = ctypes.c_int

_lib.rdma_qp_to_rts.argtypes = [ctypes.c_void_p]
_lib.rdma_qp_to_rts.restype = ctypes.c_int

_lib.rdma_reg_mr_dst.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
_lib.rdma_reg_mr_dst.restype = ctypes.c_void_p

_lib.rdma_reg_mr_src.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
_lib.rdma_reg_mr_src.restype = ctypes.c_void_p

_lib.rdma_dereg_mr.argtypes = [ctypes.c_void_p]
_lib.rdma_dereg_mr.restype = None

_lib.rdma_qp_num.argtypes = [ctypes.c_void_p]
_lib.rdma_qp_num.restype = ctypes.c_uint32

_lib.rdma_mr_lkey.argtypes = [ctypes.c_void_p]
_lib.rdma_mr_lkey.restype = ctypes.c_uint32

_lib.rdma_mr_rkey.argtypes = [ctypes.c_void_p]
_lib.rdma_mr_rkey.restype = ctypes.c_uint32


# ==================== websocket JSON protocol ====================

def _send_json(ws, msg_type, data):
    ws.send(json.dumps({"type": msg_type, "data": data}))


def _recv_json(ws, expected_type):
    raw = ws.recv()
    msg = json.loads(raw)
    if msg.get("type") != expected_type:
        raise RuntimeError("unexpected message type: %s, wanted: %s" % (msg.get("type"), expected_type))
    return msg.get("data", {})


# ==================== RDMA session ====================

class RDMASession(object):
    """Holds the RDMA resources for one transfer. Release with destroy()
    after the server's RDMA write/read has completed."""

    def __init__(self):
        self.ctx = None
        self.pd = None
        self.cq = None
        self.qp = None
        self.mr = None
        self.buf = None  # mmap.mmap object

    # ---------------- internal helpers ----------------

    def _cleanup(self):
        if self.mr:
            _lib.rdma_dereg_mr(self.mr)
            self.mr = None
        if self.qp:
            _lib.rdma_destroy_qp(self.qp)
            self.qp = None
        if self.cq:
            _lib.rdma_destroy_cq(self.cq)
            self.cq = None
        if self.pd:
            _lib.rdma_dealloc_pd(self.pd)
            self.pd = None
        if self.ctx:
            _lib.rdma_close_device(self.ctx)
            self.ctx = None
        if self.buf is not None:
            try:
                self.buf.close()
            except Exception:
                pass
            self.buf = None

    def destroy(self):
        """Release all RDMA resources (dereg MR, destroy QP/CQ/PD, close device)."""
        self._cleanup()

    @staticmethod
    def _buf_address(buf):
        arr = (ctypes.c_char * len(buf)).from_buffer(buf)
        return ctypes.addressof(arr)

    def _handshake(self, ws_url, dev_name, port, gid_index, request_id):
        """Connect to ws_url, register request_id, bring the RC QP up to
        RTS. Returns the open websocket connection."""
        ws = websocket.create_connection(ws_url)

        _send_json(ws, "register_request", {"request_id": request_id})
        reg = _recv_json(ws, "register_response")
        if not reg.get("success"):
            raise RuntimeError("registration failed: %s" % reg.get("error", ""))

        dev_bytes = dev_name.encode("ascii") if dev_name else None
        self.ctx = _lib.rdma_open_device(dev_bytes)
        if not self.ctx:
            raise RuntimeError("open_device(%s) failed" % (dev_name or "",))

        self.pd = _lib.rdma_alloc_pd(self.ctx)
        if not self.pd:
            raise RuntimeError("ibv_alloc_pd failed")

        self.cq = _lib.rdma_create_cq(self.ctx, 2)
        if not self.cq:
            raise RuntimeError("ibv_create_cq failed")

        self.qp = _lib.rdma_create_rc_qp(self.pd, self.cq, 1, 1, 0)
        if not self.qp:
            raise RuntimeError("ibv_create_qp failed")

        if _lib.rdma_qp_to_init(self.qp, port) != 0:
            raise RuntimeError("qp_to_init failed")

        gid_buf = (ctypes.c_uint8 * 16)()
        if _lib.rdma_query_gid(self.ctx, port, gid_index, gid_buf) != 0:
            raise RuntimeError("query_gid failed")
        gid = list(gid_buf)

        mtu_enum = _lib.rdma_query_port_mtu_enum(self.ctx, port)
        if mtu_enum < 0:
            raise RuntimeError("query_port mtu failed")

        qpn = _lib.rdma_qp_num(self.qp)

        _send_json(ws, "qpinfo_client", {
            "qpn": qpn,
            "lid": 0,
            "gid": gid,
            "gid_idx": gid_index,
            "mtu_enum": mtu_enum,
        })

        srv = _recv_json(ws, "qpinfo_server")
        srv_gid = (ctypes.c_uint8 * 16)(*srv["gid"])

        if _lib.rdma_qp_to_rtr(self.qp, srv["qpn"], srv["lid"], srv_gid,
                                port, srv["gid_idx"], srv["mtu_enum"]) != 0:
            raise RuntimeError("qp_to_rtr failed")

        if _lib.rdma_qp_to_rts(self.qp) != 0:
            raise RuntimeError("qp_to_rts failed")

        return ws

    # ---------------- public factory methods ----------------

    @classmethod
    def recv_setup(cls, ws_url, dev_name, port, gid_index, request_id, length):
        """Receive side: register `length` bytes of memory as remote-writable,
        bring up the QP, and send the rkey/address token to the server so it
        can RDMA-write into it. The data can be read from sess.buf after the
        server's write completes (and before calling destroy())."""
        sess = cls()
        ws = None
        try:
            ws = sess._handshake(ws_url, dev_name, port, gid_index, request_id)

            sess.buf = mmap.mmap(-1, max(length, 1))
            addr = cls._buf_address(sess.buf)

            sess.mr = _lib.rdma_reg_mr_dst(sess.pd, ctypes.c_void_p(addr), length)
            if not sess.mr:
                raise RuntimeError("ibv_reg_mr failed")

            rkey = _lib.rdma_mr_rkey(sess.mr)
            _send_json(ws, "token", {"addr": addr, "rkey": rkey, "start": 0, "length": length})

            ws.close()
            return sess
        except Exception:
            if ws is not None:
                try:
                    ws.close()
                except Exception:
                    pass
            sess._cleanup()
            raise

    @classmethod
    def send_setup(cls, ws_url, dev_name, port, gid_index, request_id, data):
        """Send side: copy `data` (bytes) into a remote-readable memory
        region, bring up the QP, and send the rkey/address token to the
        server so it can RDMA-read it."""
        sess = cls()
        ws = None
        try:
            ws = sess._handshake(ws_url, dev_name, port, gid_index, request_id)

            length = len(data)
            sess.buf = mmap.mmap(-1, max(length, 1))
            if length > 0:
                sess.buf.write(data)
            addr = cls._buf_address(sess.buf)

            sess.mr = _lib.rdma_reg_mr_src(sess.pd, ctypes.c_void_p(addr), length)
            if not sess.mr:
                raise RuntimeError("ibv_reg_mr failed")

            rkey = _lib.rdma_mr_rkey(sess.mr)
            _send_json(ws, "token", {"addr": addr, "rkey": rkey, "start": 0, "length": length})

            ws.close()
            return sess
        except Exception:
            if ws is not None:
                try:
                    ws.close()
                except Exception:
                    pass
            sess._cleanup()
            raise


def crc32(data):
    """CRC32 (IEEE), equivalent to Go's hash/crc32.ChecksumIEEE."""
    return zlib.crc32(data) & 0xFFFFFFFF
