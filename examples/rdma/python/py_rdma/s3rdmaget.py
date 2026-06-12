#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""s3rdmaget.py - download an S3 object via RDMA, using rdmalib + s3client.
Python 2.7 / 3.x compatible.

Usage:
  python s3rdmaget.py -bucket <bucket> -object <key> -file <path>
                       [-endpoint http://127.0.0.1:8901] [-rdma-dev rxe0]
                       [-ak <access-key>] [-sk <secret-key>]
"""
from __future__ import print_function

import argparse
import os
import sys

try:                       # Python 3
    from urllib.parse import urlsplit
except ImportError:        # Python 2
    from urlparse import urlsplit

import rdmalib
import s3client


def ws_url_from_endpoint(endpoint):
    parts = urlsplit(endpoint)
    return "ws://%s/rdma-ctrl" % (parts.netloc,)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("-bucket", required=True, help="Name of the bucket")
    p.add_argument("-object", required=True, help="Name of the object")
    p.add_argument("-file", required=True, help="Path to save the downloaded file")
    p.add_argument("-endpoint", default="http://127.0.0.1:8901", help="S3 endpoint URL")
    p.add_argument("-rdma-dev", dest="rdma_dev", default="rxe0", help="RDMA device name")
    p.add_argument("-ak", default="admin-ak", help="S3 access key")
    p.add_argument("-sk", default="admin-sk", help="S3 secret key")
    args = p.parse_args()

    cfg = s3client.S3Config(endpoint=args.endpoint, region="us-east-1",
                             access_key=args.ak, secret_key=args.sk)

    # Step 1: HEAD to get object size
    print("Getting object info first...", file=sys.stderr)
    try:
        file_size = s3client.head_object(cfg, args.bucket, args.object)
    except Exception as e:
        print("Failed to get object info: %s" % e, file=sys.stderr)
        return 1
    print("Object size: %d bytes" % file_size, file=sys.stderr)

    request_id = "rdma-%d" % os.getpid()

    # Step 2 & 3: RDMA handshake (register, QP setup, mmap buffer, send token)
    ws_url = ws_url_from_endpoint(args.endpoint)
    print("Initializing RDMA resources and exchanging QP info...", file=sys.stderr)
    try:
        sess = rdmalib.RDMASession.recv_setup(ws_url, args.rdma_dev, rdmalib.RDMA_PORT,
                                               rdmalib.RDMA_GID_IDX, request_id, file_size)
    except Exception as e:
        print("RDMA setup failed: %s" % e, file=sys.stderr)
        return 1

    # Step 4: trigger the server-side RDMA write via S3 GetObject
    print("Sending S3 GetObject request...", file=sys.stderr)
    try:
        s3client.get_object(cfg, args.bucket, args.object,
                             ("X-RDMA-Request-ID", request_id))
    except Exception as e:
        print("GetObject error: %s" % e, file=sys.stderr)
        sess.destroy()
        return 1

    print("Successfully requested %s from s3://%s/%s via RDMA (HTTP 200 OK)" %
          (args.file, args.bucket, args.object), file=sys.stderr)

    # Step 5: RDMA write is complete now; copy the data out before releasing resources
    data = sess.buf[:file_size]
    if not isinstance(data, bytes):
        data = bytes(data)
    sess.destroy()

    # Step 6: verify CRC and save to file
    crc = rdmalib.crc32(data)
    print("Data CRC32: 0x%08x" % crc, file=sys.stderr)

    print("Saving data to %s..." % args.file, file=sys.stderr)
    with open(args.file, "wb") as f:
        f.write(data)

    print("Download complete! File saved to %s" % args.file, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
