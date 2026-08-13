**[English](admin-api.md)**

# Admin API 文档

## 概要

StoreFS 提供了一套 RESTful Admin API，用于管理系统的用户、策略、桶和节点。这些 API 主要用于 Web 管理控制台和自动化运维。

## 基础信息

### 协议和端口

- **协议**：HTTP（生产环境建议使用 HTTPS）
- **默认端口**：与 node port 相同（7963）
- **API 前缀**：`/api`

### 认证方式

Admin API 使用Http Basic Authentication，需要先通过登录接口获取会话令牌（Session Token）。

## 已实现的 API 接口

### 1. 认证接口（Authentication）

#### 1.1 登录（Login）

**URL**：`POST /api/auth/login`

**请求**：
```json
{
  "username": "admin",
  "password": "admin123"
}
```

**响应**：
```json
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "user": {
    "id": "1",
    "name": "admin",
    "role": "admin"
  }
}
```

**错误响应**：
- 401 Unauthorized：用户名或密码错误

#### 1.2 获取当前用户信息（Get User Info）

**URL**：`GET /api/auth/user`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "id": "1",
  "name": "admin",
  "role": "admin",
  "accessKey": "AKIA1234567890",
  "secretKey": "abcdef1234567890"
}
```

#### 1.3 更改密码（Change Password）

**URL**：`PUT /api/auth/change-password`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "oldPassword": "admin123",
  "newPassword": "newpassword"
}
```

**响应**：
```json
{
  "message": "password changed successfully"
}
```

#### 1.4 退出登录（Logout）

登出并吊销当前 JWT 令牌。吊销后，该令牌将无法再用于后续请求。

**URL**: `POST /api/auth/logout`

**请求头**:
```http
Authorization: Bearer <token>
```

**响应 (200)**:
```json
{
  "message": "logged out"
}
```

**响应 (401)** — 未认证或令牌已过期/吊销。

> 注意：`X-New-Access-Token` 滑动续期头在登出时不会发出；被吊销的令牌在所有集群节点上立即失效。

#### 1.5 MFA（多因素认证）

MFA 使用 TOTP（基于时间的一次性密码）提供额外的安全层。启用后，用户在密码登录后必须提供来自身份验证器应用（如 Google Authenticator、Microsoft Authenticator）的验证码。

##### 1.5.1 开启 MFA 设置

生成 TOTP 密钥并返回二维码 URI 和备用码。此时 MFA 尚未激活——需调用验证端点完成激活。

**URL**：`POST /api/auth/mfa/enable`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "success": true,
  "secret": "JBSWY3DPEHPK3PXP",
  "uri": "otpauth://totp/StoreFS:mycluster:admin?secret=JBSWY3DPEHPK3PXP&issuer=StoreFS&algorithm=SHA1&digits=6&period=30",
  "qr": "data:image/png;base64,iVBORw0KGgo...",
  "backupCodes": ["ABCD-EFGH", "IJKL-MNOP", "QRST-UVWX", "YZ12-3456", "7890-ABCD", "EFGH-IJKL", "MNOP-QRST", "UVWX-YZ12"]
}
```

> **注意**：身份验证器应用将条目归在 `StoreFS` 分组下，标签显示为 `<集群名>:<用户名>`（例如 `mycluster:admin`）。这有助于区分不同集群的账号。

##### 1.5.2 验证 MFA 设置

验证 TOTP 码并激活 MFA。

**URL**：`POST /api/auth/mfa/verify`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "code": "123456"
}
```

**响应**：
```json
{
  "success": true,
  "message": "MFA enabled successfully",
  "pat": "stfs_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
}
```

> **注意**：激活后自动生成个人访问令牌（PAT）用于程序化访问。API 调用应使用 PAT 代替密码。

##### 1.5.3 获取 MFA 状态

**URL**：`GET /api/auth/mfa/status`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "mfaEnabled": true,
  "backupCodeCount": 8
}
```

##### 1.5.4 MFA 登录验证

密码登录后，如果用户启用了 MFA，登录响应会返回 `mfaRequired: true`。使用此端点通过 TOTP 码或备用码完成认证。

**URL**：`POST /api/auth/mfa/verify-login`

**请求**：
```json
{
  "token": "temp_jwt_token_from_login",
  "code": "123456"
}
```

**响应**：
```json
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "user": {
    "id": "1",
    "name": "admin",
    "role": "admin"
  }
}
```

##### 1.5.5 关闭 MFA

**URL**：`POST /api/auth/mfa/disable`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "password": "current_password"
}
```

**响应**：
```json
{
  "success": true,
  "message": "MFA disabled successfully"
}
```

##### 1.5.6 重新生成备用码

**URL**：`POST /api/auth/mfa/backup-codes`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "password": "current_password"
}
```

**响应**：
```json
{
  "success": true,
  "backupCodes": ["ABCD-EFGH", "IJKL-MNOP", "..."]
}
```

### 2. 用户管理（User Management）

#### 2.1 获取用户列表（List Users）

**URL**：`GET /api/users`

**请求参数**：
- `page`（可选）：页码，默认 1
- `pageSize`（可选）：每页数量，默认 20，最大 100
- `sortBy`（可选）：排序字段，默认 `created_at`
- `sortOrder`（可选）：排序顺序，`asc` 或 `desc`，默认 `desc`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "users": [
    {
      "id": "1",
      "name": "admin",
      "role": "admin",
      "created_at": "2023-01-01 12:00:00"
    }
  ],
  "total": 1,
  "page": 1,
  "pageSize": 20
}
```

