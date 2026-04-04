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
      "createdAt": "2023-01-01 12:00:00"
    }
  ],
  "total": 1,
  "page": 1,
  "pageSize": 20
}
```

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
  "createdAt": "2023-01-01 12:00:00"
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

### 6. Node Management

#### 6.1 Get Node Status

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

### 7. Health Check

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

## Unimplemented Features

Currently, the following Admin API features are not yet implemented:

- Bucket policies and ACL management
- Object version control
- Data replication and migration
- Storage usage statistics
- Audit logs
- Node configuration updates

These features will be gradually implemented in future versions.
