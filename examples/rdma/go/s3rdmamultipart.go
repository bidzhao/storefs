//go:build linux

package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"syscall"
	"time"
	"unsafe"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/credentials"
	"github.com/aws/aws-sdk-go-v2/service/s3"
	"github.com/aws/aws-sdk-go-v2/service/s3/types"
	"github.com/gorilla/websocket"
)

const (
	multipartFlagPort   = 1
	multipartFlagGIDIdx = 0
)

type rdmaPartHeaderTransport struct {
	base      http.RoundTripper
	requestID string
}

func (t *rdmaPartHeaderTransport) RoundTrip(req *http.Request) (*http.Response, error) {
	req.Header.Set("X-RDMA-Request-ID", t.requestID)
	// For RDMA UploadPart, the HTTP body is empty. The actual part size is in the RDMA token.
	req.ContentLength = 0
	return t.base.RoundTrip(req)
}

func main() {
	bucket := flag.String("bucket", "", "Name of the bucket")
	objectKey := flag.String("object", "", "Name of the object")
	filePath := flag.String("file", "", "Path to the file to upload")
	partSize := flag.Int64("partsize", 5*1024*1024, "Size of each multipart part in bytes")
	action := flag.String("action", "all", "Action to perform: all, create, upload, list, list-uploads, complete, abort")
	uploadID := flag.String("uploadid", "", "Upload ID for multipart operations")
	partNumber := flag.Int("part", 0, "Part number for upload action; 0 uploads all parts")
	prefix := flag.String("prefix", "", "Prefix for filtering results (for list-uploads action)")
	delimiter := flag.String("delimiter", "", "Delimiter for grouping keys (for list-uploads action)")
	maxUploads := flag.Int64("max-uploads", 1000, "Maximum number of uploads to return (for list-uploads action)")
	keyMarker := flag.String("key-marker", "", "Key marker for pagination (for list-uploads action)")
	uploadIDMarker := flag.String("upload-id-marker", "", "Upload ID marker for pagination (for list-uploads action)")
	endpoint := flag.String("endpoint", "http://127.0.0.1:8901", "S3 endpoint URL")
	rdmaDev := flag.String("rdma-dev", "rxe0", "RDMA device name")
	ak := flag.String("ak", "admin-ak", "S3 access key")
	sk := flag.String("sk", "admin-sk", "S3 secret key")
	flag.Parse()

	if *bucket == "" {
		log.Fatal("Usage: go run s3rdmamultipart.go rdmalib.go -bucket <bucketname> -action <action> [options]")
	}
	if *action != "list-uploads" && *objectKey == "" {
		log.Fatal("object is required for this action")
	}
	if *partSize <= 0 {
		log.Fatal("partsize must be greater than 0")
	}

	ctx := context.Background()
	cfg := newS3Config(*endpoint, *ak, *sk)
	client := newS3Client(cfg)

	switch *action {
	case "all":
		if *filePath == "" {
			log.Fatal("Usage: -action all requires -file")
		}
		fileInfo := mustStatUploadFile(*filePath)
		if fileInfo.Size() == 0 {
			log.Fatal("multipart RDMA upload does not support empty files")
		}
		newUploadID := createMultipartUpload(ctx, client, *bucket, *objectKey)
		completedParts, err := uploadFilePartsRDMA(ctx, cfg, *endpoint, *rdmaDev, *bucket, *objectKey, *filePath, newUploadID, fileInfo.Size(), *partSize, 0)
		if err != nil {
			log.Printf("Upload failed, aborting multipart upload: %v", err)
			abortMultipartUpload(ctx, client, *bucket, *objectKey, newUploadID)
			log.Fatal(err)
		}
		completeMultipartUpload(ctx, client, *bucket, *objectKey, newUploadID, completedParts)
		log.Printf("Successfully uploaded %s to s3://%s/%s via RDMA multipart", *filePath, *bucket, *objectKey)

	case "create":
		createMultipartUpload(ctx, client, *bucket, *objectKey)

	case "upload":
		if *filePath == "" || *uploadID == "" {
			log.Fatal("Usage: -action upload requires -file and -uploadid; use -part <n> for one part or omit/0 for all parts")
		}
		fileInfo := mustStatUploadFile(*filePath)
		if fileInfo.Size() == 0 {
			log.Fatal("multipart RDMA upload does not support empty files")
		}
		completedParts, err := uploadFilePartsRDMA(ctx, cfg, *endpoint, *rdmaDev, *bucket, *objectKey, *filePath, *uploadID, fileInfo.Size(), *partSize, *partNumber)
		if err != nil {
			log.Fatal(err)
		}
		for _, part := range completedParts {
			fmt.Printf("Part %d ETag: %s\n", aws.ToInt32(part.PartNumber), aws.ToString(part.ETag))
		}

	case "list":
		if *uploadID == "" {
			log.Fatal("Usage: -action list requires -uploadid")
		}
		listParts(ctx, client, *bucket, *objectKey, *uploadID)

	case "list-uploads":
		listMultipartUploads(ctx, client, *bucket, *prefix, *delimiter, *maxUploads, *keyMarker, *uploadIDMarker)

	case "complete":
		if *uploadID == "" {
			log.Fatal("Usage: -action complete requires -uploadid")
		}
		parts, err := getCompletedParts(ctx, client, *bucket, *objectKey, *uploadID)
		if err != nil {
			log.Fatal(err)
		}
		completeMultipartUpload(ctx, client, *bucket, *objectKey, *uploadID, parts)

	case "abort":
		if *uploadID == "" {
			log.Fatal("Usage: -action abort requires -uploadid")
		}
		abortMultipartUpload(ctx, client, *bucket, *objectKey, *uploadID)

	default:
		log.Fatalf("Unknown action: %s. Valid actions are: all, create, upload, list, list-uploads, complete, abort", *action)
	}
}