#### 2.2 获取用户详情（Get User）

**URL**：`GET /api/users/:id`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "id": "1",
  "name": "admin",
  "role": "admin",
  "accessKey": "AKIA1234567890",
  "secretKey": "abcdef1234567890",
  "created_at": "2023-01-01 12:00:00"
}
```

#### 2.3 创建用户（Create User）

**URL**：`POST /api/users`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "username": "testuser",
  "password": "password123",
  "role": "user"
}
```

**响应**：
```json
{
  "id": "2",
  "name": "testuser",
  "role": "user",
  "accessKey": "",
  "secretKey": "",
  "created_at": "2023-01-02 10:00:00"
}
```

#### 2.4 更新用户（Update User）

**URL**：`PUT /api/users/:id`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "username": "updateduser",
  "role": "admin"
}
```

**响应**：
```json
{
  "id": "2",
  "name": "updateduser",
  "role": "admin",
  "accessKey": "",
  "secretKey": "",
  "created_at": "2023-01-02 10:00:00"
}
```

#### 2.5 删除用户（Delete User）

**URL**：`DELETE /api/users/:id`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "message": "user deleted successfully"
}
```

#### 2.6 重置密码（Reset Password）

**URL**：`PUT /api/users/:id/reset-password`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "password": "newpassword"
}
```

**响应**：
```json
{
  "message": "password reset successfully"
}
```

#### 2.7 停用 MFA（Disable MFA）

**所需角色**：`super_admin`（任何用户），`group_admin`（仅本组用户）

**URL**：`PUT /api/users/:id/disable-mfa`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "message": "MFA disabled successfully"
}
```

此端点用于停用目标用户的 MFA，清除 TOTP 密钥并删除所有备份恢复码。该用户下次登录时需要重新启用 MFA 并扫描新的二维码。

#### 2.8 管理访问密钥（Access Keys）

##### 2.7.1 获取用户访问密钥（Get Access Keys）

**URL**：`GET /api/users/:id/access-keys`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
[
  {
    "id": "1",
    "accessKey": "AKIA1234567890",
    "secretKey": "abcdef1234567890",
    "createdAt": "2023-01-01 12:00:00"
  }
]
```

##### 2.7.2 创建访问密钥（Create Access Key）

**URL**：`POST /api/users/:id/access-keys`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "id": "1",
  "accessKey": "AKIA9876543210",
  "secretKey": "0987654321fedcba",
  "createdAt": "2023-01-01 12:00:00"
}
```

##### 2.7.3 更新访问密钥（Update Access Key）

**URL**：`PUT /api/users/:id/access-keys`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "accessKey": "AKIA9876543210",
  "secretKey": "0987654321fedcba"
}
```

**响应**：
```json
{
  "id": "1",
  "accessKey": "AKIA9876543210",
  "secretKey": "0987654321fedcba",
  "createdAt": "2023-01-01 12:00:00"
}
```

##### 2.7.4 删除访问密钥（Delete Access Key）

**URL**：`DELETE /api/users/:id/access-keys`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "message": "access keys deleted successfully"
}
```

### 3. 策略管理（Policy Management）

#### 3.1 获取策略列表（List Policies）

**URL**：`GET /api/policies`

**请求参数**：
- `page`（可选）：页码，默认 1
- `pageSize`（可选）：每页数量，默认 20，最大 100
- `sortBy`（可选）：排序字段，默认 `created_at`
- `sortOrder`（可选）：排序顺序，`asc` 或 `desc`，默认 `desc`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "policies": [
    {
      "id": "1",
      "name": "default",
      "type": "replicas",
      "replicas": 2,
      "dataShards": 0,
      "parityShards": 0,
      "createdAt": "2023-01-01 12:00:00"
    }
  ],
  "total": 1,
  "page": 1,
  "pageSize": 20
}
```

#### 3.2 获取策略详情（Get Policy）

**URL**：`GET /api/policies/:id`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "id": "1",
  "name": "default",
  "type": "replicas",
  "replicas": 2,
  "dataShards": 0,
  "parityShards": 0,
  "createdAt": "2023-01-01 12:00:00"
}
```

#### 3.3 创建策略（Create Policy）

**URL**：`POST /api/policies`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "name": "my-policy",
  "type": "ec",
  "replicas": 0,
  "dataShards": 4,
  "parityShards": 2
}
```

**响应**：
```json
{
  "id": "2",
  "name": "my-policy",
  "type": "ec",
  "replicas": 0,
  "dataShards": 4,
  "parityShards": 2,
  "createdAt": "2023-01-02 10:00:00"
}
```

**策略类型说明**：
- `replicas`：副本策略，需要设置 `replicas` 参数
- `ec`：纠删码策略，需要设置 `dataShards` 和 `parityShards` 参数

#### 3.4 更新策略（Update Policy）

**URL**：`PUT /api/policies/:id`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "name": "updated-policy",
  "type": "replicas",
  "replicas": 3,
  "dataShards": 0,
  "parityShards": 0
}
```

