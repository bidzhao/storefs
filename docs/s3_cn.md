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

### 服务端加密

StoreFS 支持三种服务端加密模式：

| 模式 | 算法 | 密钥管理 | 请求头 |
|------|------|----------|--------|
| SSE-S3 | AES-256-CTR | StoreFS 管理（桶级别，默认开启） | `x-amz-server-side-encryption: AES256` |
| SSE-C | AES-256-CTR | 客户端提供 | `x-amz-server-side-encryption-customer-algorithm: AES256` |
| SSE-KMS | AES-256-CBC（通过 KMS） | 外部 KMS（当前支持 KMIP 1.2+，后续可扩展云服务） | `x-amz-server-side-encryption: aws:kms` |

**SSE-KMS**（使用 KMS 管理密钥的服务端加密）使用外部 KMS 服务（当前支持 KMIP 1.2+，如 PyKMIP，后续可扩展云服务）来管理加密密钥。启用 SSE-KMS 时：

- 在 KMS 服务器中创建客户主密钥（CMK）并与桶关联。
- 每个对象的数据加密密钥（DEK）在本地生成，并由 CMK 加密保护。
- 加密后的 DEK 存储在对象元数据中，明文 DEK 用于对象片段的 AES-256-CTR 加密。
- 桶级别配置和密钥管理通过 [Admin API](admin-api_cn.md#9-kms-管理) 进行。

**请求头**（用于 SSE-KMS 操作）：

| 请求头 | 说明 |
|--------|------|
| `x-amz-server-side-encryption` | 必须为 `aws:kms` 表示使用 SSE-KMS |
| `x-amz-server-side-encryption-aws-kms-key-id` | 用于加密的 KMS 密钥 ID（KMS 密钥唯一标识符） |

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

**扩展**（兼容 RGW）：

- `GET /?usage` — 在响应中附加账户配额 `<Summary>` 块：
  ```xml
  <Summary>
    <QuotaMaxBytes>-1</QuotaMaxBytes>
    <QuotaMaxBuckets>1000</QuotaMaxBuckets>
    <QuotaMaxObjCount>-1</QuotaMaxObjCount>
    <QuotaMaxBytesPerBucket>-1</QuotaMaxBytesPerBucket>
    <QuotaMaxObjCountPerBucket>-1</QuotaMaxObjCountPerBucket>
  </Summary>
  ```
  （`-1` 表示无限制；该响应省略 `xmlns` 属性。）
- `GET /?max-buckets=N&continuation-token=<上一桶名>` — 分页列出。当还有更多桶时，响应中返回 `ContinuationToken`；下一页从该 token 指定的桶名之后继续。

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

#### 1.7 获取桶标签（GetBucketTagging）

**URL**：`GET /<bucket>?tagging`

**请求**：
```http
GET /mybucket?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<Tagging xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <TagSet>
    <Tag>
      <Key>project</Key>
      <Value>storefs</Value>
    </Tag>
    <Tag>
      <Key>owner</Key>
      <Value>admin</Value>
    </Tag>
  </TagSet>
</Tagging>
```

**说明**：
- 返回桶的所有标签
- 标签是键值对形式（每个桶最多 50 个标签）
- 标签键长度：1-128 字符，标签值长度：0-256 字符
- 只有桶所有者或管理员可以访问此API

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限
- 401 Unauthorized：认证失败

#### 1.8 设置桶标签（PutBucketTagging）

**URL**：`PUT /<bucket>?tagging`

**请求**：
```http
PUT /mybucket?tagging HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<Tagging xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <TagSet>
    <Tag>
      <Key>project</Key>
      <Value>storefs</Value>
    </Tag>
    <Tag>
      <Key>owner</Key>
      <Value>admin</Value>
    </Tag>
  </TagSet>
</Tagging>
```

**说明**：
- 原子替换所有现有标签为提供的标签
- 每个桶最多 50 个标签
- 标签键：1-128 字符，标签值：0-256 字符
- 只有桶所有者或管理员可以访问此API

**响应**：
```http
HTTP/1.1 200 OK
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限
- 400 Bad Request：标签格式无效或标签数量过多
- 401 Unauthorized：认证失败

#### 1.9 删除桶标签（DeleteBucketTagging）

**URL**：`DELETE /<bucket>?tagging`

**请求**：
```http
DELETE /mybucket?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**说明**：
- 删除桶的所有标签
- 只有桶所有者或管理员可以访问此API

**响应**：
```http
HTTP/1.1 204 No Content
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无权限
- 401 Unauthorized：认证失败

#### 1.10 获取桶 ACL（GetBucketAcl）

**URL**：`GET /<bucket>?acl`

**请求**：
```http
GET /mybucket?acl HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<AccessControlPolicy xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Owner>
    <ID>1</ID>
    <DisplayName>admin</DisplayName>
  </Owner>
  <AccessControlList>
    <Grant>
      <Grantee xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:type="CanonicalUser">
        <ID>1</ID>
        <DisplayName>admin</DisplayName>
      </Grantee>
      <Permission>FULL_CONTROL</Permission>
    </Grant>
  </AccessControlList>
</AccessControlPolicy>
```

**所需权限**：`READ_ACP`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `READ_ACP` 权限

#### 1.11 设置桶 ACL（PutBucketAcl）

**URL**：`PUT /<bucket>?acl`

**请求**：
```http
PUT /mybucket?acl HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<AccessControlPolicy xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Owner>
    <ID>1</ID>
  </Owner>
  <AccessControlList>
    <Grant>
      <Grantee xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:type="CanonicalUser">
        <ID>1</ID>
      </Grantee>
      <Permission>FULL_CONTROL</Permission>
    </Grant>
    <Grant>
      <Grantee xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:type="Group">
        <URI>http://acs.amazonaws.com/groups/global/AllUsers</URI>
      </Grantee>
      <Permission>READ</Permission>
    </Grant>
  </AccessControlList>
</AccessControlPolicy>
```

**授权对象类型**：
| xsi:type | 标识 | 说明 |
|----------|------|------|
| `CanonicalUser` | `<ID>` | 指定用户（通过用户 ID） |
| `Group` | `<URI>` | `http://acs.amazonaws.com/groups/global/AllUsers` — 所有用户（含匿名） |
| `Group` | `<URI>` | `http://acs.amazonaws.com/groups/global/AuthenticatedUsers` — 任意已认证用户 |

**权限说明**：
| 权限 | 说明 | 隐含关系 |
|------|------|----------|
| `FULL_CONTROL` | 桶和对象的完全控制 | — |
| `WRITE` | 写入/删除对象 | FULL_CONTROL |
| `READ` | 读取对象和列出桶 | FULL_CONTROL |
| `READ_ACP` | 读取桶 ACL | FULL_CONTROL, WRITE_ACP |
| `WRITE_ACP` | 修改桶 ACL | FULL_CONTROL |

**所需权限**：`WRITE_ACP`

**说明**：
- Owner 始终自动保留 `FULL_CONTROL`（请求中缺失时自动添加）
- 重复授权（相同授权对象 + 相同权限）会自动去重
- 设置 ACL 会原子替换所有现有 ACL 条目

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `WRITE_ACP` 权限
- 400 Bad Request：XML 格式错误

#### 1.12 列出对象版本（ListObjectVersions）

**URL**：`GET /<bucket>?versions`

列出已开启版本控制桶中对象的所有版本（含删除标记 delete marker）。

**请求**：
```http
GET /mybucket?versions HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**查询参数**：
| 参数 | 说明 |
|------|------|
| `prefix` | 按前缀过滤 key |
| `key-marker` | 从指定 key 之后开始列出 |
| `version-id-marker` | 从 `key-marker` 的指定版本之后开始列出 |
| `delimiter` | 按分隔符分组（返回 `CommonPrefixes`） |
| `max-keys` | 最大返回 key 数（默认 1000，最大 1000；`0` 返回空结果） |

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<ListVersionsResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Name>mybucket</Name>
  <Prefix></Prefix>
  <KeyMarker></KeyMarker>
  <VersionIdMarker></VersionIdMarker>
  <MaxKeys>1000</MaxKeys>
  <IsTruncated>false</IsTruncated>
  <Version>
    <Key>doc.txt</Key>
    <VersionId>1700000000000000000</VersionId>
    <IsLatest>true</IsLatest>
    <LastModified>2023-01-01T12:00:00.000Z</LastModified>
    <ETag>"d41d8cd98f00b204e9800998ecf8427e"</ETag>
    <Size>0</Size>
    <StorageClass>STANDARD</StorageClass>
  </Version>
  <DeleteMarker>
    <Key>old.txt</Key>
    <VersionId>1700000000000000001</VersionId>
    <IsLatest>false</IsLatest>
    <LastModified>2023-01-02T12:00:00.000Z</LastModified>
  </DeleteMarker>
</ListVersionsResult>
```

**所需权限**：`READ`（桶）

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `READ` 权限

#### 1.13 获取桶 CORS 配置（GetBucketCors）

**URL**：`GET /<bucket>?cors`

**请求**：
```http
GET /mybucket?cors HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<CORSConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <CORSRule>
    <AllowedOrigin>*</AllowedOrigin>
    <AllowedMethod>GET</AllowedMethod>
    <AllowedMethod>PUT</AllowedMethod>
    <AllowedHeader>*</AllowedHeader>
    <MaxAgeSeconds>3000</MaxAgeSeconds>
  </CORSRule>
</CORSConfiguration>
```

**所需权限**：`READ`

**错误响应**：
- 404 Not Found：桶不存在，或 `NoSuchCORSConfiguration`（未配置 CORS）
- 403 Forbidden：无 `READ` 权限

#### 1.14 设置桶 CORS 配置（PutBucketCors）

**URL**：`PUT /<bucket>?cors`

**请求**：
```http
PUT /mybucket?cors HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<CORSConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <CORSRule>
    <AllowedOrigin>https://app.example.com</AllowedOrigin>
    <AllowedMethod>GET</AllowedMethod>
    <AllowedMethod>PUT</AllowedMethod>
    <AllowedHeader>*</AllowedHeader>
    <ExposeHeader>ETag</ExposeHeader>
    <MaxAgeSeconds>3000</MaxAgeSeconds>
  </CORSRule>
</CORSConfiguration>
```

**说明**：
- 按原样存储 CORS 规则，GET 时原样回读
- 跨域（浏览器）请求按规则评估；匹配的请求会返回 `Access-Control-Allow-*` 头
- 支持预检请求（`OPTIONS` 携带 `Origin` + `Access-Control-Request-Method`）

**所需权限**：`WRITE`

**响应**：`200 OK`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `WRITE` 权限
- 400 Bad Request：`MalformedXML` — 空请求体或非法 XML

#### 1.15 删除桶 CORS 配置（DeleteBucketCors）

**URL**：`DELETE /<bucket>?cors`

**请求**：
```http
DELETE /mybucket?cors HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**所需权限**：`WRITE`

**响应**：`204 No Content`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `WRITE` 权限

#### 1.16 获取桶位置（GetBucketLocation）

**URL**：`GET /<bucket>?location`

**请求**：
```http
GET /mybucket?location HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<LocationConstraint xmlns="http://s3.amazonaws.com/doc/2006-03-01/"></LocationConstraint>
```

**说明**：
- 返回桶的区域（location）；空值表示默认区域（`us-east-1`）

**错误响应**：
- 404 Not Found：桶不存在

#### 1.17 桶策略（Bucket Policy）

##### 1.17.1 设置桶策略（PutBucketPolicy）

**URL**：`PUT /<bucket>?policy`

**请求**：
```http
PUT /mybucket?policy HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/json
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":"*","Action":"s3:GetObject","Resource":"arn:aws:s3:::mybucket/*"}]}
```

**说明**：
- 按原样存储策略 JSON（保留键序）
- 策略必须是合法 JSON，否则返回 `MalformedPolicy`
- `BlockPublicPolicy`（Public Access Block）会拒绝带通配符 `Principal` 且无 `Condition` 的公开策略
- `x-amz-confirm-remove-self-bucket-access: true` 记录 owner 明确移除自己访问的确认（此后 owner 对 policy 的豁免失效）

**所需权限**：`s3:PutBucketPolicy`（`FULL_CONTROL`）；桶 owner 豁免，除非已确认移除自身访问

**响应**：`204 No Content`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`（无权限，或策略被 `BlockPublicPolicy` 拦截）
- 400 Bad Request：`MalformedPolicy` — 非法策略 JSON

##### 1.17.2 获取桶策略（GetBucketPolicy）

**URL**：`GET /<bucket>?policy`

**请求**：
```http
GET /mybucket?policy HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：策略 JSON 字符串直接作为 HTTP body：

```json
{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":"*","Action":"s3:GetObject","Resource":"arn:aws:s3:::mybucket/*"}]}
```

**所需权限**：`s3:GetBucketPolicy`（`FULL_CONTROL`）；桶 owner 豁免，除非已确认移除自身访问

**错误响应**：
- 404 Not Found：桶不存在，或 `NoSuchBucketPolicy`
- 403 Forbidden：`AccessDenied`

##### 1.17.3 删除桶策略（DeleteBucketPolicy）

**URL**：`DELETE /<bucket>?policy`

**请求**：
```http
DELETE /mybucket?policy HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**所需权限**：`s3:DeleteBucketPolicy`（`FULL_CONTROL`）；桶 owner 豁免，除非已确认移除自身访问

**响应**：`204 No Content`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`

##### 1.17.4 获取桶策略状态（GetBucketPolicyStatus）

**URL**：`GET /<bucket>?policyStatus`

**请求**：
```http
GET /mybucket?policyStatus HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<PolicyStatus xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <IsPublic>false</IsPublic>
</PolicyStatus>
```

**说明**：
- 当桶策略包含带通配符 `Principal`（`*` 或 `{"AWS":"*"}`）且无 `Condition` 的 `Allow` 语句，**或**桶 ACL 授予 `AllUsers`/`AuthenticatedUsers` `READ`/`WRITE`/`FULL_CONTROL` 时，`IsPublic` 为 `true`

**所需权限**：`s3:GetBucketPolicyStatus`（`FULL_CONTROL`）

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`

#### 1.18 公共访问阻止（Public Access Block）

##### 1.18.1 设置公共访问阻止（PutBucketPublicAccessBlock）

**URL**：`PUT /<bucket>?publicAccessBlock`

**请求**：
```http
PUT /mybucket?publicAccessBlock HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<PublicAccessBlockConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <BlockPublicAcls>true</BlockPublicAcls>
  <IgnorePublicAcls>true</IgnorePublicAcls>
  <BlockPublicPolicy>true</BlockPublicPolicy>
  <RestrictPublicBuckets>true</RestrictPublicBuckets>
</PublicAccessBlockConfiguration>
```

**说明**：
- 四个独立开关，缺省均为 `false`：
  - `BlockPublicAcls` — 阻止授予公开 ACL
  - `IgnorePublicAcls` — 忽略已有的公开 ACL 授权
  - `BlockPublicPolicy` — 阻止设置公开桶策略（通配符 Principal、无 Condition）
  - `RestrictPublicBuckets` — 限制公开策略/ACL 授予的跨主体访问

**所需权限**：`s3:PutBucketPublicAccessBlock`（`FULL_CONTROL`）

**响应**：`200 OK`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`
- 400 Bad Request：`MalformedXML` — 非法 XML

##### 1.18.2 获取公共访问阻止（GetBucketPublicAccessBlock）

**URL**：`GET /<bucket>?publicAccessBlock`

**请求**：
```http
GET /mybucket?publicAccessBlock HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：
```xml
<PublicAccessBlockConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <BlockPublicAcls>true</BlockPublicAcls>
  <IgnorePublicAcls>false</IgnorePublicAcls>
  <BlockPublicPolicy>false</BlockPublicPolicy>
  <RestrictPublicBuckets>false</RestrictPublicBuckets>
</PublicAccessBlockConfiguration>
```

**所需权限**：`s3:GetBucketPublicAccessBlock`（`FULL_CONTROL`）

**错误响应**：
- 404 Not Found：桶不存在，或 `NoSuchPublicAccessBlockConfiguration`
- 403 Forbidden：`AccessDenied`

##### 1.18.3 删除公共访问阻止（DeleteBucketPublicAccessBlock）

**URL**：`DELETE /<bucket>?publicAccessBlock`

**请求**：
```http
DELETE /mybucket?publicAccessBlock HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**所需权限**：`s3:PutBucketPublicAccessBlock`（`FULL_CONTROL`）

**响应**：`204 No Content`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`

#### 1.19 桶加密（Bucket Encryption）

##### 1.19.1 设置桶加密（PutBucketEncryption）

**URL**：`PUT /<bucket>?encryption`

**请求**：
```http
PUT /mybucket?encryption HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<ServerSideEncryptionConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Rule>
    <ApplyServerSideEncryptionByDefault>
      <SSEAlgorithm>aws:kms</SSEAlgorithm>
      <KMSMasterKeyID>key-id</KMSMasterKeyID>
    </ApplyServerSideEncryptionByDefault>
  </Rule>
</ServerSideEncryptionConfiguration>
```

**说明**：
- 设置桶的默认服务端加密；不带显式加密头的新对象使用该配置
- `SSEAlgorithm`：`AES256`（SSE-S3）或 `aws:kms`（SSE-KMS）；`KMSMasterKeyID` 仅对 `aws:kms` 有意义

**所需权限**：`s3:PutEncryptionConfiguration`（`WRITE`）

**响应**：`200 OK`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`
- 400 Bad Request：`MalformedXML` 或 `InvalidArgument`（非法的 `SSEAlgorithm`）

##### 1.19.2 获取桶加密（GetBucketEncryption）

**URL**：`GET /<bucket>?encryption`

**请求**：
```http
GET /mybucket?encryption HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：
```xml
<ServerSideEncryptionConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Rule>
    <ApplyServerSideEncryptionByDefault>
      <SSEAlgorithm>AES256</SSEAlgorithm>
    </ApplyServerSideEncryptionByDefault>
  </Rule>
</ServerSideEncryptionConfiguration>
```

**错误响应**：
- 404 Not Found：桶不存在，或 `ServerSideEncryptionConfigurationNotFoundError`（未加密）
- 403 Forbidden：`AccessDenied`

##### 1.19.3 删除桶加密（DeleteBucketEncryption）

**URL**：`DELETE /<bucket>?encryption`

**请求**：
```http
DELETE /mybucket?encryption HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：`204 No Content`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`

#### 1.20 桶日志（Bucket Logging）

详见 [桶日志文档](bucketlogging_cn.md)。

##### 1.20.1 获取桶日志配置（GetBucketLogging）

**URL**：`GET /<bucket>?logging`

**请求**：
```http
GET /mybucket?logging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：未配置时返回空的 `<BucketLoggingStatus/>`；已启用时返回配置。完整 XML 载荷见[桶日志文档](bucketlogging_cn.md#配置方式)。

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`

##### 1.20.2 设置桶日志（PutBucketLogging）

**URL**：`PUT /<bucket>?logging`

**请求**：XML `<BucketLoggingStatus>` 请求体，选择日志桶、前缀与（扩展）字段。完整的请求/响应与日志桶策略要求见[桶日志文档](bucketlogging_cn.md#配置方式)。

**所需权限**：`s3:PutBucketLogging`（`FULL_CONTROL`）

**错误响应**：
- 404 Not Found：桶不存在，或日志桶不存在（`NoSuchKey`）
- 400 Bad Request：`InvalidArgument`（目标为源桶 / 已配置日志 / 已加密 / Requester Pays）或 `MalformedXML`（非法的 `LoggingType`/`PartitionDateSource`）
- 403 Forbidden：`AccessDenied`（日志桶策略未授权日志服务主体）

##### 1.20.3 删除桶日志（DeleteBucketLogging）

**URL**：`DELETE /<bucket>?logging`

**请求**：
```http
DELETE /mybucket?logging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：`204 No Content`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`

##### 1.20.4 立即投递日志（扩展）

**URL**：`POST /<bucket>?logging`

立即将待投递的访问日志 flush 成日志对象，并返回生成的日志对象 key（`FlushedLoggingObject`）。见[桶日志文档](bucketlogging_cn.md#配置方式)。

#### 1.21 请求付费（Request Payment）

##### 1.21.1 获取桶请求付费配置（GetBucketRequestPayment）

**URL**：`GET /<bucket>?requestPayment`

**请求**：
```http
GET /mybucket?requestPayment HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<RequestPaymentConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Payer>BucketOwner</Payer>
</RequestPaymentConfiguration>
```

**说明**：`Payer` 为 `BucketOwner`（默认）或 `Requester`。

**所需权限**：`s3:GetBucketRequestPayment`（`FULL_CONTROL`）

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`

##### 1.21.2 设置桶请求付费（PutBucketRequestPayment）

**URL**：`PUT /<bucket>?requestPayment`

**请求**：
```http
PUT /mybucket?requestPayment HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<RequestPaymentConfiguration xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Payer>Requester</Payer>
</RequestPaymentConfiguration>
```

**所需权限**：`s3:PutBucketRequestPayment`（`FULL_CONTROL`）

**响应**：`200 OK`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`
- 400 Bad Request：`MalformedXML` — `Payer` 必须为 `BucketOwner` 或 `Requester`

#### 1.22 桶对象所有权（Bucket Ownership Controls）

##### 1.22.1 设置对象所有权（PutBucketOwnershipControls）

**URL**：`PUT /<bucket>?ownershipControls`

**请求**：
```http
PUT /mybucket?ownershipControls HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<OwnershipControls xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Rule>
    <ObjectOwnership>BucketOwnerEnforced</ObjectOwnership>
  </Rule>
</OwnershipControls>
```

**说明**：
- `ObjectOwnership`：`BucketOwnerEnforced`、`BucketOwnerPreferred` 或 `ObjectWriter`
- `BucketOwnerEnforced` 不能在桶存在公开 ACL 时设置（`InvalidBucketAclWithObjectOwnership`）；该模式还会禁用对象 ACL

**所需权限**：`s3:PutBucketOwnershipControls`（`FULL_CONTROL`）

**响应**：`200 OK`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`
- 400 Bad Request：`MalformedXML`（非法 XML 或 `ObjectOwnership` 值）或 `InvalidBucketAclWithObjectOwnership`

##### 1.22.2 获取对象所有权（GetBucketOwnershipControls）

**URL**：`GET /<bucket>?ownershipControls`

**请求**：
```http
GET /mybucket?ownershipControls HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：
```xml
<OwnershipControls xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Rule>
    <ObjectOwnership>BucketOwnerEnforced</ObjectOwnership>
  </Rule>
</OwnershipControls>
```

**所需权限**：`s3:GetBucketOwnershipControls`（`FULL_CONTROL`）

**错误响应**：
- 404 Not Found：桶不存在，或 `OwnershipControlsNotFoundError`（未配置）
- 403 Forbidden：`AccessDenied`

##### 1.22.3 删除对象所有权（DeleteBucketOwnershipControls）

**URL**：`DELETE /<bucket>?ownershipControls`

**请求**：
```http
DELETE /mybucket?ownershipControls HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**所需权限**：`s3:PutBucketOwnershipControls`（`FULL_CONTROL`）

**响应**：`204 No Content`

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：`AccessDenied`


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
    <ETag>"d41d8cd98f00b204e9800998ecf8427e"</ETag>
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
    <ETag>"d41d8cd98f00b204e9800998ecf8427e"</ETag>
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
ETag: "d41d8cd98f00b204e9800998ecf8427e"
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
ETag: "d41d8cd98f00b204e9800998ecf8427e"
Last-Modified: 2023-01-01T12:00:00Z

[object data]
```

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：无权限

##### 2.4.1 Range Get（带 Range 头的 GetObject）

`GET` 支持 HTTP `Range` 头进行对象的部分读取（字节区间），遵循 AWS S3 语义。

**请求**：
```http
GET /mybucket/photos/2023/01.jpg HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
Range: bytes=0-1023
```

**支持的区间形式**：

| 头部 | 说明 |
|--------|-------------|
| `bytes=start-end` | 固定区间 —— 从 `start` 到 `end`（含）的字节。`end` 会被收敛到对象大小减 1。 |
| `bytes=start-` | 开放式 —— 从 `start` 到对象末尾。 |
| `bytes=-suffix` | 后缀式 —— 对象最后 `suffix` 个字节（大于对象大小时收敛为完整对象）。 |

**响应（206 Partial Content）**：
```http
HTTP/1.1 206 Partial Content
Content-Type: image/jpeg
Content-Length: 1024
Content-Range: bytes 0-1023/4096
Accept-Ranges: bytes
ETag: "d41d8cd98f00b204e9800998ecf8427e"
Last-Modified: 2023-01-01T12:00:00Z

[object data — 第 0 到 1023 字节]
```

行为说明：
- `GET` 与 `HEAD` 均返回 `Accept-Ranges: bytes`。
- **多区间**请求（`bytes=0-99,200-299`）**不支持**，按 AWS 语义返回 **`416` Range Not Satisfiable**（`InvalidRange`）。
- 支持 **`If-Range`**：当校验器（ETag 或 Last-Modified）不匹配时，忽略区间并返回**完整对象**（`200 OK`）。
- 条件头部（`If-Match`、`If-None-Match`、`If-Modified-Since`、`If-Unmodified-Since`）在区间处理之前评估：`304 Not Modified` / `412 Precondition Failed` 优先。

**错误响应（416）**：
```http
HTTP/1.1 416 Range Not Satisfiable
Content-Range: bytes */4096
Content-Type: application/xml
```
- 出现 416 的情况：对象为空、`start` 超过对象大小、`end < start`、发送多区间列表，或头部格式非法 / 非 `bytes` 单位。

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
ETag: "d41d8cd98f00b204e9800998ecf8427e"
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
    <ETag>"d41d8cd98f00b204e9800998ecf8427e"</ETag>
    <Size>5242880</Size>
  </Part>
  <Part>
    <PartNumber>2</PartNumber>
    <LastModified>2023-01-01T12:05:00Z</LastModified>
    <ETag>"5eb63bbbe01eeed093cb22bb8f5acdc3"</ETag>
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
    <ETag>"d41d8cd98f00b204e9800998ecf8427e"</ETag>
  </Part>
  <Part>
    <PartNumber>2</PartNumber>
    <ETag>"5eb63bbbe01eeed093cb22bb8f5acdc3"</ETag>
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
  <ETag>"09c2a2c5a6c5b7a8a84d3e6f4a2b1c8d"</ETag>
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

#### 2.8 对象标签（Object Tagging）

对象标签允许您为存储的对象分配键值对（标签）。标签可用于分类、访问控制和成本跟踪。

**标签规则**：
- 每个对象最多 10 个标签
- 标签键长度：1-128 字符
- 标签值长度：0-256 字符
- 标签是版本感知的：启用版本控制时，标签按对象版本存储

##### 2.8.1 获取对象标签（GetObjectTagging）

**URL**：`GET /<bucket>/<object>?tagging[&versionId=<version_id>]`

**请求参数**：
- `versionId`（可选）：特定对象版本的ID

**请求**：
```http
GET /mybucket/document.pdf?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<Tagging xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <TagSet>
    <Tag>
      <Key>department</Key>
      <Value>engineering</Value>
    </Tag>
    <Tag>
      <Key>type</Key>
      <Value>report</Value>
    </Tag>
  </TagSet>
</Tagging>
```

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：无权限
- 401 Unauthorized：认证失败

##### 2.8.2 设置对象标签（PutObjectTagging）

**URL**：`PUT /<bucket>/<object>?tagging[&versionId=<version_id>]`

**请求参数**：
- `versionId`（可选）：特定对象版本的ID

**请求**：
```http
PUT /mybucket/document.pdf?tagging HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<Tagging xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <TagSet>
    <Tag>
      <Key>department</Key>
      <Value>engineering</Value>
    </Tag>
    <Tag>
      <Key>type</Key>
      <Value>report</Value>
    </Tag>
  </TagSet>
</Tagging>
```

**说明**：
- 原子替换对象的所有现有标签
- 每个对象最多 10 个标签
- 启用版本控制时，标签应用于指定版本（如果省略 `versionId`，则应用于当前版本）

**响应**：
```http
HTTP/1.1 200 OK
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：无权限
- 400 Bad Request：标签格式无效、标签数量过多或标签键/值长度无效
- 401 Unauthorized：认证失败

##### 2.8.3 删除对象标签（DeleteObjectTagging）

**URL**：`DELETE /<bucket>/<object>?tagging[&versionId=<version_id>]`

**请求参数**：
- `versionId`（可选）：特定对象版本的ID

**请求**：
```http
DELETE /mybucket/document.pdf?tagging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**说明**：
- 删除对象的所有标签
- 启用版本控制时，标签从指定版本中删除

**响应**：
```http
HTTP/1.1 204 No Content
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：无权限
- 401 Unauthorized：认证失败

#### 2.9 复制对象时的标签处理（CopyObject with Tagging）

复制对象时，可以通过以下请求头控制标签行为：

**请求头**：
- `x-amz-tagging-directive`：设置为 `COPY` 以从源对象复制标签，或 `REPLACE`（默认）以从 `x-amz-tagging` 请求头设置标签
- `x-amz-tagging`：URL编码的标签格式（`key1=value1&key2=value2`），在使用 `REPLACE` 指令时使用

**请求**：
```http
PUT /mybucket/destination.pdf HTTP/1.1
Host: 127.0.0.1:8901
x-amz-copy-source: /mybucket/source.pdf
x-amz-tagging-directive: REPLACE
x-amz-tagging: department=engineering&type=report
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**说明**：
- `x-amz-tagging-directive: COPY` — 将源对象的标签复制到目标对象
- `x-amz-tagging-directive: REPLACE`（默认）— 从 `x-amz-tagging` 请求头设置目标对象的标签
- 如果两个请求头都不存在，目标对象将没有标签

#### 2.10 分块上传标签（Multipart Upload Tagging）

##### 2.10.1 使用标签初始化分块上传

在初始化分块上传时，可以通过 `x-amz-tagging` 请求头指定标签。这些标签将在分块上传完成时自动应用于最终对象。

**URL**：`POST /<bucket>/<object>?uploads`

**请求**：
```http
POST /mybucket/largefile.zip?uploads HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/zip
x-amz-tagging: project=storefs&department=engineering
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**说明**：
- 标签以 URL 编码的键值对格式指定：`key1=value1&key2=value2`
- 每次分块上传最多 10 个标签
- 标签与分块上传元数据一起存储，并在完成时应用于最终对象
- 如果未提供 `x-amz-tagging` 请求头，最终对象将没有标签

#### 2.11 选择对象内容（SelectObjectContent）

S3 Select 允许应用程序使用 SQL 表达式查询结构化对象内容，无需下载整个对象。支持 CSV 和 JSON 格式，并支持 GZIP 解压缩。

**URL**：`POST /<bucket>/<object>?select&select-type=2`

**支持的 SQL 语法**：
- `SELECT * FROM S3Object` — 选择所有列
- `SELECT column1, column2 FROM S3Object` — 选择特定列
- `SELECT * FROM S3Object WHERE condition` — 使用 WHERE 子句过滤行
- `SELECT COUNT(*) FROM S3Object` — 聚合函数（COUNT, SUM, AVG, MIN, MAX）
- `SELECT * FROM S3Object LIMIT N` — 限制结果数量

**不支持的 SQL 功能**：
- JOIN（多表连接）
- GROUP BY、ORDER BY、HAVING
- 子查询

**列引用方式**：
- 当 `FileHeaderInfo=USE`（CSV 含表头）时，列可以通过表头名称引用：`SELECT name, age FROM S3Object`
- 当 `FileHeaderInfo=NONE` 或 `IGNORE` 时，列按位置使用 `_1`、`_2` ... `_N` 引用：`SELECT _1, _2 FROM S3Object`
- 对于 JSON 输入，每个记录中的键名作为列名

**支持的 SQL 函数**：

**字符串函数**：
| 函数 | 说明 |
|------|------|
| `SUBSTRING(s, start [, length])` | 提取子串（位置从 1 开始） |
| `TRIM(s)` | 去除首尾空白字符 |
| `LTRIM(s)` | 去除前导空白字符 |
| `RTRIM(s)` | 去除尾部空白字符 |
| `UPPER(s)` | 转换为大写 |
| `LOWER(s)` | 转换为小写 |
| `CHAR_LENGTH(s)` | 字符串的字符数 |
| `COALESCE(val1, val2, ...)` | 返回第一个非 NULL 值 |
| `CAST(val AS type)` | 类型转换（字符串转数值） |
| `NULLIF(val1, val2)` | 如果两值相等则返回 NULL |

**日期函数**：
| 函数 | 说明 |
|------|------|
| `DATE_ADD(date, interval)` | 日期加法（基础支持） |
| `DATE_SUB(date, interval)` | 日期减法（基础支持） |
| `DATEDIFF(date1, date2)` | 两日期差值（基础支持） |
| `EXTRACT(unit FROM date)` | 提取日期部分（基础支持） |

**请求**（CSV 输入，CSV 输出）：
```http
POST /mybucket/data.csv?select&select-type=2 HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<SelectObjectContentRequest xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Expression>SELECT * FROM S3Object WHERE age > 30</Expression>
  <ExpressionType>SQL</ExpressionType>
  <InputSerialization>
    <CSV>
      <FileHeaderInfo>USE</FileHeaderInfo>
      <RecordDelimiter>\n</RecordDelimiter>
      <FieldDelimiter>,</FieldDelimiter>
    </CSV>
    <CompressionType>NONE</CompressionType>
  </InputSerialization>
  <OutputSerialization>
    <CSV>
      <RecordDelimiter>\n</RecordDelimiter>
      <FieldDelimiter>,</FieldDelimiter>
    </CSV>
  </OutputSerialization>
  <RequestProgress>
    <Enabled>true</Enabled>
  </RequestProgress>
</SelectObjectContentRequest>
```

**请求**（JSON 输入，JSON 输出）：
```http
POST /mybucket/data.json?select&select-type=2 HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<SelectObjectContentRequest xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Expression>SELECT name, age FROM S3Object WHERE age > 25</Expression>
  <ExpressionType>SQL</ExpressionType>
  <InputSerialization>
    <JSON>
      <Type>LINES</Type>
    </JSON>
    <CompressionType>NONE</CompressionType>
  </InputSerialization>
  <OutputSerialization>
    <JSON>
      <RecordDelimiter>\n</RecordDelimiter>
    </JSON>
  </OutputSerialization>
</SelectObjectContentRequest>
```

**请求参数**（XML body）：

| 参数 | 说明 |
|------|------|
| `Expression` | 要执行的 SQL 表达式（如 `SELECT * FROM S3Object`） |
| `ExpressionType` | 必须为 `SQL` |
| `InputSerialization.CSV` | CSV 输入配置（未指定 JSON 时必须） |
| `InputSerialization.CSV.FileHeaderInfo` | `USE`（首行作列名）、`IGNORE`（跳过首行）、`NONE`（无表头） |
| `InputSerialization.CSV.FieldDelimiter` | 字段分隔符（默认：`,`） |
| `InputSerialization.CSV.RecordDelimiter` | 记录分隔符（默认：`\n`） |
| `InputSerialization.CSV.QuoteChar` | 引号字符（默认：`"`） |
| `InputSerialization.CSV.QuoteEscapeChar` | 引号转义字符（默认：`"`） |
| `InputSerialization.CSV.Comments` | 注释字符（以此开头的行将被跳过） |
| `InputSerialization.JSON` | JSON 输入配置（未指定 CSV 时必须） |
| `InputSerialization.JSON.Type` | `LINES`（每行一个 JSON 对象）或 `DOCUMENT`（JSON 对象数组） |
| `InputSerialization.CompressionType` | `NONE`（默认）或 `GZIP` |
| `OutputSerialization.CSV` | CSV 输出配置 |
| `OutputSerialization.CSV.FieldDelimiter` | 输出字段分隔符（默认：`,`） |
| `OutputSerialization.CSV.RecordDelimiter` | 输出记录分隔符（默认：`\n`） |
| `OutputSerialization.JSON` | JSON 输出配置 |
| `OutputSerialization.JSON.RecordDelimiter` | 输出记录分隔符（默认：`\n`） |
| `RequestProgress.Enabled` | 设置为 `true` 以接收定期进度事件 |

**响应**：响应为 **AWS S3 Select 事件流**（二进制格式，使用长度前缀帧和 CRC32 校验和）。流包含以下事件类型：

| 事件类型 | 说明 |
|---------|------|
| `Records` | 包含查询结果载荷（CSV 或 JSON 格式的记录） |
| `Cont` | 长时间运行查询期间的保活事件 |
| `Progress` | 定期进度信息（启用 `RequestProgress` 时） |
| `Stats` | 最终统计信息（扫描字节数、返回字节数） |
| `End` | 表示事件流结束 |

**响应头**：
```http
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Transfer-Encoding: chunked
```

**响应体（二进制事件流）**：

事件流使用二进制协议。每个帧的格式如下：
```
TotalByteLength (4B) + HeadersByteLength (4B) + PreludeCRC (4B) + Headers + Payload + MessageCRC (4B)
```

**说明**：
- JSON 输出格式使用 `_1`、`_2`、...、`_N` 作为键名（如 `{"_1": "Alice", "_2": "35"}`）
- CSV 输出且 `FileHeaderInfo=USE` 时，使用表头列名；`NONE` 时使用位置引用
- Progress 和 Stats 事件的 XML 载荷包含 `<BytesScanned>` 和 `<BytesReturned>` 字段

**示例输出**（CSV 输入 `name,age\nAlice,35\nBob,28\n`，查询 `SELECT * FROM S3Object WHERE age > 30`）：
```
Alice,35
[Stats] Scanned: 32 bytes, Returned: 8 bytes
```

**错误响应**（作为 Records 事件在流中发送）：
- `InvalidRequest` — XML 请求格式错误
- `InvalidExpressionType` — 仅支持 `SQL`
- `InvalidSerialization` — 必须指定 CSV 或 JSON 之一
- `ParseError` — SQL 解析错误
- `UnsupportedSQL` — 不支持的 SQL 功能（JOIN、GROUP BY 等）
- `NoSuchKey` — 对象不存在
- `AccessDenied` — 无权限
- `InvalidCompression` — 无效的压缩类型或损坏的 GZIP 数据
- `InternalError` — 服务器内部错误

#### 2.12 对象 ACL

##### 2.12.1 获取对象 ACL（GetObjectAcl）

**URL**：`GET /<bucket>/<key>?acl`

**请求**：
```http
GET /mybucket/doc.txt?acl HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<AccessControlPolicy xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Owner>
    <ID>1</ID>
    <DisplayName>admin</DisplayName>
  </Owner>
  <AccessControlList>
    <Grant>
      <Grantee xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:type="CanonicalUser">
        <ID>1</ID>
        <DisplayName>admin</DisplayName>
      </Grantee>
      <Permission>FULL_CONTROL</Permission>
    </Grant>
  </AccessControlList>
</AccessControlPolicy>
```

**说明**：
- 只有对象 owner（对象 ACL 中持有 `FULL_CONTROL` 的 canonical grantee，即上传者）或对该对象被授予 `READ_ACP`/`FULL_CONTROL` 的用户可读取。**不**回退到桶 ACL
- 版本化桶中用 `?versionId=<id>` 选择指定版本

**所需权限**：`s3:GetObjectAcl`（基于对象 ACL；桶策略可覆盖）

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：`AccessDenied`

##### 2.12.2 设置对象 ACL（PutObjectAcl）

**URL**：`PUT /<bucket>/<key>?acl`

**请求**（通过 `x-amz-acl` canned ACL 头）：
```http
PUT /mybucket/doc.txt?acl HTTP/1.1
Host: 127.0.0.1:8901
x-amz-acl: public-read
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

或通过 XML `AccessControlPolicy` 请求体（结构与[获取对象 ACL](#2121-获取对象-aclgetobjectacl)相同，含一条或多条 `<Grant>`）。

**说明**：
- Canned ACL：`private`、`public-read`、`public-read-write`、`authenticated-read`、`bucket-owner-read`、`bucket-owner-full-control`（空 → `private`）
- 只有对象 owner 或对该对象被授予 `WRITE_ACP`/`FULL_CONTROL` 的用户可修改；不回退到桶 ACL
- `ObjectOwnership = BucketOwnerEnforced` 的桶会拒绝任何对象 ACL，返回 `AccessControlListNotSupported`（400）

**所需权限**：`s3:PutObjectAcl`（基于对象 ACL；桶策略可覆盖）

**响应**：`200 OK`

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：`AccessDenied`
- 400 Bad Request：`MalformedACLError`、`AccessControlListNotSupported` 或非法的 canned ACL

#### 2.13 对象法定保留（Object Legal Hold）

##### 2.13.1 获取对象法定保留（GetObjectLegalHold）

**URL**：`GET /<bucket>/<key>?legal-hold`

**请求**：
```http
GET /mybucket/doc.txt?legal-hold HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<LegalHold xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Status>OFF</Status>
</LegalHold>
```

**说明**：`Status` 为 `ON` 或 `OFF`。需要桶已启用对象锁定（Object Lock）；否则返回 `InvalidRequest`（400）。`?versionId=<id>` 选择指定版本。

**所需权限**：`READ`

**错误响应**：
- 404 Not Found：桶或对象不存在
- 400 Bad Request：`InvalidRequest` — 桶缺少对象锁定配置
- 403 Forbidden：`AccessDenied`

##### 2.13.2 设置对象法定保留（PutObjectLegalHold）

**URL**：`PUT /<bucket>/<key>?legal-hold`

**请求**：
```http
PUT /mybucket/doc.txt?legal-hold HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...

<LegalHold xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Status>ON</Status>
</LegalHold>
```

**所需权限**：`WRITE`

**响应**：`200 OK`

**错误响应**：
- 404 Not Found：桶或对象不存在
- 400 Bad Request：`MalformedXML`（非法 Status）或 `InvalidRequest`（桶缺少对象锁定配置）
- 403 Forbidden：`AccessDenied`

#### 2.14 获取对象属性（GetObjectAttributes）

**URL**：`GET /<bucket>/<key>`，携带 `x-amz-object-attributes` 头

返回对象的选定元数据属性（多部分对象可含每个分片的属性），无需下载内容。

**请求**：
```http
GET /mybucket/doc.txt HTTP/1.1
Host: 127.0.0.1:8901
x-amz-object-attributes: etag,checksum,storageclass,objectsize,objectparts
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**请求头**：
| 头 | 说明 |
|----|------|
| `x-amz-object-attributes` | 逗号分隔的待返回属性：`etag`、`checksum`、`storageclass`、`objectsize`、`objectparts` |
| `x-amz-max-parts` | 请求 `objectparts` 时最多返回的分片数 |
| `x-amz-part-number-marker` | 从该分片号之后开始列出分片 |
| `versionId`（查询参数） | 选择指定版本 |

**响应**：
```xml
<?xml version="1.0" encoding="UTF-8"?>
<GetObjectAttributesOutput xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <ETag>"d41d8cd98f00b204e9800998ecf8427e"</ETag>
  <ObjectSize>0</ObjectSize>
  <StorageClass>STANDARD</StorageClass>
  <Checksum>
    <ChecksumType>CRC32</ChecksumType>
    <ChecksumCRC32>abcd==</ChecksumCRC32>
  </Checksum>
  <ObjectParts>
    <IsTruncated>false</IsTruncated>
    <MaxParts>1000</MaxParts>
    <PartsCount>2</PartsCount>
    <Part>
      <PartNumber>1</PartNumber>
      <Size>5242880</Size>
    </Part>
  </ObjectParts>
</GetObjectAttributesOutput>
```

**说明**：
- 版本化桶：默认最新版本，或 `versionId` 指定的版本；删除标记返回 `404 NoSuchKey` 并带 `x-amz-delete-marker: true`
- SSE-C 加密对象需携带 customer key 头（与 HEAD 一致）
- 多部分对象按分片返回 `PartNumber`、`Size` 与校验和（若存在），支持分页

**所需权限**：`READ`

**错误响应**：
- 404 Not Found：桶或对象不存在
- 403 Forbidden：`AccessDenied`
- 400 Bad Request：SSE-C key 缺失或非法

#### 2.15 复制分片上传（UploadPartCopy）

**URL**：`PUT /<bucket>/<key>?partNumber=<N>&uploadId=<id>`，携带 `x-amz-copy-source`

将源对象的字节区间（或整个对象）复制为进行中多部分上传的一个分片，无需下载/上传数据。

**请求**：
```http
PUT /mybucket/dest.bin?partNumber=1&uploadId=<upload-id> HTTP/1.1
Host: 127.0.0.1:8901
x-amz-copy-source: /source-bucket/src.bin?versionId=<version-id>
x-amz-copy-source-range: bytes=0-1048575
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**请求头**：
| 头 | 说明 |
|----|------|
| `x-amz-copy-source` | `/bucket/key`，可带 `?versionId=`；源对象必须可读 |
| `x-amz-copy-source-range` | `bytes=start-end` 待复制的字节区间（省略则复制整个对象） |
| `x-amz-copy-source-server-side-encryption-customer-*` | SSE-C 加密源的 customer key |

**响应**：标准 `CopyPartResult` XML（复制分片的 ETag 与 `LastModified`）。

**所需权限**：目标 `s3:PutObject`，源 `s3:GetObject`

**错误响应**：
- 404 Not Found：桶或源对象不存在
- 403 Forbidden：`AccessDenied`
- 400 Bad Request：`InvalidRequest`（非法的 `uploadId`/`partNumber`/`x-amz-copy-source`）、`InvalidArgument`（非法的 range）

#### 2.16 获取对象 Torrent（GetObjectTorrent）

**URL**：`GET /<bucket>/<key>?torrent`

StoreFS 不生成 torrent 文件。按 AWS 语义返回 `404 NoSuchKey`（未配置 torrent 支持）。

**请求**：
```http
GET /mybucket/doc.txt?torrent HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**：
- 404 Not Found：`NoSuchKey` — 未配置 torrent 支持

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

