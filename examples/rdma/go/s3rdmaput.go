//go:build linux

package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
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
	"github.com/gorilla/websocket"
)

const (
	rdmaChunkSize = 2 * 1024 * 1024 // 2 MB
	flagDev       = "rxe0"          // default device
	flagPort      = 1               // default port
	flagGIDIdx    = 0               // default GID index
)

// headerTransport adds custom header to requests
type headerTransport struct {
	base      http.RoundTripper
	requestID string
}

func (t *headerTransport) RoundTrip(req *http.Request) (*http.Response, error) {
	req.Header.Set("X-RDMA-Request-ID", t.requestID)
	// For RDMA, we send Content-Length: 0, actual size is in token
	req.ContentLength = 0
	return t.base.RoundTrip(req)
}

func main() {
	// Command line arguments
	bucket := flag.String("bucket", "", "Name of the bucket")
	objectKey := flag.String("object", "", "Name of the object")
	filePath := flag.String("file", "", "Path to the file to upload")
	endpoint := flag.String("endpoint", "http://127.0.0.1:8901", "S3 endpoint URL")
	rdmaDev := flag.String("rdma-dev", "rxe0", "RDMA device name")
	user := flag.String("ak", "admin-ak", "S3 access key")
	pass := flag.String("sk", "admin-sk", "S3 secret key")
	flag.Parse()

	// Validate arguments
	if *bucket == "" || *objectKey == "" || *filePath == "" {
		log.Fatal("Usage: go run s3rdmaput.go -bucket <bucketname> -object <objectname> -file <filepath> [-endpoint <endpoint>] [-rdma-dev <dev>] [-user <access-key>] [-pass <secret-key>]")
	}

	// Read file
	fileData, err := os.ReadFile(*filePath)
	if err != nil {
		log.Fatalf("Failed to read file: %v", err)
	}
	fileSize := len(fileData)

	// Generate a unique request ID
	requestID := fmt.Sprintf("rdma-%d", os.Getpid())

	// Step 1: Establish WebSocket connection and register request ID
	log.Println("Connecting to WebSocket endpoint...")
	wsURL, err := url.Parse(*endpoint)
	if err != nil {
		log.Fatalf("Failed to parse endpoint: %v", err)
	}
	wsURL.Scheme = "ws"
	wsURL.Path = "/rdma-ctrl"

	wsConn, _, err := websocket.DefaultDialer.Dial(wsURL.String(), nil)
	if err != nil {
		log.Fatalf("Failed to connect to WebSocket: %v", err)
	}
	defer wsConn.Close()

	log.Println("Registering request ID...")
	if err := sendJSON(wsConn, MsgTypeRegisterRequest, RegisterRequest{RequestID: requestID}); err != nil {
		log.Fatalf("Failed to send register request: %v", err)
	}

	var regResp RegisterResponse
	if err := recvJSON(wsConn, MsgTypeRegisterResponse, &regResp); err != nil {
		log.Fatalf("Failed to receive register response: %v", err)
	}
	if !regResp.Success {
		log.Fatalf("Registration failed: %s", regResp.Error)
	}
	log.Println("Registration successful")

	// Step 2: Initialize RDMA resources
	log.Println("Initializing RDMA resources...")
	ctx, err := OpenDevice(*rdmaDev)
	if err != nil {
		log.Fatalf("Failed to open RDMA device: %v", err)
	}
	defer ctx.Close()

	pd, err := ctx.AllocPD()
	if err != nil {
		log.Fatalf("Failed to alloc PD: %v", err)
	}
	defer pd.Dealloc()

	cq, err := ctx.CreateCQ(2)
	if err != nil {
		log.Fatalf("Failed to create CQ: %v", err)
	}
	defer cq.Destroy()

	qp, err := pd.CreateRC(cq, 1, 1, 0)
	if err != nil {
		log.Fatalf("Failed to create QP: %v", err)
	}
	defer qp.Destroy()

	if err := qp.ToInit(flagPort); err != nil {
		log.Fatalf("Failed to set QP to INIT: %v", err)
	}

	// Get our GID and MTU
	gid, err := ctx.QueryGID(flagPort, flagGIDIdx)
	if err != nil {
		log.Fatalf("Failed to query GID: %v", err)
	}
	mtuEnum, err := ctx.QueryMTUEnum(flagPort)
	if err != nil {
		log.Fatalf("Failed to query MTU: %v", err)
	}

	// Step 3: Send our QP info
	log.Println("Sending QP info...")
	clientQPInfo := QPInfo{
		QPN:     qp.Num,
		LID:     0,
		GID:     gid,
		GIDIdx:  flagGIDIdx,
		MTUEnum: int32(mtuEnum),
	}
	if err := sendJSON(wsConn, MsgTypeQPInfoClient, clientQPInfo); err != nil {
		log.Fatalf("Failed to send QP info: %v", err)
	}

	// Step 4: Receive server QP info
	log.Println("Receiving server QP info...")
	var serverQPInfo QPInfo
	if err := recvJSON(wsConn, MsgTypeQPInfoServer, &serverQPInfo); err != nil {
		log.Fatalf("Failed to receive server QP info: %v", err)
	}

	// Step 5: Transition QP to RTR then RTS
	log.Println("Transitioning QP to RTR...")
	if err := qp.ToRTR(serverQPInfo.QPN, serverQPInfo.LID, serverQPInfo.GID,
		flagPort, serverQPInfo.GIDIdx, int(serverQPInfo.MTUEnum)); err != nil {
		log.Fatalf("Failed to set QP to RTR: %v", err)
	}
	log.Println("Transitioning QP to RTS...")
	if err := qp.ToRTS(); err != nil {
		log.Fatalf("Failed to set QP to RTS: %v", err)
	}

	// Step 6: Register memory region
	log.Printf("Registering memory region (size = %d bytes)...", fileSize)
	buf, err := syscall.Mmap(-1, 0, fileSize,
		syscall.PROT_READ|syscall.PROT_WRITE,
		syscall.MAP_PRIVATE|syscall.MAP_ANON)
	if err != nil {
		log.Fatalf("Failed to mmap: %v", err)
	}
	defer syscall.Munmap(buf)

	copy(buf, fileData)

	mr, err := pd.RegMRSrc(unsafe.Pointer(&buf[0]), uint64(fileSize))
	if err != nil {
		log.Fatalf("Failed to register MR: %v", err)
	}
	defer mr.Dereg()

	// Step 7: Send Token
	log.Println("Sending token...")
	token := Token{
		Addr:   uint64(uintptr(unsafe.Pointer(&buf[0]))),
		Rkey:   mr.Rkey,
		Start:  0,
		Length: uint64(fileSize),
	}
	if err := sendJSON(wsConn, MsgTypeToken, token); err != nil {
		log.Fatalf("Failed to send token: %v", err)
	}

	// Token sent, server has everything it needs - close WebSocket now
	log.Println("Token sent, closing WebSocket connection early")
	err = wsConn.WriteControl(websocket.CloseMessage,
		websocket.FormatCloseMessage(websocket.CloseNormalClosure, "Token sent, closing early"),
		time.Now().Add(time.Second))
	if err != nil {
		log.Printf("Warning: Could not send close frame: %v", err)
	}
	wsConn.Close()

	// Step 8: Send S3 PutObject request (body is empty, Content-Length: 0)
	log.Println("Sending S3 PutObject request...")
	cfg := aws.Config{
		Region:      "us-east-1",
		Credentials: credentials.NewStaticCredentialsProvider(*user, *pass, ""),
		EndpointResolver: aws.EndpointResolverFunc(func(service, region string) (aws.Endpoint, error) {
			return aws.Endpoint{
				URL:               *endpoint,
				SigningRegion:     "us-east-1",
				HostnameImmutable: true,
			}, nil
		}),
	}

	// Use custom http client to add header
	httpClient := &http.Client{
		Transport: &headerTransport{
			base:      http.DefaultTransport,
			requestID: requestID,
		},
	}

	customClient := s3.NewFromConfig(cfg, func(o *s3.Options) {
		o.UsePathStyle = true
		o.HTTPClient = httpClient
	})

	input := &s3.PutObjectInput{
		Bucket: bucket,
		Key:    objectKey,
		Body:   bytes.NewReader([]byte{}),
	}

	_, err = customClient.PutObject(context.Background(), input)
	if err != nil {
		log.Fatalf("PutObject error: %v", err)
	}

	log.Printf("Successfully uploaded %s to s3://%s/%s via RDMA (HTTP 200 OK)\n", *filePath, *bucket, *objectKey)
}