**响应**：
```json
{
  "id": "2",
  "name": "updated-policy",
  "type": "replicas",
  "replicas": 3,
  "dataShards": 0,
  "parityShards": 0,
  "createdAt": "2023-01-02 10:00:00"
}
```

#### 3.5 删除策略（Delete Policy）

**URL**：`DELETE /api/policies/:id`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "message": "policy deleted successfully"
}
```

### 4. 桶管理（Bucket Management）

#### 4.1 获取桶列表（List Buckets）

**URL**：`GET /api/buckets`

**请求参数**：
- `page`（可选）：页码，默认 1
- `pageSize`（可选）：每页数量，默认 20，最大 100
- `sortBy`（可选）：排序字段，默认 `created_at`
- `sortOrder`（可选）：排序顺序，`asc` 或 `desc`，默认 `desc`
- `userId`（可选）：按用户过滤（仅超级管理员可用）
- `groupId`（可选）：按用户组过滤（仅超级管理员和组管理员可用）

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "buckets": [
    {
      "id": "1",
      "name": "mybucket",
      "policyId": "1",
      "policyName": "default",
      "policyType": "replicas",
      "ownerId": "1",
      "ownerName": "admin",
      "userPermission": "FULL_CONTROL",
      "canReadAcl": true,
      "canWrite": true,
      "versioning": "Unversioned",
      "isLocked": false,
      "lockMode": "COMPLIANCE",
      "retention": "30",
      "isPublic": false,
      "isEncrypted": true,
      "createdAt": "2023-01-01 12:00:00"
    }
  ],
  "total": 1,
  "page": 1,
  "pageSize": 20
}
```

**新增字段说明**（自 v0.4.0）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `userPermission` | string | 当前用户对桶的有效 S3 权限（`FULL_CONTROL`、`READ, WRITE`、`READ` 或空） |
| `canReadAcl` | bool | 当前用户是否可以读取桶 ACL |
| `canWrite` | bool | 当前用户是否可以写入/删除桶中的对象 |
| `versioning` | string | 版本控制状态：`Unversioned`、`Enabled` 或 `Suspended` |
| `isLocked` | bool | 是否启用对象锁定 |
| `lockMode` | string | 对象锁定模式：`COMPLIANCE` 或 `GOVERNANCE` |
| `retention` | string | 默认保留天数 |
| `isPublic` | bool | 是否启用公开读取 |
| `isEncrypted` | bool | 是否启用服务端加密 |

#### 4.2 获取桶详情（Get Bucket）

**URL**：`GET /api/buckets/:id`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "id": "1",
  "name": "mybucket",
  "policyId": "1",
  "policyName": "default",
  "policyType": "replicas",
  "ownerId": "1",
  "ownerName": "admin",
  "userPermission": "FULL_CONTROL",
  "canReadAcl": true,
  "canWrite": true,
  "versioning": "Unversioned",
  "isLocked": false,
  "lockMode": "COMPLIANCE",
  "retention": "30",
  "isPublic": false,
  "isEncrypted": true,
  "lastUpdatedAt": "2023-01-01 12:00:00",
  "createdAt": "2023-01-01 12:00:00",
  "tags": []
}
```

#### 4.3 创建桶（Create Bucket）

**URL**：`POST /api/buckets`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "name": "newbucket",
  "policyId": "1",
  "ownerId": "1"
}
```

**响应**：
```json
{
  "id": "2",
  "name": "newbucket",
  "policyId": "1",
  "ownerId": "1",
  "createdAt": "2023-01-02 10:00:00"
}
```

#### 4.4 更新桶（Update Bucket）

**URL**：`PUT /api/buckets/:id`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "name": "updatedbucket",
  "policyId": "2",
  "ownerId": "1"
}
```

**响应**：
```json
{
  "id": "2",
  "name": "updatedbucket",
  "policyId": "2",
  "ownerId": "1",
  "createdAt": "2023-01-02 10:00:00"
}
```

#### 4.5 删除桶（Delete Bucket）

**URL**：`DELETE /api/buckets/:id`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "message": "bucket deleted successfully"
}
```

**说明**：只有空桶才能被删除。

#### 4.6 获取桶 ACL（Get Bucket ACL）

**URL**：`GET /api/buckets/:id/acl`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "bucketId": "2081560956567031808",
  "ownerId": "1",
  "ownerName": "admin",
  "grants": [
    {
      "id": "1",
      "granteeId": "1",
      "granteeName": "admin",
      "granteeType": "canonical_user",
      "permission": "FULL_CONTROL"
    },
    {
      "granteeType": "all_users",
      "granteeUri": "http://acs.amazonaws.com/groups/global/AllUsers",
      "permission": "READ"
    }
  ]
}
```

**授权对象类型**：
| granteeType | 说明 |
|-------------|------|
| `canonical_user` | 指定用户（通过 `granteeId` 标识） |
| `all_users` | 所有用户（含匿名），URI: `http://acs.amazonaws.com/groups/global/AllUsers` |
| `authenticated_users` | 任意已认证用户，URI: `http://acs.amazonaws.com/groups/global/AuthenticatedUsers` |

