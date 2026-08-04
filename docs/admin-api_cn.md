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

#### 2.7 管理访问密钥（Access Keys）

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

