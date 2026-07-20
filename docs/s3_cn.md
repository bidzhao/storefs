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

#### 1.4 获取桶版本控制状态（GetBucketVersioning）

**URL**：`GET /<bucket>?versioning`

**请求**：
```http
GET /mybucket?versioning HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<VersioningConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Status>Enabled</Status>
</VersioningConfiguration>
```

**说明**：
- Status 可能的值：`Enabled`（启用）、`Suspended`（暂停）或空（未启用）
- 只有桶所有者或管理员可以访问此API

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限
- 401 Unauthorized：认证失败

#### 1.5 设置桶版本控制状态（PutBucketVersioning）

**URL**：`PUT /<bucket>?versioning`

**请求**：
```http
PUT /mybucket?versioning HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Content-MD5: <md5_base64>
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<VersioningConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Status>Enabled</Status>
</VersioningConfiguration>
```

**说明**：
- Status 可以是 `Enabled`（启用）或 `Suspended`（暂停）
- 版本控制一旦启用就不能完全禁用，只能暂停
- 只有桶所有者或管理员可以访问此API

**响应**：
```http
HTTP/1.1 200 OK
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限
- 400 Bad Request：无效的版本控制状态
- 401 Unauthorized：认证失败

#### 1.6 获取桶对象锁定配置（GetObjectLockConfiguration）

**URL**：`GET /<bucket>?object-lock`

**请求**：
```http
GET /mybucket?object-lock HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（已启用对象锁定）：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ObjectLockConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <ObjectLockEnabled>Enabled</ObjectLockEnabled>
  <Rule>
    <DefaultRetention>
      <Mode>COMPLIANCE</Mode>
      <Days>50</Days>
    </DefaultRetention>
  </Rule>
</ObjectLockConfiguration>
```

**说明**：
- `ObjectLockEnabled`：固定为 `Enabled`
- `Mode`：保留模式，`GOVERNANCE`（治理模式）或 `COMPLIANCE`（合规模式）
- `Days` 或 `Years`：保留期限（互斥）
- 只有桶所有者或管理员可以访问此API

**响应**（未启用对象锁定）：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<Error>
  <Code>ObjectLockConfigurationNotFoundError</Code>
  <Message>Object Lock configuration does not exist for this bucket</Message>
  <BucketName>mybucket</BucketName>
</Error>
```

**错误响应**：
- 404 Not Found：桶不存在或对象锁定未配置
- 403 Forbidden：无权限
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

**可选：Gzip 压缩传输**：
PutObject 支持 gzip 压缩的请求体。设置 `Content-Encoding: gzip` 请求头后，服务端会自动解压数据再存储：
```http
PUT /mybucket/photos/2023/01.jpg HTTP/1.1
Host: 127.0.0.1:8901
Content-Encoding: gzip
Content-Length: 512
Content-Type: image/jpeg
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

[gzip 压缩的对象数据]
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

#### 2.6 多部分上传操作（Multipart Upload）

##### 2.6.1 初始化分块上传（CreateMultipartUpload）

**URL**：`POST /<bucket>/<object>?uploads`

**请求**：
```http
POST /mybucket/largefile.zip?uploads HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/zip
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<InitiateMultipartUploadResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Bucket>mybucket</Bucket>
  <Key>largefile.zip</Key>
  <UploadId>abc123def456</UploadId>
</InitiateMultipartUploadResult>
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限

##### 2.6.2 上传分块（UploadPart）

**URL**：`PUT /<bucket>/<object>?partNumber=<part_number>&uploadId=<upload_id>`

**请求**：
```http
PUT /mybucket/largefile.zip?partNumber=1&uploadId=abc123def456 HTTP/1.1
Host: 127.0.0.1:8901
Content-Length: 5242880
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

[part data]
```

**可选：Gzip 压缩传输**：
UploadPart 同样支持 gzip 压缩的分块数据。设置 `Content-Encoding: gzip` 请求头：
```http
PUT /mybucket/largefile.zip?partNumber=1&uploadId=abc123def456 HTTP/1.1
Host: 127.0.0.1:8901
Content-Encoding: gzip
Content-Length: 2621440
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

[gzip 压缩的分块数据]
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
- 400 Bad Request：无效的分块编号或上传ID

##### 2.6.3 列出已上传的分块（ListParts）

**URL**：`GET /<bucket>/<object>?uploadId=<upload_id>`

**请求**：
```http
GET /mybucket/largefile.zip?uploadId=abc123def456 HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ListPartsResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Bucket>mybucket</Bucket>
  <Key>largefile.zip</Key>
  <UploadId>abc123def456</UploadId>
  <Initiator>
    <ID>1</ID>
    <DisplayName>default</DisplayName>
  </Initiator>
  <Owner>
    <ID>1</ID>
    <DisplayName>default</DisplayName>
  </Owner>
  <StorageClass>STANDARD</StorageClass>
  <PartNumberMarker>0</PartNumberMarker>
  <NextPartNumberMarker>2</NextPartNumberMarker>
  <MaxParts>1000</MaxParts>
  <IsTruncated>false</IsTruncated>
  <Part>
    <PartNumber>1</PartNumber>
    <LastModified>2023-01-01T12:00:00Z</LastModified>
    <ETag>"abc123"</ETag>
    <Size>5242880</Size>
  </Part>
  <Part>
    <PartNumber>2</PartNumber>
    <LastModified>2023-01-01T12:05:00Z</LastModified>
    <ETag>"def456"</ETag>
    <Size>5242880</Size>
  </Part>
