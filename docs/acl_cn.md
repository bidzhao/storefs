# ACL（访问控制列表）文档

## 概述

StoreFS 实现了与 S3 兼容的访问控制列表（ACL），用于桶级别的授权控制。ACL 提供了细粒度的权限模型，控制不同用户对桶可以执行的操作。系统支持 S3 XML API（GetBucketAcl / PutBucketAcl）和 JSON Admin API 两种方式来管理 ACL。

## 核心概念

### 权限

ACL 定义了五种权限级别：

| 权限 | 说明 | 隐含关系 |
|------|------|----------|
| `FULL_CONTROL` | 对桶和对象的完全控制，包括读写 ACL | — |
| `WRITE` | 写入和删除桶中的对象 | `FULL_CONTROL` |
| `READ` | 列出桶中的对象和读取对象内容 | `FULL_CONTROL` |
| `READ_ACP` | 读取桶的 ACL | `FULL_CONTROL`、`WRITE_ACP` |
| `WRITE_ACP` | 修改桶的 ACL | `FULL_CONTROL` |

权限包含关系规则：
- `FULL_CONTROL` 包含所有其他权限（READ、WRITE、READ_ACP、WRITE_ACP）
- `WRITE_ACP` 包含 `READ_ACP`
- 不存在其他跨权限包含关系（例如，`READ` 不包含 `WRITE`，反之亦然）
- 权限必须精确匹配或被更广泛的权限所包含

### 授权对象类型

ACL 授权中可以指定三种类型的授权对象：

| 授权对象类型 | 标识 | 说明 |
|-------------|------|------|
| `CanonicalUser` | 用户 ID（数字）| 指定某个 StoreFS 用户 |
| `AllUsers` | URI：`http://acs.amazonaws.com/groups/global/AllUsers` | 所有请求，包括匿名（未认证）请求 |
| `AuthenticatedUsers` | URI：`http://acs.amazonaws.com/groups/global/AuthenticatedUsers` | 任何已认证的 StoreFS 用户 |

## 默认 ACL

创建新桶时，StoreFS 会自动分配一个默认 ACL，授予桶所有者 `FULL_CONTROL` 权限：

```json
[
  {
    "grantee": "<owner_user_id>",
    "grantee_type": "canonical_user",
    "permission": "FULL_CONTROL"
  }
]
```

当桶没有设置任何 ACL 时，系统会回退到**旧版授权**模式（见下文）。

## 授权流程

ACL 授权按以下顺序进行：

```
请求 → 识别用户 → 查找桶 → 检查 ACL
                                  ↓
                   是否存在 ACL？──→ 否 ──→ 旧版授权
                     │ 是
                     ↓
                 逐条评估 ACL 条目
                  ────────────────
                  任一授权对象匹配    否 ──→ 拒绝
                  且权限被包含？
                     │ 是
                     ↓
                   允许访问
```

### 旧版授权（回退模式）

当桶没有 ACL 时，系统使用旧版授权检查：

| 条件 | 匿名用户 | 已认证用户 |
|------|---------|-----------|
| 桶所有者 | — | 完全访问 |
| 公共桶（`is_public=true`）| 仅 READ | 同匿名 + 所有者访问 |
| 私有桶（`is_public=false`）| 拒绝 | 仅所有者 |
| 组管理员（group_admin）| — | 可访问同组用户拥有的桶 |
| 超级管理员（super_admin）| — | 始终有访问权限 |

## S3 API

### 获取桶 ACL（GetBucketAcl）

获取桶的 ACL。

**URL**：`GET /<bucket>?acl`

**所需权限**：`READ_ACP`