**权限**：`FULL_CONTROL`、`WRITE`、`READ`、`READ_ACP`、`WRITE_ACP`

#### 4.7 设置桶 ACL（Set Bucket ACL）

**URL**：`PUT /api/buckets/:id/acl`

**请求头**：
```http
Authorization: Bearer <token>
Content-Type: application/json
```

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
      "granteeType": "all_users",
      "permission": "READ"
    }
  ]
}
```

**响应**：
```json
{
  "status": "ok"
}
```

**说明**：
- Owner 始终自动保留 `FULL_CONTROL`（请求中缺失时自动添加）
- 重复授权（相同授权对象 + 相同权限）会自动去重
- 设置 ACL 会原子替换所有现有 ACL 条目

**错误响应**：
- 400 Bad Request：无效的桶 ID 或请求体
- 403 Forbidden：无权限
- 404 Not Found：桶不存在

#### 4.8 桶通知管理（Bucket Notification Management）

##### 4.8.1 列出桶通知

**URL**：`GET /api/buckets/:id/notifications`

**所需权限**：对桶有 `READ` 权限

**响应**：
```json
{
  "notifications": [
    {
      "id": "1",
      "bucketId": "1",
      "url": "https://hooks.example.com/webhook",
      "events": "s3:ObjectCreated:*",
      "filterPrefix": "images/",
      "filterSuffix": "",
      "format": "native",
      "enabled": true,
      "retryCount": 10,
      "retryInterval": 1,
      "createdAt": "2025-01-01 12:00:00",
      "lastUpdateAt": "2025-01-01 12:00:00"
    }
  ],
  "total": 1
}
```

##### 4.8.2 创建桶通知

**URL**：`POST /api/buckets/:id/notifications`

**所需权限**：对桶有 `WRITE` 或 `FULL_CONTROL` 权限

**请求**：
```json
{
  "url": "https://hooks.example.com/webhook",
  "secret": "your-hmac-secret",
  "events": "s3:ObjectCreated:Put,s3:ObjectRemoved:Delete",
  "filterPrefix": "images/",
  "filterSuffix": ".jpg",
  "format": "native",
  "enabled": true,
  "retryCount": 10,
  "retryInterval": 1
}
```

**响应**（201 Created）：返回创建的通知对象。

##### 4.8.3 获取通知

**URL**：`GET /api/notifications/:id`

##### 4.8.4 更新通知

**URL**：`PUT /api/notifications/:id`

##### 4.8.5 删除通知

**URL**：`DELETE /api/notifications/:id` 或 `DELETE /api/buckets/:bucketId/notifications/:notificationId`

##### 4.8.6 测试 Webhook

**URL**：`POST /api/notifications/test`

**请求**：
```json
{
  "url": "https://hooks.example.com/webhook",
  "secret": "your-hmac-secret",
  "format": "native"
}
```

**响应**：
```json
{
  "success": true,
  "statusCode": 200,
  "body": "OK"
}
```

详细文档请参考：[通知文档](notification_cn.md)

### 5. 对象管理（Object Management）

#### 5.1 获取桶中的对象列表（List Objects in Bucket）

**URL**：`GET /api/buckets/:id/objects`

**请求参数**：
- `page`（可选）：页码，默认 1
- `pageSize`（可选）：每页数量，默认 20，最大 100
- `sortBy`（可选）：排序字段，默认 `created_at`
- `sortOrder`（可选）：排序顺序，`asc` 或 `desc`，默认 `desc`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "objects": [
    {
      "id": "1",
      "bucketId": "1",
      "policyId": "1",
      "objectName": "test.txt",
      "size": 1024,
      "crc": "abc123",
      "writeTime": "2023-01-01 12:00:00",
      "status": "completed",
      "createdAt": "2023-01-01 12:00:00"
    }
  ],
  "total": 1,
  "page": 1,
  "pageSize": 20
}
```

#### 5.2 删除对象（Delete Object）