</ListPartsResult>
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限
- 400 Bad Request：无效的上传ID

##### 2.6.4 完成分块上传（CompleteMultipartUpload）

**URL**：`POST /<bucket>/<object>?uploadId=<upload_id>`

**请求**：
```http
POST /mybucket/largefile.zip?uploadId=abc123def456 HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: multipart/form-data
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<CompleteMultipartUpload>
  <Part>
    <PartNumber>1</PartNumber>
    <ETag>"abc123"</ETag>
  </Part>
  <Part>
    <PartNumber>2</PartNumber>
    <ETag>"def456"</ETag>
  </Part>
</CompleteMultipartUpload>
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<CompleteMultipartUploadResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Location>http://127.0.0.1:8901/mybucket/largefile.zip</Location>
  <Bucket>mybucket</Bucket>
  <Key>largefile.zip</Key>
  <ETag>"abc123def456"</ETag>
</CompleteMultipartUploadResult>
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限
- 400 Bad Request：无效的上传ID或分块信息

##### 2.6.5 中止分块上传（AbortMultipartUpload）

**URL**：`DELETE /<bucket>/<object>?uploadId=<upload_id>`

**请求**：
```http
DELETE /mybucket/largefile.zip?uploadId=abc123def456 HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```http
HTTP/1.1 204 No Content
Content-Length: 0
```

**说明**：无论上传是否存在，都返回成功响应。

##### 2.6.6 列出分块上传（ListMultipartUploads）

**URL**：`GET /<bucket>?uploads`

**请求**：
```http
GET /mybucket?uploads HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ListMultipartUploadsResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Bucket>mybucket</Bucket>
  <KeyMarker></KeyMarker>
  <UploadIdMarker></UploadIdMarker>
  <NextKeyMarker>largefile.zip</NextKeyMarker>
  <NextUploadIdMarker>abc123def456</NextUploadIdMarker>
  <MaxUploads>1000</MaxUploads>
  <IsTruncated>false</IsTruncated>
  <Upload>
    <Key>largefile.zip</Key>
    <UploadId>abc123def456</UploadId>
    <Initiated>2023-01-01T12:00:00Z</Initiated>
    <StorageClass>STANDARD</StorageClass>
    <Initiator>
      <ID>1</ID>
      <DisplayName>default</DisplayName>
    </Initiator>
    <Owner>
      <ID>1</ID>
      <DisplayName>default</DisplayName>
    </Owner>
  </Upload>
</ListMultipartUploadsResult>
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限

#### 2.7 获取对象保留配置（GetObjectRetention）

**URL**：`GET /<bucket>/<object>?retention[&versionId=<version_id>]`

**请求参数**：
- `versionId`（可选）：特定对象版本的ID

**请求**：
```http
GET /mybucket/protectedfile.txt?retention HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（已配置保留）：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<Retention xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Mode>COMPLIANCE</Mode>
  <RetainUntilDate>2023-12-31T12:00:00.000Z</RetainUntilDate>
</Retention>
```

**说明**：
- `Mode`：保留模式，`GOVERNANCE`（治理模式）或 `COMPLIANCE`（合规模式）
- `RetainUntilDate`：保留截止日期，ISO 8601 格式
- 只有桶所有者或管理员可以访问此API
- 如果指定了 `versionId`，则获取该特定版本的保留配置

**响应**（未配置保留）：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<Error>
  <Code>NoSuchObjectRetention</Code>
  <Message>The specified object does not have a retention configuration</Message>
  <BucketName>mybucket</BucketName>
  <Key>protectedfile.txt</Key>
</Error>
```

**错误响应**：
- 404 Not Found：桶不存在、对象不存在或未配置保留
- 403 Forbidden：无权限
- 400 Bad Request：无效的versionId
- 401 Unauthorized：认证失败

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

