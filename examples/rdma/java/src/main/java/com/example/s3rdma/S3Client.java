package com.example.s3rdma;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import javax.net.ssl.SSLSocketFactory;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.Socket;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.time.ZoneOffset;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Minimal S3 HEAD/GET/PUT with AWS SigV4 signing. Mirrors s3client.h /
 * s3client.c / s3client.py: signs only `host` and `x-amz-date` (matching the
 * working Go client / server's auth.go), uses payload hash
 * "UNSIGNED-PAYLOAD" (the server's default when no x-amz-content-sha256
 * header is present), and sends only the headers it explicitly lists (no
 * Accept/Accept-Encoding/User-Agent that an HTTP client might add by
 * default, which the server-side AWS SDK v4 re-signing would otherwise pick
 * up as extra signed headers).
 */
public class S3Client {

    private static final String UNSIGNED_PAYLOAD = "UNSIGNED-PAYLOAD";
    private static final DateTimeFormatter AMZ_DATE_FMT = DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'");
    private static final DateTimeFormatter DATE_STAMP_FMT = DateTimeFormatter.ofPattern("yyyyMMdd");

    private final String endpoint;
    private final String region;
    private final String accessKey;
    private final String secretKey;

    public S3Client(String endpoint, String region, String accessKey, String secretKey) {
        this.endpoint = endpoint;
        this.region = region;
        this.accessKey = accessKey;
        this.secretKey = secretKey;
    }

    // ==================== SigV4 ====================

    private static byte[] hmacSha256(byte[] key, String data) throws Exception {
        Mac mac = Mac.getInstance("HmacSHA256");
        mac.init(new SecretKeySpec(key, "HmacSHA256"));
        return mac.doFinal(data.getBytes(StandardCharsets.UTF_8));
    }

    private static String sha256Hex(String data) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        return hex(md.digest(data.getBytes(StandardCharsets.UTF_8)));
    }

    private static String hex(byte[] data) {
        StringBuilder sb = new StringBuilder(data.length * 2);
        for (byte b : data) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }

    private String signRequest(String method, String canonicalUri, String host, String amzdate, String datestamp)
            throws Exception {
        String canonicalHeaders = "host:" + host + "\nx-amz-date:" + amzdate + "\n";
        String signedHeaders = "host;x-amz-date";

        String canonicalRequest = method + "\n" + canonicalUri + "\n\n"
                + canonicalHeaders + "\n" + signedHeaders + "\n" + UNSIGNED_PAYLOAD;

        String creqHash = sha256Hex(canonicalRequest);

        String scope = datestamp + "/" + region + "/s3/aws4_request";
        String stringToSign = "AWS4-HMAC-SHA256\n" + amzdate + "\n" + scope + "\n" + creqHash;

        byte[] kDate = hmacSha256(("AWS4" + secretKey).getBytes(StandardCharsets.UTF_8), datestamp);
        byte[] kRegion = hmacSha256(kDate, region);
        byte[] kService = hmacSha256(kRegion, "s3");
        byte[] kSigning = hmacSha256(kService, "aws4_request");

        String signature = hex(hmacSha256(kSigning, stringToSign));

        return "AWS4-HMAC-SHA256 Credential=" + accessKey + "/" + scope
                + ", SignedHeaders=" + signedHeaders + ", Signature=" + signature;
    }

    // ==================== raw HTTP/1.1 request ====================

    private static class HttpResponse {
        int status;
        Map<String, String> headers = new HashMap<>();
    }

    private static HttpResponse rawRequest(String scheme, String host, int port,
                                            String method, String path,
                                            List<String[]> headers) throws IOException {
        Socket sock = "https".equals(scheme)
                ? SSLSocketFactory.getDefault().createSocket(host, port)
                : new Socket(host, port);
        try {
            sock.setSoTimeout(30000);

            OutputStream os = sock.getOutputStream();
            StringBuilder req = new StringBuilder();
            req.append(method).append(' ').append(path).append(" HTTP/1.1\r\n");
            for (String[] h : headers) {
                req.append(h[0]).append(": ").append(h[1]).append("\r\n");
            }
            req.append("\r\n");
            os.write(req.toString().getBytes(StandardCharsets.US_ASCII));
            os.flush();

            InputStream is = sock.getInputStream();
            BufferedReader reader = new BufferedReader(new InputStreamReader(is, StandardCharsets.US_ASCII));

            String statusLine = reader.readLine();
            if (statusLine == null) {
                throw new IOException("empty response from " + host + ":" + port);
            }
            String[] parts = statusLine.split(" ", 3);
            if (parts.length < 2) {
                throw new IOException("malformed status line: " + statusLine);
            }
            HttpResponse resp = new HttpResponse();
            resp.status = Integer.parseInt(parts[1]);

            String line;
            while ((line = reader.readLine()) != null && !line.isEmpty()) {
                int idx = line.indexOf(':');
                if (idx > 0) {
                    String name = line.substring(0, idx).trim().toLowerCase();
                    String value = line.substring(idx + 1).trim();
                    resp.headers.put(name, value);
                }
            }
            // Response body (if any) is intentionally not read: the actual
            // object data is delivered out-of-band via RDMA.
            return resp;
        } finally {
            sock.close();
        }
    }

    private HttpResponse doRequest(String method, String bucket, String key,
                                    String extraHeaderName, String extraHeaderValue) throws Exception {
        URL u = new URL(endpoint);
        String host = u.getHost();
        int explicitPort = u.getPort();
        int port = explicitPort == -1 ? u.getDefaultPort() : explicitPort;
        String hostHeader = (explicitPort == -1 || explicitPort == u.getDefaultPort())
                ? host : host + ":" + explicitPort;

        String canonicalUri = (key != null && !key.isEmpty()) ? "/" + bucket + "/" + key : "/" + bucket;

        ZonedDateTime now = ZonedDateTime.now(ZoneOffset.UTC);
        String amzdate = now.format(AMZ_DATE_FMT);
        String datestamp = now.format(DATE_STAMP_FMT);

        String authorization = signRequest(method, canonicalUri, hostHeader, amzdate, datestamp);

        List<String[]> headers = new ArrayList<>();
        headers.add(new String[]{"Host", hostHeader});
        headers.add(new String[]{"x-amz-date", amzdate});
        headers.add(new String[]{"Authorization", authorization});
        if ("PUT".equals(method)) {
            headers.add(new String[]{"Content-Length", "0"});
        }
        if (extraHeaderName != null && extraHeaderValue != null) {
            headers.add(new String[]{extraHeaderName, extraHeaderValue});
        }

        return rawRequest(u.getProtocol(), host, port, method, canonicalUri, headers);
    }

    // ==================== APIs ====================

    /** HTTP HEAD on bucket/key, returns Content-Length. */
    public long headObject(String bucket, String key) throws Exception {
        HttpResponse resp = doRequest("HEAD", bucket, key, null, null);
        if (resp.status != 200) {
            throw new RuntimeException("HEAD failed: HTTP " + resp.status);
        }
        String len = resp.headers.get("content-length");
        if (len == null) {
            throw new RuntimeException("HEAD response missing Content-Length");
        }
        return Long.parseLong(len);
    }

    /** HTTP GET on bucket/key. The actual object data arrives via RDMA. */
    public void getObject(String bucket, String key, String extraHeaderName, String extraHeaderValue)
            throws Exception {
        HttpResponse resp = doRequest("GET", bucket, key, extraHeaderName, extraHeaderValue);
        if (resp.status != 200) {
            throw new RuntimeException("GetObject failed: HTTP " + resp.status);
        }
    }

    /** HTTP PUT on bucket/key with an empty body (Content-Length: 0). */
    public void putObject(String bucket, String key, String extraHeaderName, String extraHeaderValue)
            throws Exception {
        HttpResponse resp = doRequest("PUT", bucket, key, extraHeaderName, extraHeaderValue);
        if (resp.status != 200 && resp.status != 201) {
            throw new RuntimeException("PutObject failed: HTTP " + resp.status);
        }
    }
}