func newS3Config(endpoint, ak, sk string) aws.Config {
	return aws.Config{
		Region:      "us-east-1",
		Credentials: credentials.NewStaticCredentialsProvider(ak, sk, ""),
		EndpointResolver: aws.EndpointResolverFunc(func(service, region string) (aws.Endpoint, error) {
			return aws.Endpoint{
				URL:               endpoint,
				SigningRegion:     "us-east-1",
				HostnameImmutable: true,
			}, nil
		}),
	}
}

func newS3Client(cfg aws.Config) *s3.Client {
	return s3.NewFromConfig(cfg, func(o *s3.Options) {
		o.UsePathStyle = true
	})
}

func mustStatUploadFile(filePath string) os.FileInfo {
	fileInfo, err := os.Stat(filePath)
	if err != nil {
		log.Fatalf("Failed to stat file: %v", err)
	}
	return fileInfo
}

func createMultipartUpload(ctx context.Context, client *s3.Client, bucket, objectKey string) string {
	resp, err := client.CreateMultipartUpload(ctx, &s3.CreateMultipartUploadInput{
		Bucket: aws.String(bucket),
		Key:    aws.String(objectKey),
	})
	if err != nil {
		log.Fatalf("CreateMultipartUpload error: %v", err)
	}
	uploadID := aws.ToString(resp.UploadId)
	fmt.Printf("Successfully created multipart upload\n")
	fmt.Printf("Upload ID: %s\n", uploadID)
	return uploadID
}

func uploadFilePartsRDMA(ctx context.Context, cfg aws.Config, endpoint, rdmaDev, bucket, objectKey, filePath, uploadID string, fileSize, partSize int64, onlyPart int) ([]types.CompletedPart, error) {
	file, err := os.Open(filePath)
	if err != nil {
		return nil, fmt.Errorf("open file: %w", err)
	}
	defer file.Close()

	numParts := int((fileSize + partSize - 1) / partSize)
	if onlyPart < 0 || onlyPart > numParts {
		return nil, fmt.Errorf("part must be between 1 and %d, or 0 for all parts", numParts)
	}

	startPart := 1
	endPart := numParts
	if onlyPart > 0 {
		startPart = onlyPart
		endPart = onlyPart
	}

	completedParts := make([]types.CompletedPart, 0, endPart-startPart+1)
	for partNumber := startPart; partNumber <= endPart; partNumber++ {
		offset := int64(partNumber-1) * partSize
		currentPartSize := partSize
		if remaining := fileSize - offset; remaining < currentPartSize {
			currentPartSize = remaining
		}

		etag, err := uploadPartRDMA(ctx, cfg, endpoint, rdmaDev, file, bucket, objectKey, uploadID, partNumber, offset, currentPartSize)
		if err != nil {
			return nil, fmt.Errorf("upload part %d: %w", partNumber, err)
		}

		pn := int32(partNumber)
		completedParts = append(completedParts, types.CompletedPart{
			ETag:       aws.String(etag),
			PartNumber: &pn,
		})
		log.Printf("Uploaded part %d/%d, size=%d, etag=%s", partNumber, numParts, currentPartSize, etag)
	}

	return completedParts, nil
}