**URL**：`DELETE /api/buckets/:id/objects/:name`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "message": "object deleted successfully"
}
```

#### 5.3 生成预签名 URL（Generate Presigned URL）

**URL**：`POST /api/buckets/:id/s3-presigned-url`

**请求头**：
```http
Authorization: Bearer <token>
```

**请求**：
```json
{
  "method": "PUT",
  "bucketId": "1",
  "objectName": "test.txt",
  "contentType": "text/plain",
  "host": "127.0.0.1"
}
```

**响应**：
```json
{
  "url": "http://127.0.0.1:7963/mybucket/test.txt?X-Amz-Algorithm=AWS4-HMAC-SHA256&...",
  "host": "127.0.0.1:7963"
}
```

**说明**：生成的预签名 URL 有效期为 5 分钟。

### 6. 用户组管理（Group Management）

#### 6.1 列出用户组

**URL**：`GET /api/groups`

**说明**：
- 超级管理员看到所有用户组 + 虚拟的"超级管理员组"（id=0）
- 组管理员看到自己的组 + 通过 ACL 可访问的桶所属的组
- 普通用户看到自己的组 + 通过 ACL 可访问的桶所属的组

**响应**：
```json
{
  "groups": [
    {
      "id": "1",
      "name": "default",
      "defaultPolicyId": "1",
      "createdAt": "2023-01-01 12:00:00"
    }
  ],
  "total": 1
}
```

#### 6.2 获取用户组详情

**URL**：`GET /api/groups/:id`

#### 6.3 创建用户组

**URL**：`POST /api/groups`

**请求**：
```json
{
  "name": "engineering",
  "defaultPolicyId": 1
}
```

#### 6.4 更新用户组

**URL**：`PUT /api/groups/:id`

#### 6.5 删除用户组

**URL**：`DELETE /api/groups/:id`

### 7. 节点管理（Node Management）

#### 7.1 获取集群节点状态

**URL**：`GET /api/node/status`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "nodes": [
    {
      "id": "1",
      "name": "node1",
      "ip": "127.0.0.1",
      "port": 7946,
      "status": "active",
      "disks": [
        {
          "path": "/disk1",
          "weight": 1,
          "totalCapacity": 107374182400,
          "usedCapacity": 53687091200
        }
      ]
    }
  ]
}
```

#### 7.2 获取节点污点状态

**URL**：`GET /api/node-status`

**响应**：
```json
{
  "nodes": [
    {
      "nodeName": "node1",
      "status": "active",
      "operator": 1,
      "lastUpdateAt": "2025-01-01 12:00:00"
    },
    {
      "nodeName": "node2",
      "status": "taint",
      "operator": 1,
      "lastUpdateAt": "2025-01-01 12:05:00"
    }
  ]
}
```

#### 7.3 更新节点污点状态

**URL**：`PUT /api/node-status/:nodeName`

**请求**：
```json
{
  "status": "taint"
}
```

**可选值**：`active`（正常运行）、`taint`（阻止新数据写入）。需要 `super_admin` 角色。

### 8. 健康检查（Health Check）

**URL**：`GET /api/health`

**响应**：
```json
{
  "status": "healthy"
}
```

**说明**：此接口无需认证，用于监控系统健康状态。

### 9. KMS 管理

KMS（密钥管理服务）提供了与外部密钥管理服务的集成。当前支持 KMIP（密钥管理互操作协议）标准（KMIP 1.2+），后续计划支持 AWS KMS、Azure Key Vault、GCP KMS 和 HashiCorp Vault。

系统支持同时配置多个 KMS 服务。每个 KMS 服务有唯一的名称，KMS 密钥通过 `kmsConfigId` 关联到特定的 KMS 服务。

#### 9.1 获取主 KMS 配置（旧版）

**所需角色**：`super_admin`

