**[查看中文版](admin-api_cn.md)**

# Admin API Documentation

## Overview

StoreFS provides a RESTful Admin API for managing the system's users, policies, buckets, and nodes. These APIs are primarily used for web management consoles and automated operations.

## Basic Information

### Protocol and Port

- **Protocol**: HTTP (HTTPS is recommended for production environments)
- **Default Port**: Same as node port (7963)
- **API Prefix**: `/api`

### Authentication Method

Admin API uses Http Basic Authentication, requiring a session token to be obtained first through the login interface.

## Implemented API Interfaces

### 1. Authentication

#### 1.1 Login

**URL**: `POST /api/auth/login`

**Request**:
```json
{
  "username": "admin",
  "password": "admin123"
}
```

**Response**:
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

**Error Response**:
- 401 Unauthorized: Incorrect username or password

#### 1.2 Get Current User Info

**URL**: `GET /api/auth/user`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "id": "1",
  "name": "admin",
  "role": "admin",
  "accessKey": "AKIA1234567890",
  "secretKey": "abcdef1234567890"
}
```

#### 1.3 Change Password

**URL**: `PUT /api/auth/change-password`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "oldPassword": "admin123",
  "newPassword": "newpassword"
}
```

**Response**:
```json
{
  "message": "password changed successfully"
}
```

### 2. User Management

#### 2.1 List Users

**URL**: `GET /api/users`

**Request Parameters**:
- `page` (optional): Page number, default 1
- `pageSize` (optional): Number per page, default 20, max 100
- `sortBy` (optional): Sort field, default `created_at`
- `sortOrder` (optional): Sort order, `asc` or `desc`, default `desc`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

#### 2.2 Get User

**URL**: `GET /api/users/:id`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

#### 2.3 Create User

**URL**: `POST /api/users`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "username": "testuser",
  "password": "password123",
  "role": "user"
}
```

**Response**:
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

#### 2.4 Update User

**URL**: `PUT /api/users/:id`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "username": "updateduser",
  "role": "admin"
}
```

**Response**:
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

#### 2.5 Delete User

**URL**: `DELETE /api/users/:id`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "message": "user deleted successfully"
}
```

#### 2.6 Reset Password

**URL**: `PUT /api/users/:id/reset-password`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "password": "newpassword"
}
```

**Response**:
```json
{
  "message": "password reset successfully"
}
```

#### 2.7 Access Key Management

##### 2.7.1 Get User Access Keys

**URL**: `GET /api/users/:id/access-keys`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

##### 2.7.2 Create Access Key

**URL**: `POST /api/users/:id/access-keys`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "id": "1",
  "accessKey": "AKIA9876543210",
  "secretKey": "0987654321fedcba",
  "createdAt": "2023-01-01 12:00:00"
}
```

##### 2.7.3 Update Access Key

**URL**: `PUT /api/users/:id/access-keys`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "accessKey": "AKIA9876543210",
  "secretKey": "0987654321fedcba"
}
```

**Response**:
```json
{
  "id": "1",
  "accessKey": "AKIA9876543210",
  "secretKey": "0987654321fedcba",
  "createdAt": "2023-01-01 12:00:00"
}
```

##### 2.7.4 Delete Access Key

**URL**: `DELETE /api/users/:id/access-keys`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "message": "access keys deleted successfully"
}
```

### 3. Policy Management

#### 3.1 List Policies

**URL**: `GET /api/policies`

**Request Parameters**:
- `page` (optional): Page number, default 1
- `pageSize` (optional): Number per page, default 20, max 100
- `sortBy` (optional): Sort field, default `created_at`
- `sortOrder` (optional): Sort order, `asc` or `desc`, default `desc`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

#### 3.2 Get Policy

**URL**: `GET /api/policies/:id`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

#### 3.3 Create Policy

**URL**: `POST /api/policies`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "name": "my-policy",
  "type": "ec",
  "replicas": 0,
  "dataShards": 4,
  "parityShards": 2
}
```

**Response**:
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

**Policy Type Description**:
- `replicas`: Replica policy, requires setting the `replicas` parameter
- `ec`: Erasure coding policy, requires setting `dataShards` and `parityShards` parameters

#### 3.4 Update Policy