**请求**：
```http
GET /mybucket?acl HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**响应**（200 OK）：
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

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `READ_ACP` 权限或访问密钥无效

当未设置 ACL 时，返回默认 ACL（所有者拥有 FULL_CONTROL）。

### 设置桶 ACL（PutBucketAcl）

设置桶的 ACL。此操作会**替换**所有现有 ACL 条目（原子操作）。

**URL**：`PUT /<bucket>?acl`

**所需权限**：`WRITE_ACP`

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

**说明**：
- XML 中的 **Owner** 部分仅为参考信息 — 实际所有者由桶决定，而非请求体
- 所有者始终自动保留 `FULL_CONTROL`。如果请求的授权列表中没有包含所有者的 `FULL_CONTROL`，系统会自动添加
- 重复授权（相同的授权对象 + 相同的权限）会自动去重
- 请求中的无效权限会被静默跳过
- 指定不存在的用户作为 `CanonicalUser` 授权对象时，该授权会被静默跳过

**响应**：
```http
HTTP/1.1 200 OK
Content-Length: 0
```

**错误响应**：
- 404 Not Found：桶不存在
- 403 Forbidden：无 `WRITE_ACP` 权限
- 400 Bad Request：XML 格式错误

### ACL 在 S3 操作中的行为

所有 S3 操作在访问桶时都会检查相应的 ACL 权限：

| S3 操作 | 检查的权限 |
|---------|-----------|
| 列出桶（ListBuckets）| 每个桶的 `READ` |
| 查询桶是否存在（HeadBucket）| `READ` |
| 列出对象（ListObjects）| `READ` |
| 上传对象（PutObject）| `WRITE` |
| 下载对象（GetObject）| `READ` |
| 删除对象（DeleteObject）| `WRITE` |
| 复制对象（CopyObject）| `READ`（源）+ `WRITE`（目标）|
| 初始化分块上传 | `WRITE` |
| 上传分块 | `WRITE` |
| 完成分块上传 | `WRITE` |
| 中止分块上传 | `WRITE` |
| 获取桶 ACL | `READ_ACP` |
| 设置桶 ACL | `WRITE_ACP` |
| 获取桶版本控制 | `READ` |
| 设置桶版本控制 | `WRITE` |
| 获取桶标签 | `READ` |
| 设置桶标签 | `WRITE` |
| 删除桶标签 | `WRITE` |

## 管理 API（Admin API）

管理 API 提供基于 JSON 的端点来管理桶 ACL。

### 获取桶 ACL

**URL**：`GET /api/buckets/{id}/acl`

**所需角色**：桶所有者、组管理员或超级管理员

**响应**（200 OK）：
```json
{
  "bucketId": "1",
  "ownerId": "1",
  "ownerName": "admin",
  "grants": [
    {
      "id": "1",
      "granteeId": "1",
      "granteeName": "admin",
      "granteeType": "canonical_user",
      "permission": "FULL_CONTROL"
    }
  ]
}
```

当未设置 ACL 时，返回默认 ACL：
```json
{
  "bucketId": "1",
  "ownerId": "1",
  "ownerName": "admin",
  "grants": [
    {
      "granteeId": "1",
      "granteeType": "canonical_user",
      "permission": "FULL_CONTROL"
    }
  ]
}
```

**错误响应**：
- 401 Unauthorized：未认证
- 403 Forbidden：无权查看此桶的 ACL
- 404 Not Found：桶不存在

### 设置桶 ACL

**URL**：`PUT /api/buckets/{id}/acl`

**所需角色**：桶所有者、组管理员或超级管理员

**请求**：
```json
{
  "grants": [
    {
      "granteeId": "1",
      "granteeType": "canonical_user",
      "permission": "FULL_CONTROL"
    },
    {
      "granteeUri": "http://acs.amazonaws.com/groups/global/AllUsers",
      "granteeType": "all_users",
      "permission": "READ"
    },
    {
      "granteeUri": "http://acs.amazonaws.com/groups/global/AuthenticatedUsers",
      "granteeType": "authenticated_users",
      "permission": "READ"
    }
  ]
}
```

**请求字段**：

| 字段 | 必填 | 说明 |
|------|------|------|
| `granteeType` | 是 | `canonical_user`、`all_users` 或 `authenticated_users` |
| `granteeId` | `canonical_user` 时必填 | 授权对象的数字用户 ID |
| `granteeUri` | 组类型时必填 | AllUsers 或 AuthenticatedUsers 的 URI |
| `permission` | 是 | `FULL_CONTROL`、`WRITE`、`READ`、`READ_ACP` 或 `WRITE_ACP` |

**说明**：
- 所有者始终自动保留 `FULL_CONTROL`（请求中缺失时自动添加）
- 重复授权（相同授权对象 + 相同权限）会自动去重
- 无效的权限或不存在的用户 ID 会被静默跳过
- 此操作**原子替换**所有现有 ACL 条目

**响应**：
```json
{
  "status": "ok"
}
```

**错误响应**：
- 400 Bad Request：请求体格式无效
- 401 Unauthorized：未认证
- 403 Forbidden：无权限
- 404 Not Found：桶不存在

## 数据库结构

ACL 条目存储在 `bucket_acls` 表中：

```sql
CREATE TABLE IF NOT EXISTS bucket_acls (
    id            BIGINT       NOT NULL COMMENT 'ACL 条目唯一 ID',
    bucket_id     BIGINT       NOT NULL COMMENT '桶 ID',
    grantee       VARCHAR(256) NOT NULL COMMENT '授权对象标识：canonical_user 时为用户 ID，AllUsers/AuthenticatedUsers 时为 URI',
    grantee_type  VARCHAR(32)  NOT NULL COMMENT '授权对象类型：canonical_user | all_users | authenticated_users',
    permission    VARCHAR(32)  NOT NULL COMMENT 'S3 权限：FULL_CONTROL | WRITE | READ | READ_ACP | WRITE_ACP',
    created_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX index_bucket_acls_bucket_id (bucket_id) USING BITMAP
) PRIMARY KEY (id)
DISTRIBUTED BY HASH(id) BUCKETS 3;
```

## 使用示例

### 授予匿名用户读取权限

通过 S3 API 将桶设为公开可读：

```http
PUT /mybucket?acl HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 ...