**URL**：`GET /api/kms/config`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "id": "1",
  "name": "primary-kms",
  "description": "主 KMS 服务器",
  "endpoint": "192.168.1.100:5696",
  "provider": "kmip",
  "username": "admin",
  "password": "****",
  "clientCert": "****",
  "clientKey": "****",
  "caCert": "****",
  "timeout": 10,
  "healthCheckInterval": 30,
  "allowDegradedReads": false,
  "createdAt": "2026-01-01 12:00:00",
  "updatedAt": "2026-01-01 12:00:00"
}
```

**响应字段**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | string | KMS 配置 ID |
| `name` | string | 配置名称 |
| `description` | string | 配置描述 |
| `endpoint` | string | KMS 服务器地址（host:port） |
| `provider` | string | KMS 提供商类型（`kmip`，未来支持 `aws`/`azure`/`gcp`/`vault`） |
	| `username` | string | KMS 认证用户名（KMIP 提供商使用） |
| `password` | string | KMS 认证密码（KMIP 提供商使用） |
| `clientCert` | string | TLS 客户端证书 PEM 内容 |
| `clientKey` | string | TLS 客户端密钥 PEM 内容 |
| `caCert` | string | CA 证书 PEM 内容 |
| `timeout` | int | KMS 请求超时时间（秒） |
| `healthCheckInterval` | int | 健康检查间隔（秒） |
| `allowDegradedReads` | bool | 当 KMS 离线时是否允许使用过期缓存读取 |
| `createdAt` | string | 创建时间戳 |
| `updatedAt` | string | 最后更新时间戳 |

**错误响应**：
- 200：如果未配置 KMS，返回 `{"message": "no KMS config configured"}`

#### 9.2 更新 KMS 配置

**所需角色**：`super_admin`

**URL**：`PUT /api/kms/config`

**请求头**：
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**请求体**：
```json
{
  "endpoint": "192.168.1.100:5696",
  "provider": "kmip",
  "username": "admin",
  "password": "secret",
  "clientCert": "-----BEGIN CERTIFICATE-----\n...",
  "clientKey": "-----BEGIN PRIVATE KEY-----\n...",
  "caCert": "-----BEGIN CERTIFICATE-----\n...",
  "timeout": 10,
  "healthCheckInterval": 30,
  "allowDegradedReads": false
}
```

**请求字段**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `endpoint` | string | 是 | KMS 服务器地址（host:port） |
| `provider` | string | 否 | KMS 提供商类型（`kmip`，默认 `kmip`） |
| `providerConfig` | object | 否 | 提供商专用配置（JSON 对象，如云 KMS 的 region 等） |
| `username` | string | 否 | KMS 认证用户名（KMIP 提供商使用） |
| `password` | string | 否 | KMS 认证密码（KMIP 提供商使用） |
| `clientCert` | string | 否 | TLS 客户端证书 PEM 内容 |
| `clientKey` | string | 否 | TLS 客户端密钥 PEM 内容 |
| `caCert` | string | 否 | CA 证书 PEM 内容 |
| `timeout` | int | 否 | KMS 请求超时时间（秒，默认 10） |
| `healthCheckInterval` | int | 否 | 健康检查间隔（秒，默认 30） |
| `allowDegradedReads` | bool | 否 | KMS 离线时允许使用过期缓存读取（默认 false） |

**注意**：保存前会测试连接。如果测试失败，配置不会被保存。

**响应**：
```json
{
  "message": "KMS config updated successfully"
}
```

**错误响应**：
- 200：`{"error": "connection test failed: ..."}` 如果 KMS 端点不可达
- 200：`{"warning": "config saved but reconnect failed: ..."}` 如果保存成功但重连失败

#### 9.3 列出 KMS 配置

**URL**：`GET /api/kms/configs`

**所需角色**：`super_admin`

**请求头**：
```http
Authorization: Bearer <token>
```

**查询参数**：
- `includeInactive`（可选）：设为 `true` 包含非活跃（已软删除）的配置

**响应**：
```json
{
  "configs": [
    {
      "id": "1",
      "name": "primary-kms",
      "description": "主 KMS 服务器",
      "provider": "kmip",
      "endpoint": "192.168.1.100:5696",
      "username": "admin",
      "password": "****",
      "clientCert": "****",
      "clientKey": "****",
      "caCert": "****",
      "timeout": 10,
      "healthCheckInterval": 30,
      "allowDegradedReads": false,
      "createdAt": "2026-01-01 12:00:00",
      "updatedAt": "2026-01-01 12:00:00"
    }
  ],
  "total": 1
}
```

#### 9.4 创建 KMS 配置

**URL**：`POST /api/kms/configs`

**所需角色**：`super_admin`

**请求头**：
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**请求体**：
```json
{
  "name": "my-kms",
  "description": "我的 KMS 服务器",
  "endpoint": "192.168.1.100:5696",
  "provider": "kmip",
  "username": "admin",
  "password": "secret",
  "clientCert": "-----BEGIN CERTIFICATE-----\n...",
  "clientKey": "-----BEGIN PRIVATE KEY-----\n...",
  "caCert": "-----BEGIN CERTIFICATE-----\n...",
  "timeout": 10,
  "healthCheckInterval": 30,
  "allowDegradedReads": false
}
```

**请求字段**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 唯一的配置名称 |
| `description` | string | 否 | 配置描述 |
| `endpoint` | string | 是 | KMS 服务器地址（host:port） |
| `provider` | string | 否 | KMS 提供商类型（`kmip`，默认 `kmip`） |
| `providerConfig` | object | 否 | 提供商专用配置（JSON 对象） |
| `username` | string | 否 | KMS 认证用户名 |
| `password` | string | 否 | KMS 认证密码 |
| `clientCert` | string | 否 | TLS 客户端证书 PEM 内容 |
| `clientKey` | string | 否 | TLS 客户端密钥 PEM 内容 |
| `caCert` | string | 否 | CA 证书 PEM 内容 |
| `timeout` | int | 否 | 请求超时时间（秒，默认 10） |
| `healthCheckInterval` | int | 否 | 健康检查间隔（秒，默认 30） |
| `allowDegradedReads` | bool | 否 | KMS 离线时允许使用过期缓存读取（默认 false） |

**注意**：保存前会测试连接。如果测试失败，配置不会被保存。

**响应**：
```json
{
  "message": "KMS config created successfully",
  "id": "1"
}
```

#### 9.5 获取指定 ID 的 KMS 配置

**URL**：`GET /api/kms/configs/{id}`

**所需角色**：`super_admin`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "id": "1",
  "name": "my-kms",
  "description": "我的 KMS 服务器",
  "provider": "kmip",
  "endpoint": "192.168.1.100:5696",
  "username": "admin",
  "password": "****",
  "clientCert": "****",
  "clientKey": "****",
  "caCert": "****",
  "timeout": 10,
  "healthCheckInterval": 30,
  "allowDegradedReads": false,
  "createdAt": "2026-01-01 12:00:00",
  "updatedAt": "2026-01-01 12:00:00"
}
```

**错误响应**：
- 200：`{"error": "KMS config not found"}` 如果配置不存在

#### 9.6 更新指定 ID 的 KMS 配置

**URL**：`PUT /api/kms/configs/{id}`

**所需角色**：`super_admin`

**请求体**（所有字段可选，仅更新指定字段）：
```json
{
  "name": "my-kms-updated",
  "description": "更新后的描述",
  "endpoint": "192.168.1.101:5696",
  "timeout": 15
}
```