func uploadPartRDMA(ctx context.Context, cfg aws.Config, endpoint, rdmaDev string, file *os.File, bucket, objectKey, uploadID string, partNumber int, offset, partSize int64) (string, error) {
	requestID := fmt.Sprintf("rdma-multipart-%d-part-%d-%d", os.Getpid(), partNumber, time.Now().UnixNano())
	buf, err := mmapPart(file, offset, partSize)
	if err != nil {
		return "", err
	}
	defer syscall.Munmap(buf)

	wsConn, err := connectRDMAControl(endpoint, requestID)
	if err != nil {
		return "", err
	}
	defer wsConn.Close()

	ctxRDMA, err := OpenDevice(rdmaDev)
	if err != nil {
		return "", fmt.Errorf("open RDMA device: %w", err)
	}
	defer ctxRDMA.Close()

	pd, err := ctxRDMA.AllocPD()
	if err != nil {
		return "", fmt.Errorf("alloc PD: %w", err)
	}
	defer pd.Dealloc()

	cq, err := ctxRDMA.CreateCQ(2)
	if err != nil {
		return "", fmt.Errorf("create CQ: %w", err)
	}
	defer cq.Destroy()

	qp, err := pd.CreateRC(cq, 1, 1, 0)
	if err != nil {
		return "", fmt.Errorf("create QP: %w", err)
	}
	defer qp.Destroy()

	if err := qp.ToInit(multipartFlagPort); err != nil {
		return "", fmt.Errorf("set QP INIT: %w", err)
	}

	gid, err := ctxRDMA.QueryGID(multipartFlagPort, multipartFlagGIDIdx)
	if err != nil {
		return "", fmt.Errorf("query GID: %w", err)
	}
	mtuEnum, err := ctxRDMA.QueryMTUEnum(multipartFlagPort)
	if err != nil {
		return "", fmt.Errorf("query MTU: %w", err)
	}

	clientQPInfo := QPInfo{
		QPN:     qp.Num,
		LID:     0,
		GID:     gid,
		GIDIdx:  multipartFlagGIDIdx,
		MTUEnum: int32(mtuEnum),
	}
	if err := sendJSON(wsConn, MsgTypeQPInfoClient, clientQPInfo); err != nil {
		return "", fmt.Errorf("send QP info: %w", err)
	}

	var serverQPInfo QPInfo
	if err := recvJSON(wsConn, MsgTypeQPInfoServer, &serverQPInfo); err != nil {
		return "", fmt.Errorf("receive server QP info: %w", err)
	}

	if err := qp.ToRTR(serverQPInfo.QPN, serverQPInfo.LID, serverQPInfo.GID, multipartFlagPort, serverQPInfo.GIDIdx, int(serverQPInfo.MTUEnum)); err != nil {
		return "", fmt.Errorf("set QP RTR: %w", err)
	}
	if err := qp.ToRTS(); err != nil {
		return "", fmt.Errorf("set QP RTS: %w", err)
	}

	mr, err := pd.RegMRSrc(unsafe.Pointer(&buf[0]), uint64(partSize))
	if err != nil {
		return "", fmt.Errorf("register source MR: %w", err)
	}
	defer mr.Dereg()

	token := Token{
		Addr:   uint64(uintptr(unsafe.Pointer(&buf[0]))),
		Rkey:   mr.Rkey,
		Start:  0,
		Length: uint64(partSize),
	}
	if err := sendJSON(wsConn, MsgTypeToken, token); err != nil {
		return "", fmt.Errorf("send token: %w", err)
	}

	if err := closeControlAfterToken(wsConn); err != nil {
		log.Printf("Warning: could not close RDMA control connection cleanly: %v", err)
	}

	rdmaClient := s3.NewFromConfig(cfg, func(o *s3.Options) {
		o.UsePathStyle = true
		o.HTTPClient = &http.Client{
			Transport: &rdmaPartHeaderTransport{
				base:      http.DefaultTransport,
				requestID: requestID,
			},
		}
	})

	pn := int32(partNumber)
	resp, err := rdmaClient.UploadPart(ctx, &s3.UploadPartInput{
		Bucket:        aws.String(bucket),
		Key:           aws.String(objectKey),
		UploadId:      aws.String(uploadID),
		PartNumber:    &pn,
		Body:          bytes.NewReader(nil),
		ContentLength: aws.Int64(0),
	})
	if err != nil {
		return "", fmt.Errorf("UploadPart request: %w", err)
	}

	return aws.ToString(resp.ETag), nil
}

