#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""s3rdmaput.py - upload a file to S3 via RDMA, using rdmalib + s3client.
Python 2.7 / 3.x compatible.

Usage:
  python s3rdmaput.py -bucket <bucket> -object <key> -file <path>
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
    p.add_argument("-file", required=True, help="Path to the file to upload")
    p.add_argument("-endpoint", default="http://127.0.0.1:8901", help="S3 endpoint URL")
    p.add_argument("-rdma-dev", dest="rdma_dev", default="rxe0", help="RDMA device name")
    p.add_argument("-ak", default="admin-ak", help="S3 access key")
    p.add_argument("-sk", default="admin-sk", help="S3 secret key")
    args = p.parse_args()

    cfg = s3client.S3Config(endpoint=args.endpoint, region="us-east-1",
                             access_key=args.ak, secret_key=args.sk)

    # Step 1: read the file into memory
    try:
        with open(args.file, "rb") as f:
            data = f.read()
    except IOError as e:
        print("Failed to read file: %s" % e, file=sys.stderr)
        return 1

    request_id = "rdma-%d" % os.getpid()

    # Step 2: RDMA handshake (register, QP setup, copy data into mmap'd MR, send token)
    ws_url = ws_url_from_endpoint(args.endpoint)
    print("Initializing RDMA resources and exchanging QP info...", file=sys.stderr)
    try:
        sess = rdmalib.RDMASession.send_setup(ws_url, args.rdma_dev, rdmalib.RDMA_PORT,
                                               rdmalib.RDMA_GID_IDX, request_id, data)
    except Exception as e:
        print("RDMA setup failed: %s" % e, file=sys.stderr)
        return 1

    # Step 3: trigger the server-side RDMA read via S3 PutObject (empty body)
    print("Sending S3 PutObject request...", file=sys.stderr)
    try:
        s3client.put_object(cfg, args.bucket, args.object,
                             ("X-RDMA-Request-ID", request_id))
    except Exception as e:
        print("PutObject error: %s" % e, file=sys.stderr)
        sess.destroy()
        return 1

    print("Successfully uploaded %s to s3://%s/%s via RDMA (HTTP 200 OK)" %
          (args.file, args.bucket, args.object), file=sys.stderr)

    # Step 4: RDMA read is complete now; release RDMA resources
    sess.destroy()
    return 0


if __name__ == "__main__":
    sys.exit(main())
