package com.example.s3rdma;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import javax.net.ssl.SSLSocketFactory;
import javax.xml.parsers.DocumentBuilderFactory;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.net.URI;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.time.ZoneOffset;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;

/**
 * Multipart upload to S3 via RDMA.
 *
 * Usage:
 *   java -Djna.library.path=native -cp target/s3rdma.jar com.example.s3rdma.S3RdmaMultipart \
 *        -action all -bucket <bucket> -object <key> -file <path>
 */
public class S3RdmaMultipart {
    private static final String UNSIGNED_PAYLOAD = "UNSIGNED-PAYLOAD";
    private static final DateTimeFormatter AMZ_DATE_FMT = DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'");
    private static final DateTimeFormatter DATE_STAMP_FMT = DateTimeFormatter.ofPattern("yyyyMMdd");

    private static class Options {
        String action = "all";
        String bucket;
        String object;
        String file;
        String uploadId;
        String endpoint = "http://127.0.0.1:8901";
        String rdmaDev = "rxe0";
        String ak = "admin-ak";
        String sk = "admin-sk";
        String prefix = "";
        String delimiter = "";
        String keyMarker = "";
        String uploadIdMarker = "";
        long partSize = 5L * 1024 * 1024;
        int part = 0;
        int maxUploads = 1000;
    }

    private static class PartInfo {
        int partNumber;
        String etag;
        long size;
    }

    private static class S3Response {
        int status;
        String body;
    }

    private static void usage() {
        System.err.println("Usage: S3RdmaMultipart -action <all|create|upload|list|list-uploads|complete|abort> -bucket <bucket> [options]\n"
                + "  -object <key>                 Required except list-uploads\n"
                + "  -file <path>                  Required for all/upload\n"
                + "  -uploadid <id>                Required for upload/list/complete/abort\n"
                + "  -part <n>                     Upload one part; 0 means all parts\n"
                + "  -partsize <bytes>             Default 5242880\n"
                + "  -endpoint <url>               Default http://127.0.0.1:8901\n"
                + "  -rdma-dev <dev>               Default rxe0\n"
                + "  -ak <access-key> -sk <secret-key>\n"
                + "  -prefix <prefix> -delimiter <delimiter> -max-uploads <n>\n"
                + "  -key-marker <key> -upload-id-marker <id>");
        System.exit(1);
    }