**URL**: `PUT /api/policies/:id`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "name": "updated-policy",
  "type": "replicas",
  "replicas": 3,
  "dataShards": 0,
  "parityShards": 0
}
```

**Response**:
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

#### 3.5 Delete Policy

**URL**: `DELETE /api/policies/:id`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "message": "policy deleted successfully"
}
```

### 4. Bucket Management

#### 4.1 List Buckets

**URL**: `GET /api/buckets`

**Request Parameters**:
- `page` (optional): Page number, default 1
- `pageSize` (optional): Number per page, default 20, max 100
- `sortBy` (optional): Sort field, default `created_at`
- `sortOrder` (optional): Sort order, `asc` or `desc`, default `desc`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

**New Fields** (since v0.4.0):

| Field | Type | Description |
|-------|------|-------------|
| `userPermission` | string | Current user's effective S3 permission on the bucket (`FULL_CONTROL`, `READ, WRITE`, `READ`, or empty) |
| `canReadAcl` | bool | Whether the current user can read the bucket ACL |
| `canWrite` | bool | Whether the current user can write/delete objects in the bucket |
| `versioning` | string | Versioning status: `Unversioned`, `Enabled`, or `Suspended` |
| `isLocked` | bool | Whether object lock is enabled |
| `lockMode` | string | Object lock mode: `COMPLIANCE` or `GOVERNANCE` |
| `retention` | string | Default retention period in days |
| `isPublic` | bool | Whether public read access is enabled |
| `isEncrypted` | bool | Whether server-side encryption is enabled |

#### 4.2 Get Bucket

**URL**: `GET /api/buckets/:id`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

#### 4.3 Create Bucket

**URL**: `POST /api/buckets`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "name": "newbucket",
  "policyId": "1",
  "ownerId": "1"
}
```

**Response**:
```json
{
  "id": "2",
  "name": "newbucket",
  "policyId": "1",
  "ownerId": "1",
  "createdAt": "2023-01-02 10:00:00"
}
```

#### 4.4 Update Bucket

**URL**: `PUT /api/buckets/:id`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "name": "updatedbucket",
  "policyId": "2",
  "ownerId": "1"
}
```

**Response**:
```json
{
  "id": "2",
  "name": "updatedbucket",
  "policyId": "2",
  "ownerId": "1",
  "createdAt": "2023-01-02 10:00:00"
}
```

#### 4.5 Delete Bucket

**URL**: `DELETE /api/buckets/:id`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "message": "bucket deleted successfully"
}
```

**Note**: Only empty buckets can be deleted.

#### 4.6 Get Bucket ACL

**URL**: `GET /api/buckets/:id/acl`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

**Grantee Types**:
| granteeType | Description |
|-------------|-------------|
| `canonical_user` | Specific user identified by `granteeId` |
| `all_users` | All users (including anonymous), URI: `http://acs.amazonaws.com/groups/global/AllUsers` |
| `authenticated_users` | Any authenticated user, URI: `http://acs.amazonaws.com/groups/global/AuthenticatedUsers` |

**Permissions**: `FULL_CONTROL`, `WRITE`, `READ`, `READ_ACP`, `WRITE_ACP`

#### 4.7 Set Bucket ACL

**URL**: `PUT /api/buckets/:id/acl`

**Request Header**:
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**Request**:
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

**Response**:
```json
{
  "status": "ok"
}
```

**Notes**:
- Owner always retains `FULL_CONTROL` automatically (added if missing)
- Duplicate grants (same grantee + permission) are deduplicated
- Replaces all existing ACL entries atomically

**Error Responses**:
- 400 Bad Request: Invalid bucket ID or request body
- 403 Forbidden: No permission
- 404 Not Found: Bucket does not exist

#### 4.8 Bucket Notification Management

##### 4.8.1 List Bucket Notifications

**URL**: `GET /api/buckets/:id/notifications`

**Required Permission**: `READ` or `FULL_CONTROL` on the bucket

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

##### 4.8.2 Create Bucket Notification

**URL**: `POST /api/buckets/:id/notifications`

**Required Permission**: `WRITE` or `FULL_CONTROL` on the bucket

**Request**:
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

**Response** (201 Created): Returns the created notification object.

##### 4.8.3 Get Notification

**URL**: `GET /api/notifications/:id`

**Required Permission**: `READ` or `FULL_CONTROL` on the associated bucket