**响应**：
```json
{
  "message": "KMS config updated successfully"
}
```

#### 9.7 删除 KMS 配置

**URL**：`DELETE /api/kms/configs/{id}`

**所需角色**：`super_admin`

**错误响应**：
- 200：`{"error": "cannot delete KMS config: N key(s) still reference this config"}` 如果配置仍有密钥引用
- 200：`{"message": "KMS config deleted successfully"}` 删除成功

**注意**：删除 KMS 配置前，必须先删除所有引用该配置的 KMS 密钥。

#### 9.8 测试 KMS 连接（旧版）

**所需角色**：`super_admin`

**URL**：`POST /api/kms/config/test`

**请求头**：
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**请求体**：
```json
{
  "endpoint": "192.168.1.100:5696",
  "provider": "kmip",
  "username": "admin",
  "password": "secret",
  "clientCert": "-----BEGIN CERTIFICATE-----\n...",
  "clientKey": "-----BEGIN PRIVATE KEY-----\n...",
  "caCert": "-----BEGIN CERTIFICATE-----\n...",
  "timeout": 10
}
```

**响应**：
```json
{
  "success": true,
  "message": "KMS connection test successful"
}
```

**错误响应**：
- 200：`{"success": false, "message": "..."}` 如果连接测试失败

#### 9.9 检查 KMS 健康状态

**所需角色**：任何已认证用户

**URL**：`GET /api/kms/config/health`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "status": "online",
  "provider": "kmip",
  "endpoint": "192.168.1.100:5696",
  "lastCheck": "2026-01-01 12:00:00"
}
```

**响应字段**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | `online`（在线）或 `offline`（离线） |
| `provider` | string | KMS 提供商类型（当前支持 `kmip`，后续可扩展） |
| `endpoint` | string | KMS 服务器地址 |
| `lastCheck` | string | 上次健康检查时间戳 |

#### 9.10 列出 KMS 密钥

**所需角色**：任何已认证用户（super_admin 可见所有密钥，普通用户可见全局密钥，group_admin 可见全局密钥 + 本组密钥）

**URL**：`GET /api/kms/keys`

**请求参数**：
- `page`（可选）：页码，默认 1
- `pageSize`（可选）：每页数量，默认 20，最大 100
- `groupId`（可选）：按组 ID 过滤（0=全局密钥）
- `showRetired`（可选，仅 super_admin）：设为 `true` 时包含已退役的密钥（默认 `false`）

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "keys": [
    {
      "id": "1",
      "kmsConfigId": "1",
      "keyId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
      "alias": "my-cmk",
      "description": "我的第一个 CMK",
      "provider": "kmip",
      "groupId": "0",
      "groupName": "",
      "status": "enabled",
      "createdAt": "2026-01-01 12:00:00",
      "updatedAt": "2026-01-01 12:00:00"
    },
    {
      "id": "2",
      "kmsConfigId": "1",
      "keyId": "f6e5d4c3-b2a1-0987-6543-210fedcba987",
      "alias": "my-cmk (rotated 2026-08-06 14:30:00)",
      "description": "我的第一个 CMK",
      "provider": "kmip",
      "groupId": "0",
      "groupName": "",
      "status": "retired",
      "retiredAt": "2026-08-06 14:30:00",
      "createdAt": "2026-01-01 12:00:00",
      "updatedAt": "2026-08-06 14:30:00"
    }
  ],
  "total": 2
}
```

**响应字段**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | string | 数据库 ID |
| `keyId` | string | KMS 密钥唯一标识符 |
| `alias` | string | 密钥的友好别名 |
| `description` | string | 可选描述 |
| `provider` | string | 提供商类型（当前支持 `kmip`，后续可扩展） |
| `groupId` | string | 组ID："0"=全局密钥，>"0"=组密钥 |
| `groupName` | string | 组名称（响应中自动填充，为空时表示未关联组） |
| `status` | string | 密钥状态：`enabled`、`disabled`、`retired`、`pre-active`、`compromised` 或 `destroyed` |
| `retiredAt` | string | 退役时间（仅当 `status` 为 `retired` 时返回） |
| `createdAt` | string | 创建时间戳 |
| `updatedAt` | string | 最后更新时间戳 |

#### 9.11 创建 KMS 密钥

**URL**：`POST /api/kms/keys`

**所需角色**：`super_admin`（可以为任何组创建密钥），`group_admin`（只能在本组创建密钥）

**请求头**：
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**请求体**：
```json
{
  "alias": "my-cmk",
  "description": "我的第一个客户主密钥",
  "groupId": "0"
}
```

**请求字段**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `alias` | string | 是 | 密钥的友好别名 |
| `description` | string | 否 | 可选描述 |
| `groupId` | string | 否 | 组ID（"0"=全局密钥，默认 "0"） |

**响应**：
```json
{
  "id": "1",
  "keyId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "alias": "my-cmk",
  "description": "我的第一个客户主密钥",
  "provider": "kmip",
  "groupId": "0",
  "groupName": "",
  "status": "enabled",
  "createdAt": "2026-01-01 12:00:00",
  "updatedAt": "2026-01-01 12:00:00"
}
```

