# -*- coding: utf-8 -*-
"""s3client.py - minimal S3 HEAD/GET/PUT with AWS SigV4 signing.
Python 2.7 / 3.x compatible.

Mirrors s3client.h/s3client.c from the C implementation: signs only
`host` and `x-amz-date` (matching the working Go client / server's
auth.go), uses payload hash "UNSIGNED-PAYLOAD" (the server's default
when no x-amz-content-sha256 header is present), and never sends an
`Accept` header (so the server-side AWS SDK v4 re-signing doesn't pick
up extra signed headers the client didn't account for).
"""
from __future__ import print_function

import hashlib
import hmac
import time

try:                       # Python 3
    from urllib.parse import urlsplit
except ImportError:        # Python 2
    from urlparse import urlsplit

try:                        # Python 3
    import http.client as http_client
except ImportError:         # Python 2
    import httplib as http_client


UNSIGNED_PAYLOAD = "UNSIGNED-PAYLOAD"


class S3Config(object):
    def __init__(self, endpoint, region, access_key, secret_key):
        self.endpoint = endpoint        # e.g. "http://127.0.0.1:8901"
        self.region = region            # e.g. "us-east-1"
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


def _sign_request(cfg, method, canonical_uri, host, amzdate, datestamp):
    """Build the Authorization header value, signing only host;x-amz-date
    (matches the working Go client / server's auth.go re-signing)."""
    canonical_headers = "host:%s\nx-amz-date:%s\n" % (host, amzdate)
    signed_headers = "host;x-amz-date"

    canonical_request = "%s\n%s\n\n%s\n%s\n%s" % (
        method, canonical_uri, canonical_headers, signed_headers, UNSIGNED_PAYLOAD)

    creq_hash = _sha256_hex(canonical_request)

    scope = "%s/%s/s3/aws4_request" % (datestamp, cfg.region)
    string_to_sign = "AWS4-HMAC-SHA256\n%s\n%s\n%s" % (amzdate, scope, creq_hash)

    signing_key = _derive_signing_key(cfg.secret_key, datestamp, cfg.region)
    signature = hmac.new(signing_key, _to_bytes(string_to_sign), hashlib.sha256).hexdigest()

    return "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s" % (
        cfg.access_key, scope, signed_headers, signature)


def _request(cfg, method, bucket, key, extra_header=None):
    """Send a SigV4-signed request to /bucket/key (or /bucket if key is
    empty). Returns (status_code, response_headers_dict). The response body
    is never read (the RDMA path delivers the actual object data)."""
    parts = urlsplit(cfg.endpoint)
    host = parts.netloc
    hostname = parts.hostname
    port = parts.port or (443 if parts.scheme == "https" else 80)

    now = time.gmtime()
    amzdate = time.strftime("%Y%m%dT%H%M%SZ", now)
    datestamp = time.strftime("%Y%m%d", now)

    if key:
        canonical_uri = "/%s/%s" % (bucket, key)
    else:
        canonical_uri = "/%s" % (bucket,)

    authorization = _sign_request(cfg, method, canonical_uri, host, amzdate, datestamp)

    headers = [
        ("Host", host),
        ("x-amz-date", amzdate),
        ("Authorization", authorization),
    ]
    if method == "PUT":
        headers.append(("Content-Length", "0"))
    if extra_header:
        headers.append(extra_header)

    if parts.scheme == "https":
        conn = http_client.HTTPSConnection(hostname, port)
    else:
        conn = http_client.HTTPConnection(hostname, port)

    try:
        conn.putrequest(method, canonical_uri, skip_host=True, skip_accept_encoding=True)
        for k, v in headers:
            conn.putheader(k, v)
        conn.endheaders()

        resp = conn.getresponse()
        status = resp.status
        resp_headers = dict((k.lower(), v) for k, v in resp.getheaders())
        return status, resp_headers
    finally:
        conn.close()


def head_object(cfg, bucket, key):
    """HTTP HEAD on bucket/key, returns Content-Length as an int."""
    status, headers = _request(cfg, "HEAD", bucket, key)
    if status != 200:
        raise RuntimeError("HEAD failed: HTTP %d" % status)
    length = headers.get("content-length")
    if length is None:
        raise RuntimeError("HEAD response missing Content-Length")
    return int(length)


def get_object(cfg, bucket, key, extra_header=None):
    """HTTP GET on bucket/key. The actual object data arrives via RDMA, so
    the HTTP response body is ignored."""
    status, _ = _request(cfg, "GET", bucket, key, extra_header)
    if status != 200:
        raise RuntimeError("GetObject failed: HTTP %d" % status)


def put_object(cfg, bucket, key, extra_header=None):
    """HTTP PUT on bucket/key with an empty body (Content-Length: 0). The
    actual object data is transferred out-of-band via RDMA."""
    status, _ = _request(cfg, "PUT", bucket, key, extra_header)
    if status not in (200, 201):
        raise RuntimeError("PutObject failed: HTTP %d" % status)