##### 4.8.4 Update Notification

**URL**: `PUT /api/notifications/:id`

**Required Permission**: `WRITE` or `FULL_CONTROL` on the associated bucket

**Request**: All fields optional — only provided fields are updated.

##### 4.8.5 Delete Notification

**URL**: `DELETE /api/notifications/:id` or `DELETE /api/buckets/:bucketId/notifications/:notificationId`

##### 4.8.6 Test Webhook

**URL**: `POST /api/notifications/test`

**Request**:
```json
{
  "url": "https://hooks.example.com/webhook",
  "secret": "your-hmac-secret",
  "format": "native"
}
```

**Response**:
```json
{
  "success": true,
  "statusCode": 200,
  "body": "OK"
}
```

For detailed documentation, refer to: [Notification Documentation](notification.md)

### 5. Object Management

#### 5.1 List Objects in Bucket

**URL**: `GET /api/buckets/:id/objects`

**Request Parameters**:
- `page` (optional): Page number, default 1
- `pageSize` (optional): Number per page, default 20, max 100
- `sortBy` (optional): Sort field, default `created_at`
- `sortOrder` (optional): Sort order, `asc` or `desc`, default `desc`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

#### 5.2 Delete Object

**URL**: `DELETE /api/buckets/:id/objects/:name`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "message": "object deleted successfully"
}
```

#### 5.3 Generate Presigned URL

**URL**: `POST /api/buckets/:id/s3-presigned-url`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "method": "PUT",
  "bucketId": "1",
  "objectName": "test.txt",
  "contentType": "text/plain",
  "host": "127.0.0.1"
}
```

**Response**:
```json
{
  "url": "http://127.0.0.1:7963/mybucket/test.txt?X-Amz-Algorithm=AWS4-HMAC-SHA256&...",
  "host": "127.0.0.1:7963"
}
```

**Note**: The generated presigned URL is valid for 5 minutes.

### 6. Group Management

#### 6.1 List Groups

**URL**: `GET /api/groups`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

**Notes**:
- Super admins see all groups plus a virtual "Super Admin Group" (id=0)
- Group admins see their own group plus groups from ACL-accessible buckets
- Regular users see their own group plus groups from ACL-accessible buckets

#### 6.2 Get Group

**URL**: `GET /api/groups/:id`

#### 6.3 Create Group

**URL**: `POST /api/groups`

**Request**:
```json
{
  "name": "engineering",
  "defaultPolicyId": 1
}
```

#### 6.4 Update Group

**URL**: `PUT /api/groups/:id`

**Request**:
```json
{
  "name": "engineering-v2",
  "defaultPolicyId": 2
}
```

#### 6.5 Delete Group

**URL**: `DELETE /api/groups/:id`

### 7. Node Management

#### 7.1 Get Cluster Node Status

**URL**: `GET /api/node/status`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

#### 7.2 Get Node Taint Status

**URL**: `GET /api/node-status`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
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

#### 7.3 Update Node Taint Status

**URL**: `PUT /api/node-status/:nodeName`

**Request**:
```json
{
  "status": "taint"
}
```

**Values**: `active` (normal operation), `taint` (prevent new data writes). Requires `super_admin` role.

### 8. Health Check

**URL**: `GET /api/health`

**Response**:
```json
{
  "status": "healthy"
}
```

**Note**: This interface does not require authentication and is used for monitoring system health status.

## Error Response Format

All error responses follow a uniform format:

```json
{
  "error": "error message"
}
```

Common HTTP status codes:

- 400 Bad Request: Invalid request parameters
- 401 Unauthorized: Unauthenticated or authentication failed
- 403 Forbidden: No permission
- 404 Not Found: Resource does not exist
- 500 Internal Server Error: Internal error

## Usage Examples

### Calling API with curl

```bash
# Login
curl -X POST http://127.0.0.1:7963/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"admin123"}'

# Get user list (using token from login)
curl -X GET http://127.0.0.1:7963/api/users \
  -H "Authorization: Bearer <token>"
```

### Calling API with JavaScript

```javascript
// Login
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

// Get user list
const usersResponse = await fetch('http://127.0.0.1:7963/api/users', {
  headers: {
    'Authorization': `Bearer ${token}`
  }
});

const usersData = await usersResponse.json();
console.log(usersData.users);
```