**错误响应**：
- 400 Bad Request：缺少 `alias`
- 403 Forbidden：权限不足
- 503 Service Unavailable：KMS 客户端未初始化

#### 9.12 获取 KMS 密钥

**所需角色**：任何已认证用户（仅可见自己有权限的密钥）

**URL**：`GET /api/kms/keys/{keyId}`

**请求头**：
```http
Authorization: Bearer <token>
```

**响应**：
```json
{
  "id": "1",
  "keyId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "alias": "my-cmk",
  "description": "我的第一个客户主密钥",
  "provider": "kmip",
  "groupId": "0",
  "groupName": "",
  "status": "enabled",
  "createdAt": "2026-01-01 12:00:00",
  "updatedAt": "2026-01-01 12:00:00"
}
```

**错误响应**：
- 404 Not Found：密钥不存在

#### 9.13 更新 KMS 密钥

**所需角色**：`super_admin`（任何密钥），`group_admin`（仅本组密钥）

**URL**：`PUT /api/kms/keys/{keyId}`

**URL 参数**：
- `keyId`：KMS 密钥的数据库 ID

**请求头**：
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**请求体**：
```json
{
  "status": "disabled",
  "description": "更新后的描述"
}
```

**请求字段**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `status` | string | 否 | 新的密钥状态：`enabled` 或 `disabled`（注意：`retired` 状态只能通过密钥轮转设置） |
| `description` | string | 否 | 更新后的描述 |

**注意**：当 `status` 设置为 `disabled` 时，密钥在 KMS 中被吊销。设置为 `enabled` 时，密钥被重新激活。

**响应**：
```json
{
  "id": "1",
  "keyId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "alias": "my-cmk",
  "description": "更新后的描述",
  "provider": "kmip",
  "groupId": "0",
  "groupName": "",
  "status": "disabled",
  "createdAt": "2026-01-01 12:00:00",
  "updatedAt": "2026-01-01 12:00:00"
}
```

#### 9.14 删除 KMS 密钥

**所需角色**：`super_admin`（任何密钥），`group_admin`（仅本组密钥）

**URL**：`DELETE /api/kms/keys/{keyId}`

**URL 参数**：
- `keyId`：KMS 密钥的数据库 ID

**请求头**：
```http
Authorization: Bearer <token>
```

**注意**：如果密钥当前被一个或多个桶使用，无法删除。删除前会进行检查。

**响应**：
```json
{
  "message": "KMS key deleted"
}
```

**错误响应**：
- 409 Conflict：密钥被一个或多个桶使用
- 404 Not Found：密钥不存在
- 503 Service Unavailable：KMS 客户端未初始化

#### 9.15 轮转 KMS 密钥

**所需角色**：`super_admin`（任何密钥），`group_admin`（仅本组密钥）

**URL**：`POST /api/kms/keys/{keyId}/rotate`

**URL 参数**：
- `keyId`：KMS 密钥的数据库 ID

**请求头**：
```http
Authorization: Bearer <token>
```

**注意**：轮转操作在 KMS 中创建 CMK 的新版本（KMIP 提供商使用 `ReKey` 操作）。使用之前密钥版本加密的现有对象仍然可读。旧密钥的别名会添加时间戳后缀（如 `my-cmk (rotated 2026-08-06 14:30:00)`），状态变为 `retired`（已退役）。新密钥继承原别名。

**响应**：
```json
{
  "message": "KMS key rotated successfully",
  "newKey": {
    "id": "2",
    "kmsConfigId": "1",
    "keyId": "b2c3d4e5-f6a7-8901-bcde-f123456789ab",
    "alias": "my-cmk",
    "description": "我的第一个客户主密钥",
    "provider": "kmip",
    "groupId": "0",
    "groupName": "",
    "status": "enabled",
    "createdAt": "2026-08-06 14:30:00",
    "updatedAt": "2026-08-06 14:30:00"
  }
}
```

**错误响应**：
- 404 Not Found：密钥不存在
- 503 Service Unavailable：KMS 客户端未初始化

## 错误响应格式

所有错误响应都采用统一格式：

```json
{
  "error": "error message"
}
```

常见的 HTTP 状态码：

- 400 Bad Request：无效的请求参数
- 401 Unauthorized：未认证或认证失败
- 403 Forbidden：无权限
- 404 Not Found：资源不存在
- 500 Internal Server Error：内部错误

## 使用示例

### 使用 curl 调用 API

```bash
# 登录
curl -X POST http://127.0.0.1:7963/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"admin123"}'

# 获取用户列表（使用登录返回的 token）
curl -X GET http://127.0.0.1:7963/api/users \
  -H "Authorization: Bearer <token>"
```

### 使用 JavaScript 调用 API

```javascript
// 登录
const response = await fetch('http://127.0.0.1:7963/api/auth/login', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json',
  },
  body: JSON.stringify({
    username: 'admin',
    password: 'admin123'
  })
});

const data = await response.json();
const token = data.token;

// 获取用户列表
const usersResponse = await fetch('http://127.0.0.1:7963/api/users', {
  headers: {
    'Authorization': `Bearer ${token}`
  }
});

const usersData = await usersResponse.json();
console.log(usersData.users);
```