    private static Options parseArgs(String[] args) {
        Options opts = new Options();
        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "-action": opts.action = args[++i]; break;
                case "-bucket": opts.bucket = args[++i]; break;
                case "-object": opts.object = args[++i]; break;
                case "-file": opts.file = args[++i]; break;
                case "-uploadid": opts.uploadId = args[++i]; break;
                case "-part": opts.part = Integer.parseInt(args[++i]); break;
                case "-partsize": opts.partSize = Long.parseLong(args[++i]); break;
                case "-endpoint": opts.endpoint = args[++i]; break;
                case "-rdma-dev": opts.rdmaDev = args[++i]; break;
                case "-ak": opts.ak = args[++i]; break;
                case "-sk": opts.sk = args[++i]; break;
                case "-prefix": opts.prefix = args[++i]; break;
                case "-delimiter": opts.delimiter = args[++i]; break;
                case "-max-uploads": opts.maxUploads = Integer.parseInt(args[++i]); break;
                case "-key-marker": opts.keyMarker = args[++i]; break;
                case "-upload-id-marker": opts.uploadIdMarker = args[++i]; break;
                default: usage();
            }
        }
        if (opts.bucket == null) usage();
        if (!"list-uploads".equals(opts.action) && opts.object == null) usage();
        if (opts.partSize <= 0) usage();
        return opts;
    }

    private static String wsUrlFromEndpoint(String endpoint) {
        URI u = URI.create(endpoint);
        String scheme = "https".equals(u.getScheme()) ? "wss" : "ws";
        return scheme + "://" + u.getAuthority() + "/rdma-ctrl";
    }

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
        for (byte b : data) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    private static String signRequest(Options opts, String method, String canonicalUri, String canonicalQuery,
                                      String host, String amzdate, String datestamp, Long contentLength) throws Exception {
        String canonicalHeaders;
        String signedHeaders;
        if (contentLength == null) {
            canonicalHeaders = "host:" + host + "\nx-amz-date:" + amzdate + "\n";
            signedHeaders = "host;x-amz-date";
        } else {
            canonicalHeaders = "content-length:" + contentLength + "\nhost:" + host + "\nx-amz-date:" + amzdate + "\n";
            signedHeaders = "content-length;host;x-amz-date";
        }

        String canonicalRequest = method + "\n" + canonicalUri + "\n" + (canonicalQuery == null ? "" : canonicalQuery) + "\n"
                + canonicalHeaders + "\n" + signedHeaders + "\n" + UNSIGNED_PAYLOAD;
        String creqHash = sha256Hex(canonicalRequest);
        String scope = datestamp + "/us-east-1/s3/aws4_request";
        String stringToSign = "AWS4-HMAC-SHA256\n" + amzdate + "\n" + scope + "\n" + creqHash;

        byte[] kDate = hmacSha256(("AWS4" + opts.sk).getBytes(StandardCharsets.UTF_8), datestamp);
        byte[] kRegion = hmacSha256(kDate, "us-east-1");
        byte[] kService = hmacSha256(kRegion, "s3");
        byte[] kSigning = hmacSha256(kService, "aws4_request");
        String signature = hex(hmacSha256(kSigning, stringToSign));

        return "AWS4-HMAC-SHA256 Credential=" + opts.ak + "/" + scope
                + ", SignedHeaders=" + signedHeaders + ", Signature=" + signature;
    }

    private static S3Response request(Options opts, String method, String key, String query, String body,
                                      String extraHeaderName, String extraHeaderValue) throws Exception {
        URL u = new URL(opts.endpoint);
        String host = u.getHost();
        int explicitPort = u.getPort();
        int port = explicitPort == -1 ? u.getDefaultPort() : explicitPort;
        String hostHeader = (explicitPort == -1 || explicitPort == u.getDefaultPort()) ? host : host + ":" + explicitPort;
        String canonicalUri = key != null && !key.isEmpty() ? "/" + opts.bucket + "/" + key : "/" + opts.bucket;
        String path = canonicalUri + (query != null && !query.isEmpty() ? "?" + query : "");
        byte[] bodyBytes = body == null ? null : body.getBytes(StandardCharsets.UTF_8);

        ZonedDateTime now = ZonedDateTime.now(ZoneOffset.UTC);
        String amzdate = now.format(AMZ_DATE_FMT);
        String datestamp = now.format(DATE_STAMP_FMT);
        String authorization = signRequest(opts, method, canonicalUri, query, hostHeader, amzdate, datestamp,
                bodyBytes == null ? null : (long) bodyBytes.length);

        Socket sock = "https".equals(u.getProtocol())
                ? SSLSocketFactory.getDefault().createSocket(host, port)
                : new Socket(host, port);
        try {
            sock.setSoTimeout(30000);
            OutputStream os = sock.getOutputStream();
            StringBuilder req = new StringBuilder();
            req.append(method).append(' ').append(path).append(" HTTP/1.1\r\n");
            req.append("Host: ").append(hostHeader).append("\r\n");
            req.append("x-amz-date: ").append(amzdate).append("\r\n");
            req.append("Authorization: ").append(authorization).append("\r\n");
            if (bodyBytes != null) req.append("Content-Length: ").append(bodyBytes.length).append("\r\n");
            else if ("PUT".equals(method)) req.append("Content-Length: 0\r\n");
            if (extraHeaderName != null) req.append(extraHeaderName).append(": ").append(extraHeaderValue).append("\r\n");
            req.append("\r\n");
            os.write(req.toString().getBytes(StandardCharsets.US_ASCII));
            if (bodyBytes != null && bodyBytes.length > 0) os.write(bodyBytes);
            os.flush();

            InputStream is = sock.getInputStream();
            ByteArrayOutputStream raw = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) >= 0) raw.write(buf, 0, n);
            String response = raw.toString(StandardCharsets.UTF_8.name());
            int headerEnd = response.indexOf("\r\n\r\n");
            String header = headerEnd >= 0 ? response.substring(0, headerEnd) : response;
            String respBody = headerEnd >= 0 ? response.substring(headerEnd + 4) : "";
            String statusLine = header.split("\r\n", 2)[0];
            String[] parts = statusLine.split(" ", 3);
            S3Response r = new S3Response();
            r.status = Integer.parseInt(parts[1]);
            r.body = respBody;
            if (r.status < 200 || r.status >= 300) {
                throw new RuntimeException(method + " failed: HTTP " + r.status + " body=" + r.body);
            }
            return r;
        } finally {
            sock.close();
        }
    }

    private static String tagText(Element elem, String name) {
        NodeList nodes = elem.getElementsByTagName(name);
        if (nodes.getLength() == 0 || nodes.item(0).getTextContent() == null) return "";
        return nodes.item(0).getTextContent();
    }

    private static Document parseXml(String xml) throws Exception {
        return DocumentBuilderFactory.newInstance().newDocumentBuilder()
                .parse(new java.io.ByteArrayInputStream(xml.getBytes(StandardCharsets.UTF_8)));
    }

    private static String createMultipart(Options opts) throws Exception {
        S3Response resp = request(opts, "POST", opts.object, "uploads=", null, null, null);
        String uploadId = tagText(parseXml(resp.body).getDocumentElement(), "UploadId");
        if (uploadId.isEmpty()) throw new RuntimeException("CreateMultipartUpload response missing UploadId: " + resp.body);
        System.out.println("Successfully created multipart upload");
        System.out.println("Upload ID: " + uploadId);
        return uploadId;
    }

    private static PartInfo uploadPartRdma(Options opts, String uploadId, int partNumber, byte[] data) throws Exception {
        String requestId = "rdma-multipart-" + ProcessHandle.current().pid() + "-part-" + partNumber + "-" + System.nanoTime();
        RdmaSession sess = RdmaSession.sendSetup(wsUrlFromEndpoint(opts.endpoint), opts.rdmaDev,
                RdmaSession.RDMA_PORT, RdmaSession.RDMA_GID_IDX, requestId, data);
        try {
            request(opts, "PUT", opts.object, "partNumber=" + partNumber + "&uploadId=" + uploadId,
                    null, "X-RDMA-Request-ID", requestId);
        } finally {
            sess.destroy();
        }
        PartInfo part = new PartInfo();
        part.partNumber = partNumber;
        part.etag = "\"part-" + partNumber + "\"";
        part.size = data.length;
        System.err.println("Uploaded part " + partNumber + ", size=" + data.length + ", ETag=" + part.etag);
        return part;
    }

    private static List<PartInfo> uploadParts(Options opts, String uploadId) throws Exception {
        Path path = Paths.get(opts.file);
        long size = Files.size(path);
        if (size <= 0) throw new RuntimeException("multipart RDMA upload does not support empty files");
        int numParts = (int) ((size + opts.partSize - 1) / opts.partSize);
        if (opts.part < 0 || opts.part > numParts) {
            throw new RuntimeException("part must be between 1 and " + numParts + ", or 0 for all parts");
        }
        int start = opts.part > 0 ? opts.part : 1;
        int end = opts.part > 0 ? opts.part : numParts;
        byte[] all = Files.readAllBytes(path);
        List<PartInfo> parts = new ArrayList<>();
        for (int pn = start; pn <= end; pn++) {
            int offset = (int) ((pn - 1L) * opts.partSize);
            int len = (int) Math.min(opts.partSize, all.length - offset);
            byte[] data = new byte[len];
            System.arraycopy(all, offset, data, 0, len);
            parts.add(uploadPartRdma(opts, uploadId, pn, data));
        }
        return parts;
    }

    private static List<PartInfo> listParts(Options opts, String uploadId) throws Exception {
        S3Response resp = request(opts, "GET", opts.object, "uploadId=" + uploadId, null, null, null);
        Document doc = parseXml(resp.body);
        NodeList nodes = doc.getElementsByTagName("Part");
        List<PartInfo> parts = new ArrayList<>();
        for (int i = 0; i < nodes.getLength(); i++) {
            Element elem = (Element) nodes.item(i);
            PartInfo part = new PartInfo();
            part.partNumber = Integer.parseInt(tagText(elem, "PartNumber"));
            part.etag = tagText(elem, "ETag");
            String size = tagText(elem, "Size");
            part.size = size.isEmpty() ? 0 : Long.parseLong(size);
            parts.add(part);
        }
        return parts;
    }

    private static void completeMultipart(Options opts, String uploadId, List<PartInfo> parts) throws Exception {
        parts.sort(Comparator.comparingInt(p -> p.partNumber));
        StringBuilder body = new StringBuilder("<CompleteMultipartUpload>");
        for (PartInfo p : parts) {
            body.append("<Part><PartNumber>").append(p.partNumber).append("</PartNumber><ETag>")
                    .append(p.etag).append("</ETag></Part>");
        }
        body.append("</CompleteMultipartUpload>");
        S3Response resp = request(opts, "POST", opts.object, "uploadId=" + uploadId, body.toString(), null, null);
        System.out.println(resp.body);
        System.out.println("Successfully completed multipart upload");
    }

    private static void abortMultipart(Options opts, String uploadId) throws Exception {
        request(opts, "DELETE", opts.object, "uploadId=" + uploadId, null, null, null);
        System.out.println("Successfully aborted multipart upload");
    }

    private static void listUploads(Options opts) throws Exception {
        List<String> qs = new ArrayList<>();
        qs.add("uploads=");
        if (!opts.delimiter.isEmpty()) qs.add("delimiter=" + opts.delimiter);
        if (!opts.keyMarker.isEmpty()) qs.add("key-marker=" + opts.keyMarker);
        qs.add("max-uploads=" + opts.maxUploads);
        if (!opts.prefix.isEmpty()) qs.add("prefix=" + opts.prefix);
        if (!opts.uploadIdMarker.isEmpty()) qs.add("upload-id-marker=" + opts.uploadIdMarker);
        System.out.println(request(opts, "GET", null, String.join("&", qs), null, null, null).body);
    }

    public static void main(String[] args) throws Exception {
        Options opts = parseArgs(args);
        if (!"list-uploads".equals(opts.action) && opts.object == null) usage();

        switch (opts.action) {
            case "create":
                createMultipart(opts);
                break;
            case "upload": {
                if (opts.file == null || opts.uploadId == null) usage();
                List<PartInfo> parts = uploadParts(opts, opts.uploadId);
                for (PartInfo p : parts) System.out.println("Part " + p.partNumber + " ETag: " + p.etag);
                break;
            }
            case "list": {
                if (opts.uploadId == null) usage();
                List<PartInfo> parts = listParts(opts, opts.uploadId);
                System.out.println("Parts uploaded for upload ID " + opts.uploadId + ":");
                for (PartInfo p : parts) System.out.println("Part " + p.partNumber + ": ETag=" + p.etag + ", Size=" + p.size + " bytes");
                break;
            }
            case "list-uploads":
                listUploads(opts);
                break;
            case "complete": {
                if (opts.uploadId == null) usage();
                List<PartInfo> parts = listParts(opts, opts.uploadId);
                if (parts.isEmpty()) throw new RuntimeException("no uploaded parts found for uploadId " + opts.uploadId);
                completeMultipart(opts, opts.uploadId, parts);
                break;
            }
            case "abort":
                if (opts.uploadId == null) usage();
                abortMultipart(opts, opts.uploadId);
                break;
            case "all": {
                if (opts.file == null) usage();
                String uploadId = createMultipart(opts);
                try {
                    List<PartInfo> parts = uploadParts(opts, uploadId);
                    completeMultipart(opts, uploadId, parts);
                } catch (Exception e) {
                    abortMultipart(opts, uploadId);
                    throw e;
                }
                break;
            }
            default:
                usage();
        }
    }
}