<AccessControlPolicy>
  <Owner><ID>1</ID></Owner>
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

之后，匿名用户可以列出和读取 `mybucket` 中的对象，但不能写入或删除。

### 授予其他用户写入权限

授予用户 ID `42` 对桶的写入权限：

```http
PUT /mybucket?acl HTTP/1.1
Host: 127.0.0.1:8901
Content-Type: application/xml
Authorization: AWS4-HMAC-SHA256 ...

<AccessControlPolicy>
  <Owner><ID>1</ID></Owner>
  <AccessControlList>
    <Grant>
      <Grantee xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:type="CanonicalUser">
        <ID>1</ID>
      </Grantee>
      <Permission>FULL_CONTROL</Permission>
    </Grant>
    <Grant>
      <Grantee xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:type="CanonicalUser">
        <ID>42</ID>
      </Grantee>
      <Permission>WRITE</Permission>
    </Grant>
  </AccessControlList>
</AccessControlPolicy>
```

### 授予所有已认证用户读取权限

允许任何已认证的 StoreFS 用户读取桶中的内容：

```http
PUT /mybucket?acl HTTP/1.1
...
<Grant>
  <Grantee xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:type="Group">
    <URI>http://acs.amazonaws.com/groups/global/AuthenticatedUsers</URI>
  </Grantee>
  <Permission>READ</Permission>
</Grant>
```

### 使用管理 API 设置 ACL

```bash
curl -X PUT http://admin-host:7946/api/buckets/1/acl \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <token>" \
  -d '{
    "grants": [
      {"granteeId": "1", "granteeType": "canonical_user", "permission": "FULL_CONTROL"},
      {"granteeUri": "http://acs.amazonaws.com/groups/global/AllUsers", "granteeType": "all_users", "permission": "READ"}
    ]
  }'
```

## 关键行为说明

1. **原子替换**：设置新 ACL 会替换所有现有条目。没有"添加授权"操作——每次必须提供完整的期望授权集。

2. **所有者始终保留 FULL_CONTROL**：如果请求中没有明确包含所有者的 `FULL_CONTROL` 权限，系统会自动添加。这防止了意外锁定。

3. **ACL 存在触发 ACL 鉴权**：一旦桶设置了 ACL，旧版的 `is_public` 和所有者授权方式将不再使用——仅评估 ACL 条目。要恢复到旧版模式，需删除所有 ACL 条目。

4. **去重**：重复的授权（相同授权对象 + 相同权限）会自动去除。S3 API 和管理 API 中都进行了处理。

5. **超级管理员绕过**：拥有 `super_admin` 角色的用户绕过所有 ACL 检查。

6. **组管理员访问**：组管理员可以访问同组用户拥有的桶（仅旧版回退模式——ACL 存在时不评估）。