func listParts(ctx context.Context, client *s3.Client, bucket, objectKey, uploadID string) {
	resp, err := client.ListParts(ctx, &s3.ListPartsInput{
		Bucket:   aws.String(bucket),
		Key:      aws.String(objectKey),
		UploadId: aws.String(uploadID),
	})
	if err != nil {
		log.Fatalf("ListParts error: %v", err)
	}

	fmt.Printf("Parts uploaded for upload ID %s:\n", aws.ToString(resp.UploadId))
	for _, part := range resp.Parts {
		fmt.Printf("Part %d: ETag=%s, Size=%d bytes\n", aws.ToInt32(part.PartNumber), aws.ToString(part.ETag), aws.ToInt64(part.Size))
	}
}

func getCompletedParts(ctx context.Context, client *s3.Client, bucket, objectKey, uploadID string) ([]types.CompletedPart, error) {
	resp, err := client.ListParts(ctx, &s3.ListPartsInput{
		Bucket:   aws.String(bucket),
		Key:      aws.String(objectKey),
		UploadId: aws.String(uploadID),
	})
	if err != nil {
		return nil, fmt.Errorf("ListParts error: %w", err)
	}
	if len(resp.Parts) == 0 {
		return nil, fmt.Errorf("no uploaded parts found for uploadId %s", uploadID)
	}

	parts := make([]types.CompletedPart, 0, len(resp.Parts))
	for _, part := range resp.Parts {
		partNumber := aws.ToInt32(part.PartNumber)
		parts = append(parts, types.CompletedPart{
			ETag:       aws.String(aws.ToString(part.ETag)),
			PartNumber: aws.Int32(partNumber),
		})
	}
	return parts, nil
}

func listMultipartUploads(ctx context.Context, client *s3.Client, bucket, prefix, delimiter string, maxUploads int64, keyMarker, uploadIDMarker string) {
	maxUploads32 := int32(maxUploads)
	resp, err := client.ListMultipartUploads(ctx, &s3.ListMultipartUploadsInput{
		Bucket:         aws.String(bucket),
		Prefix:         aws.String(prefix),
		Delimiter:      aws.String(delimiter),
		MaxUploads:     &maxUploads32,
		KeyMarker:      aws.String(keyMarker),
		UploadIdMarker: aws.String(uploadIDMarker),
	})
	if err != nil {
		log.Fatalf("ListMultipartUploads error: %v", err)
	}

	fmt.Printf("Multipart uploads for bucket: %s\n", aws.ToString(resp.Bucket))
	if resp.Prefix != nil {
		fmt.Printf("Prefix: %s\n", aws.ToString(resp.Prefix))
	}
	if resp.Delimiter != nil {
		fmt.Printf("Delimiter: %s\n", aws.ToString(resp.Delimiter))
	}
	fmt.Printf("MaxUploads: %d\n", aws.ToInt32(resp.MaxUploads))
	fmt.Printf("IsTruncated: %v\n", resp.IsTruncated)
	if resp.KeyMarker != nil {
		fmt.Printf("KeyMarker: %s\n", aws.ToString(resp.KeyMarker))
	}
	if resp.UploadIdMarker != nil {
		fmt.Printf("UploadIdMarker: %s\n", aws.ToString(resp.UploadIdMarker))
	}
	if aws.ToBool(resp.IsTruncated) {
		if resp.NextKeyMarker != nil {
			fmt.Printf("NextKeyMarker: %s\n", aws.ToString(resp.NextKeyMarker))
		}
		if resp.NextUploadIdMarker != nil {
			fmt.Printf("NextUploadIdMarker: %s\n", aws.ToString(resp.NextUploadIdMarker))
		}
	}
	fmt.Printf("\nNumber of uploads: %d\n", len(resp.Uploads))

	for i, upload := range resp.Uploads {
		fmt.Printf("\nUpload %d:\n", i+1)
		fmt.Printf("  Key: %s\n", aws.ToString(upload.Key))
		fmt.Printf("  UploadId: %s\n", aws.ToString(upload.UploadId))
		fmt.Printf("  StorageClass: %s\n", string(upload.StorageClass))
		if upload.Initiated != nil {
			fmt.Printf("  Initiated: %v\n", upload.Initiated.Format(time.RFC3339))
		}
	}

	if len(resp.CommonPrefixes) > 0 {
		fmt.Printf("\nNumber of common prefixes: %d\n", len(resp.CommonPrefixes))
		for i, prefix := range resp.CommonPrefixes {
			fmt.Printf("\nCommon Prefix %d:\n", i+1)
			fmt.Printf("  Prefix: %s\n", aws.ToString(prefix.Prefix))
		}
	}
}

