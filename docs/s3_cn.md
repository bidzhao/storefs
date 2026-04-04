**[English](s3.md)**

# S3 API 文档

## 概要

StoreFS 实现了 Amazon S3 API 的核心功能，允许用户使用与 AWS S3 兼容的客户端和工具进行对象存储操作。本文档详细描述了已实现的 API 接口、请求格式和响应结构。

## 基础信息

### 协议和端口

- **协议**：HTTP（生产环境建议使用 HTTPS）
- **默认端口**：8901
- **API 端点**：`http://<host>:<port>/`

### 认证方式

StoreFS 使用 AWS Signature Version 4（SigV4）认证方式，需要提供访问密钥（AK）和秘密密钥（SK）。

## 已实现的 API 接口

### 1. 桶操作（Bucket Operations）

#### 1.1 创建桶（CreateBucket）

**URL**：`PUT /<bucket>`

**请求**：
```http
PUT /mybucket HTTP/1.1
Host: 127.0.0.1:8901
Content-Length: 0
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```http
HTTP/1.1 200 OK
Location: /mybucket
Content-Length: 0
```

**错误响应**：
- 400 Bad Request：无效的桶名
- 409 Conflict：桶已存在
- 401 Unauthorized：认证失败

#### 1.2 删除桶（DeleteBucket）

**URL**：`DELETE /<bucket>`

**请求**：
```http
DELETE /mybucket HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```http
HTTP/1.1 204 No Content
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限
- 409 Conflict：桶不为空

#### 1.3 列出桶（ListBuckets）

**URL**：`GET /`

**请求**：
```http
GET / HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ListAllMyBucketsResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Owner>
    <ID>1</ID>
    <DisplayName>default</DisplayName>
  </Owner>
  <Buckets>
    <Bucket>
      <Name>mybucket</Name>
      <CreationDate>2023-01-01T12:00:00.000Z</CreationDate>
    </Bucket>
  </Buckets>
</ListAllMyBucketsResult>
```

**错误响应**：
- 401 Unauthorized：认证失败

### 2. 对象操作（Object Operations）

#### 2.1 列出桶中的对象（ListObjects V1）

**URL**：`GET /<bucket>`

**请求参数**：
- `prefix`（可选）：前缀过滤
- `marker`（可选）：分页标记
- `max-keys`（可选）：最大返回数量（默认1000）
- `delimiter`（可选）：分隔符用于分组

**请求**：
```http
GET /mybucket?prefix=photos/&max-keys=100 HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ListBucketResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Name>mybucket</Name>
  <Prefix>photos/</Prefix>
  <Marker></Marker>
  <MaxKeys>100</MaxKeys>
  <IsTruncated>false</IsTruncated>
  <Contents>
    <Key>photos/2023/01.jpg</Key>
    <LastModified>2023-01-01T12:00:00Z</LastModified>
    <ETag>"abc123"</ETag>
    <Size>1024</Size>
    <StorageClass>STANDARD</StorageClass>
    <Owner>
      <ID>1</ID>
      <DisplayName>default</DisplayName>
    </Owner>
  </Contents>
</ListBucketResult>
```

#### 2.2 列出桶中的对象（ListObjects V2）

**URL**：`GET /<bucket>?list-type=2`

**请求参数**：
- `prefix`（可选）：前缀过滤
- `continuation-token`（可选）：分页标记
- `max-keys`（可选）：最大返回数量（默认1000）
- `delimiter`（可选）：分隔符用于分组

**请求**：
```http
GET /mybucket?list-type=2&prefix=photos/&max-keys=100 HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ListBucketResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Name>mybucket</Name>
  <Prefix>photos/</Prefix>
  <MaxKeys>100</MaxKeys>
  <IsTruncated>false</IsTruncated>
  <KeyCount>1</KeyCount>
  <Contents>
    <Key>photos/2023/01.jpg</Key>
    <LastModified>2023-01-01T12:00:00Z</LastModified>
    <ETag>"abc123"</ETag>
    <Size>1024</Size>
    <StorageClass>STANDARD</StorageClass>
    <Owner>
      <ID>1</ID>
      <DisplayName>default</DisplayName>
    </Owner>
  </Contents>
</ListBucketResult>
```

#### 2.3 上传对象（PutObject）

**URL**：`PUT /<bucket>/<object>`

**请求**：
```http
PUT /mybucket/photos/2023/01.jpg HTTP/1.1
Host: 127.0.0.1:8901
Content-Length: 1024
Content-Type: image/jpeg
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

[object data]
```

**响应**：
```http
HTTP/1.1 200 OK
ETag: "abc123"
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限
- 500 Internal Server Error：内部错误

#### 2.4 下载对象（GetObject）

**URL**：`GET /<bucket>/<object>`

**请求**：
```http
GET /mybucket/photos/2023/01.jpg HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```http
HTTP/1.1 200 OK
Content-Type: image/jpeg
Content-Length: 1024
ETag: "abc123"
Last-Modified: 2023-01-01T12:00:00Z

[object data]
```

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：无权限

#### 2.5 删除对象（DeleteObject）

**URL**：`DELETE /<bucket>/<object>`

**请求**：
```http
DELETE /mybucket/photos/2023/01.jpg HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```http
HTTP/1.1 204 No Content
Content-Length: 0
```

**说明**：无论对象是否存在，都返回成功响应。

## 桶名规范

StoreFS 遵循 AWS S3 桶名规范：

1. 长度必须在 3 到 63 个字符之间
2. 只能包含小写字母、数字、点和连字符
3. 不能以点或连字符开头或结尾
4. 不能包含连续的点或连字符
5. 不能是 IP 地址格式

## 错误响应格式

所有错误响应都采用 XML 格式，示例：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Error>
  <Code>NoSuchBucket</Code>
  <Message>The specified bucket does not exist</Message>
  <BucketName>mybucket</BucketName>
  <RequestId>123456</RequestId>
  <HostId>abc123</HostId>
</Error>
```

## 常用客户端工具

### AWS CLI

```bash
# 配置 CLI
aws configure --profile storefs
AWS Access Key ID: <AK>
AWS Secret Access Key: <SK>
Default region name: us-east-1
Default output format: json

# 使用 CLI 操作
aws s3 --endpoint-url http://127.0.0.1:8901 --profile storefs ls
aws s3 --endpoint-url http://127.0.0.1:8901 --profile storefs mb s3://mybucket
aws s3 --endpoint-url http://127.0.0.1:8901 --profile storefs cp localfile.txt s3://mybucket/
```

### 其他工具

- **s3cmd**：命令行工具
- **MinIO Client**：与 S3 兼容的客户端
- **各种语言的 SDK**：如 boto3（Python）、AWS SDK for Java 等

## 未实现的功能

目前以下 S3 功能尚未实现：

- 桶策略和 ACL
- 对象版本控制
- 多部分上传
- 服务器端加密
- 生命周期管理
- 跨域资源共享（CORS）
- 事件通知

这些功能将在未来的版本中逐步实现。
