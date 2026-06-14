#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""s3rdmamultipart.py - multipart upload to S3 via RDMA.
Python 2.7 / 3.x compatible.
"""
from __future__ import print_function

import argparse
import hashlib
import hmac
import os
import sys
import time
try:
    from urllib.parse import urlsplit
except ImportError:
    from urlparse import urlsplit
try:
    import http.client as http_client
except ImportError:
    import httplib as http_client
try:
    import xml.etree.ElementTree as ET
except ImportError:
    from xml.etree import ElementTree as ET

import rdmalib

UNSIGNED_PAYLOAD = "UNSIGNED-PAYLOAD"


class S3Config(object):
    def __init__(self, endpoint, region, access_key, secret_key):
        self.endpoint = endpoint
        self.region = region
        self.access_key = access_key
        self.secret_key = secret_key


def _to_bytes(s):
    if isinstance(s, bytes):
        return s
    return s.encode("utf-8")


def _hmac_sha256(key, msg):
    return hmac.new(key, _to_bytes(msg), hashlib.sha256).digest()


def _sha256_hex(s):
    return hashlib.sha256(_to_bytes(s)).hexdigest()


def _derive_signing_key(secret_key, datestamp, region):
    k_date = _hmac_sha256(_to_bytes("AWS4" + secret_key), datestamp)
    k_region = _hmac_sha256(k_date, region)
    k_service = _hmac_sha256(k_region, "s3")
    return _hmac_sha256(k_service, "aws4_request")


def _sign_request(cfg, method, canonical_uri, canonical_query, host, amzdate, datestamp, content_length=None):
    if content_length is None:
        canonical_headers = "host:%s\nx-amz-date:%s\n" % (host, amzdate)
        signed_headers = "host;x-amz-date"
    else:
        canonical_headers = "content-length:%d\nhost:%s\nx-amz-date:%s\n" % (content_length, host, amzdate)
        signed_headers = "content-length;host;x-amz-date"

    canonical_request = "%s\n%s\n%s\n%s\n%s\n%s" % (
        method, canonical_uri, canonical_query or "", canonical_headers, signed_headers, UNSIGNED_PAYLOAD)
    creq_hash = _sha256_hex(canonical_request)

    scope = "%s/%s/s3/aws4_request" % (datestamp, cfg.region)
    string_to_sign = "AWS4-HMAC-SHA256\n%s\n%s\n%s" % (amzdate, scope, creq_hash)
    signing_key = _derive_signing_key(cfg.secret_key, datestamp, cfg.region)
    signature = hmac.new(signing_key, _to_bytes(string_to_sign), hashlib.sha256).hexdigest()

    return "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s" % (
        cfg.access_key, scope, signed_headers, signature)


def _request(cfg, method, bucket, key=None, query="", body=None, extra_header=None):
    parts = urlsplit(cfg.endpoint)
    host = parts.netloc
    hostname = parts.hostname
    port = parts.port or (443 if parts.scheme == "https" else 80)
    canonical_uri = "/%s/%s" % (bucket, key) if key else "/%s" % bucket
    path = canonical_uri + (("?" + query) if query else "")

    now = time.gmtime()
    amzdate = time.strftime("%Y%m%dT%H%M%SZ", now)
    datestamp = time.strftime("%Y%m%d", now)
    body_bytes = _to_bytes(body) if body is not None else None
    content_length = len(body_bytes) if body_bytes is not None else None
    authorization = _sign_request(cfg, method, canonical_uri, query, host, amzdate, datestamp, content_length)

    headers = [("Host", host), ("x-amz-date", amzdate), ("Authorization", authorization)]
    if content_length is not None:
        headers.append(("Content-Length", str(content_length)))
    elif method == "PUT":
        headers.append(("Content-Length", "0"))
    if extra_header:
        headers.append(extra_header)

    conn_cls = http_client.HTTPSConnection if parts.scheme == "https" else http_client.HTTPConnection
    conn = conn_cls(hostname, port)
    try:
        conn.putrequest(method, path, skip_host=True, skip_accept_encoding=True)
        for k, v in headers:
            conn.putheader(k, v)
        conn.endheaders()
        if body_bytes is not None and body_bytes:
            conn.send(body_bytes)
        resp = conn.getresponse()
        data = resp.read()
        if not isinstance(data, str):
            data = data.decode("utf-8", "replace")
        if resp.status < 200 or resp.status >= 300:
            raise RuntimeError("%s failed: HTTP %d body=%s" % (method, resp.status, data))
        return data
    finally:
        conn.close()


def ws_url_from_endpoint(endpoint):
    parts = urlsplit(endpoint)
    scheme = "wss" if parts.scheme == "https" else "ws"
    return "%s://%s/rdma-ctrl" % (scheme, parts.netloc)


def _text(root, name):
    node = root.find(".//" + name)
    return node.text if node is not None and node.text is not None else ""


def create_multipart(cfg, bucket, obj):
    xml = _request(cfg, "POST", bucket, obj, "uploads=")
    upload_id = _text(ET.fromstring(xml), "UploadId")
    if not upload_id:
        raise RuntimeError("CreateMultipartUpload response missing UploadId: %s" % xml)
    print("Successfully created multipart upload")
    print("Upload ID: %s" % upload_id)
    return upload_id


def upload_part_rdma(cfg, args, upload_id, part_number, data):
    request_id = "rdma-multipart-%d-part-%d-%d" % (os.getpid(), part_number, int(time.time() * 1000000))
    sess = rdmalib.RDMASession.send_setup(ws_url_from_endpoint(args.endpoint), args.rdma_dev,
                                           rdmalib.RDMA_PORT, rdmalib.RDMA_GID_IDX, request_id, data)
    try:
        query = "partNumber=%d&uploadId=%s" % (part_number, upload_id)
        _request(cfg, "PUT", args.bucket, args.object, query, None, ("X-RDMA-Request-ID", request_id))
    finally:
        sess.destroy()
    etag = '"part-%d"' % part_number
    print("Uploaded part %d, size=%d, ETag=%s" % (part_number, len(data), etag), file=sys.stderr)
    return {"PartNumber": part_number, "ETag": etag, "Size": len(data)}


def upload_parts(cfg, args, upload_id):
    size = os.path.getsize(args.file)
    if size <= 0:
        raise RuntimeError("multipart RDMA upload does not support empty files")
    num_parts = int((size + args.partsize - 1) // args.partsize)
    if args.part < 0 or args.part > num_parts:
        raise RuntimeError("part must be between 1 and %d, or 0 for all parts" % num_parts)
    start = args.part or 1
    end = args.part or num_parts
    parts = []
    with open(args.file, "rb") as f:
        for part_number in range(start, end + 1):
            f.seek((part_number - 1) * args.partsize)
            data = f.read(args.partsize)
            parts.append(upload_part_rdma(cfg, args, upload_id, part_number, data))
    return parts


def list_parts(cfg, bucket, obj, upload_id):
    xml = _request(cfg, "GET", bucket, obj, "uploadId=%s" % upload_id)
    root = ET.fromstring(xml)
    parts = []
    for node in root.findall(".//Part"):
        parts.append({
            "PartNumber": int(_text(node, "PartNumber")),
            "ETag": _text(node, "ETag"),
            "Size": int(_text(node, "Size") or 0),
        })
    return parts


def print_parts(parts, upload_id):
    print("Parts uploaded for upload ID %s:" % upload_id)
    for p in parts:
        print("Part %d: ETag=%s, Size=%d bytes" % (p["PartNumber"], p["ETag"], p.get("Size", 0)))


def complete_multipart(cfg, bucket, obj, upload_id, parts):
    body = ["<CompleteMultipartUpload>"]
    for p in sorted(parts, key=lambda x: x["PartNumber"]):
        body.append("<Part><PartNumber>%d</PartNumber><ETag>%s</ETag></Part>" % (p["PartNumber"], p["ETag"]))
    body.append("</CompleteMultipartUpload>")
    xml = _request(cfg, "POST", bucket, obj, "uploadId=%s" % upload_id, "".join(body))
    print(xml)
    print("Successfully completed multipart upload")


def abort_multipart(cfg, bucket, obj, upload_id):
    _request(cfg, "DELETE", bucket, obj, "uploadId=%s" % upload_id)
    print("Successfully aborted multipart upload")


def list_uploads(cfg, args):
    qs = ["uploads="]
    if args.delimiter:
        qs.append("delimiter=%s" % args.delimiter)
    if args.key_marker:
        qs.append("key-marker=%s" % args.key_marker)
    if args.max_uploads:
        qs.append("max-uploads=%d" % args.max_uploads)
    if args.prefix:
        qs.append("prefix=%s" % args.prefix)
    if args.upload_id_marker:
        qs.append("upload-id-marker=%s" % args.upload_id_marker)
    print(_request(cfg, "GET", args.bucket, None, "&".join(qs)))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("-action", default="all", choices=["all", "create", "upload", "list", "list-uploads", "complete", "abort"])
    p.add_argument("-bucket", required=True)
    p.add_argument("-object")
    p.add_argument("-file")
    p.add_argument("-uploadid")
    p.add_argument("-part", type=int, default=0)
    p.add_argument("-partsize", type=int, default=5 * 1024 * 1024)
    p.add_argument("-endpoint", default="http://127.0.0.1:8901")
    p.add_argument("-rdma-dev", dest="rdma_dev", default="rxe0")
    p.add_argument("-ak", default="admin-ak")
    p.add_argument("-sk", default="admin-sk")
    p.add_argument("-prefix", default="")
    p.add_argument("-delimiter", default="")
    p.add_argument("-max-uploads", dest="max_uploads", type=int, default=1000)
    p.add_argument("-key-marker", dest="key_marker", default="")
    p.add_argument("-upload-id-marker", dest="upload_id_marker", default="")
    args = p.parse_args()

    if args.action != "list-uploads" and not args.object:
        p.error("-object is required for this action")
    cfg = S3Config(args.endpoint, "us-east-1", args.ak, args.sk)

    if args.action == "create":
        create_multipart(cfg, args.bucket, args.object)
    elif args.action == "upload":
        if not args.file or not args.uploadid:
            p.error("-action upload requires -file and -uploadid")
        for part in upload_parts(cfg, args, args.uploadid):
            print("Part %d ETag: %s" % (part["PartNumber"], part["ETag"]))
    elif args.action == "list":
        if not args.uploadid:
            p.error("-action list requires -uploadid")
        print_parts(list_parts(cfg, args.bucket, args.object, args.uploadid), args.uploadid)
    elif args.action == "list-uploads":
        list_uploads(cfg, args)
    elif args.action == "complete":
        if not args.uploadid:
            p.error("-action complete requires -uploadid")
        parts = list_parts(cfg, args.bucket, args.object, args.uploadid)
        if not parts:
            raise RuntimeError("no uploaded parts found for uploadId %s" % args.uploadid)
        complete_multipart(cfg, args.bucket, args.object, args.uploadid, parts)
    elif args.action == "abort":
        if not args.uploadid:
            p.error("-action abort requires -uploadid")
        abort_multipart(cfg, args.bucket, args.object, args.uploadid)
    elif args.action == "all":
        if not args.file:
            p.error("-action all requires -file")
        upload_id = create_multipart(cfg, args.bucket, args.object)
        try:
            parts = upload_parts(cfg, args, upload_id)
            complete_multipart(cfg, args.bucket, args.object, upload_id, parts)
        except Exception:
            abort_multipart(cfg, args.bucket, args.object, upload_id)
            raise
    return 0


if __name__ == "__main__":
    sys.exit(main())