func completeMultipartUpload(ctx context.Context, client *s3.Client, bucket, objectKey, uploadID string, parts []types.CompletedPart) {
	resp, err := client.CompleteMultipartUpload(ctx, &s3.CompleteMultipartUploadInput{
		Bucket:   aws.String(bucket),
		Key:      aws.String(objectKey),
		UploadId: aws.String(uploadID),
		MultipartUpload: &types.CompletedMultipartUpload{
			Parts: parts,
		},
	})
	if err != nil {
		log.Fatalf("CompleteMultipartUpload error: %v", err)
	}

	fmt.Printf("Successfully completed multipart upload\n")
	fmt.Printf("Location: %s\n", aws.ToString(resp.Location))
	fmt.Printf("ETag: %s\n", aws.ToString(resp.ETag))
	fmt.Printf("Bucket: %s\n", aws.ToString(resp.Bucket))
	fmt.Printf("Key: %s\n", aws.ToString(resp.Key))
}

func abortMultipartUpload(ctx context.Context, client *s3.Client, bucket, objectKey, uploadID string) {
	_, err := client.AbortMultipartUpload(ctx, &s3.AbortMultipartUploadInput{
		Bucket:   aws.String(bucket),
		Key:      aws.String(objectKey),
		UploadId: aws.String(uploadID),
	})
	if err != nil {
		log.Fatalf("AbortMultipartUpload error: %v", err)
	}
	fmt.Printf("Successfully aborted multipart upload\n")
}

func mmapPart(file *os.File, offset, partSize int64) ([]byte, error) {
	if partSize <= 0 {
		return nil, fmt.Errorf("part size must be greater than 0")
	}
	if int64(int(partSize)) != partSize {
		return nil, fmt.Errorf("part size %d is too large for mmap length", partSize)
	}

	buf, err := syscall.Mmap(-1, 0, int(partSize), syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_PRIVATE|syscall.MAP_ANON)
	if err != nil {
		return nil, fmt.Errorf("mmap: %w", err)
	}

	n, err := file.ReadAt(buf, offset)
	if err != nil && err != io.EOF {
		syscall.Munmap(buf)
		return nil, fmt.Errorf("read file part: %w", err)
	}
	if int64(n) != partSize {
		syscall.Munmap(buf)
		return nil, fmt.Errorf("read %d bytes, expected %d", n, partSize)
	}

	return buf, nil
}

func connectRDMAControl(endpoint, requestID string) (*websocket.Conn, error) {
	wsURL, err := url.Parse(endpoint)
	if err != nil {
		return nil, fmt.Errorf("parse endpoint: %w", err)
	}
	if wsURL.Scheme == "https" {
		wsURL.Scheme = "wss"
	} else {
		wsURL.Scheme = "ws"
	}
	wsURL.Path = "/rdma-ctrl"
	wsURL.RawQuery = ""

	wsConn, _, err := websocket.DefaultDialer.Dial(wsURL.String(), nil)
	if err != nil {
		return nil, fmt.Errorf("connect WebSocket: %w", err)
	}

	if err := sendJSON(wsConn, MsgTypeRegisterRequest, RegisterRequest{RequestID: requestID}); err != nil {
		wsConn.Close()
		return nil, fmt.Errorf("send register request: %w", err)
	}

	var regResp RegisterResponse
	if err := recvJSON(wsConn, MsgTypeRegisterResponse, &regResp); err != nil {
		wsConn.Close()
		return nil, fmt.Errorf("receive register response: %w", err)
	}
	if !regResp.Success {
		wsConn.Close()
		return nil, fmt.Errorf("registration failed: %s", regResp.Error)
	}

	return wsConn, nil
}

func closeControlAfterToken(wsConn *websocket.Conn) error {
	return wsConn.WriteControl(
		websocket.CloseMessage,
		websocket.FormatCloseMessage(websocket.CloseNormalClosure, "Token sent, closing early"),
		time.Now().Add(time.Second),
	)
}
