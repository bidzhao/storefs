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

#### 1.4 Logout

Logs out and revokes the current JWT token. After revocation, the token can no longer be used for subsequent requests.

**URL**: `POST /api/auth/logout`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response (200)**:
```json
{
  "message": "logged out"
}
```

**Response (401)** — Authentication required or token already expired/revoked.

> Note: The `X-New-Access-Token` sliding renewal header is not issued on logout; the revoked token becomes invalid immediately on all cluster nodes.

#### 1.5 MFA (Multi-Factor Authentication)

MFA adds an extra layer of security using TOTP (Time-based One-Time Password). When enabled, users must provide a verification code from an authenticator app (e.g., Google Authenticator, Microsoft Authenticator) after password login.

##### 1.5.1 Enable MFA Setup

Generates a TOTP secret and returns a QR code URI along with backup codes. MFA is not yet active at this stage — call the verify endpoint to complete activation.

**URL**: `POST /api/auth/mfa/enable`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "success": true,
  "secret": "JBSWY3DPEHPK3PXP",
  "uri": "otpauth://totp/StoreFS:mycluster:admin?secret=JBSWY3DPEHPK3PXP&issuer=StoreFS&algorithm=SHA1&digits=6&period=30",
  "qr": "data:image/png;base64,iVBORw0KGgo...",
  "backupCodes": ["ABCD-EFGH", "IJKL-MNOP", "QRST-UVWX", "YZ12-3456", "7890-ABCD", "EFGH-IJKL", "MNOP-QRST", "UVWX-YZ12"]
}
```

> **Note**: The authenticator app groups the account under `StoreFS` and displays the label as `<clustername>:<username>` (e.g., `mycluster:admin`). This distinguishes accounts across different clusters.

##### 1.5.2 Verify MFA Setup

Verifies a TOTP code and activates MFA for the user.

**URL**: `POST /api/auth/mfa/verify`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "code": "123456"
}
```

**Response**:
```json
{
  "success": true,
  "message": "MFA enabled successfully",
  "pat": "stfs_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
}
```

> **Note**: A Personal Access Token (PAT) is auto-generated for programmatic access. Use the PAT instead of password for API calls that require authentication.

##### 1.5.3 Get MFA Status

**URL**: `GET /api/auth/mfa/status`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "mfaEnabled": true,
  "backupCodeCount": 8
}
```

##### 1.5.4 Verify Login with MFA

After password login, if MFA is enabled, the login response returns `mfaRequired: true`. Use this endpoint to complete authentication with a TOTP code or backup code.

**URL**: `POST /api/auth/mfa/verify-login`

**Request**:
```json
{
  "token": "temp_jwt_token_from_login",
  "code": "123456"
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

##### 1.5.5 Disable MFA

**URL**: `POST /api/auth/mfa/disable`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "password": "current_password"
}
```

**Response**:
```json
{
  "success": true,
  "message": "MFA disabled successfully"
}
```

##### 1.5.6 Recreate Backup Codes

**URL**: `POST /api/auth/mfa/backup-codes`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Request**:
```json
{
  "password": "current_password"
}
```

**Response**:
```json
{
  "success": true,
  "backupCodes": ["ABCD-EFGH", "IJKL-MNOP", "..."]
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

#### 2.7 Disable MFA

**Required Role**: `super_admin` (any user), `group_admin` (own group users only)

**URL**: `PUT /api/users/:id/disable-mfa`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "message": "MFA disabled successfully"
}
```

This endpoint disables MFA for the target user, clears the TOTP secret, and deletes all backup codes. The user will need to re-enable MFA and scan a new QR code on their next login.

#### 2.8 Access Key Management

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

#### 4.9 Bucket Lifecycle Management

##### 4.9.1 Get Bucket Lifecycle Rules

**URL**: `GET /api/buckets/:id/lifecycle`

**Required Permission**: `READ` or `FULL_CONTROL` on the bucket

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "rules": [
    {
      "id": "2087104432666841088",
      "bucket_id": "2087104436177473536",
      "rule_id": "ExpireLogs",
      "status": "Enabled",
      "filter_prefix": "logs/",
      "expiration_days": 30
    }
  ]
}
```

##### 4.9.2 Set Bucket Lifecycle Rules

**URL**: `PUT /api/buckets/:id/lifecycle`

**Required Permission**: `WRITE` or `FULL_CONTROL` on the bucket

**Request Header**:
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**:
```json
{
  "rules": [
    {
      "rule_id": "ExpireLogs",
      "status": "Enabled",
      "filter_prefix": "logs/",
      "expiration_days": 30
    }
  ]
}
```

**Response**: `{"status": "ok"}`

##### 4.9.3 Delete Bucket Lifecycle Rules

**URL**: `DELETE /api/buckets/:id/lifecycle`

**Required Permission**: `WRITE` or `FULL_CONTROL` on the bucket

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**: `{"status": "ok"}`

#### 4.10 Lifecycle Scanner Configuration

Global lifecycle scanner settings. **Super admin only**.

##### 4.10.1 Get Scanner Config

**URL**: `GET /api/lifecycle/config`

**Required Permission**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "enabled": true,
  "scan_interval": "1h0m0s"
}
```

##### 4.10.2 Update Scanner Config

**URL**: `PUT /api/lifecycle/config`

**Required Permission**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**:
```json
{
  "enabled": true,
  "scan_interval": "1h"
}
```

**Response**: `{"status": "ok"}`

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

### 9. KMS Management

KMS (Key Management Service) provides integration with external key management services. Currently supports the KMIP (Key Management Interoperability Protocol) standard (KMIP 1.2+), with future support planned for AWS KMS, Azure Key Vault, GCP KMS, and HashiCorp Vault. It enables SSE-KMS (Server-Side Encryption with KMS-Managed Keys) as an additional encryption mode.

The system supports configuring multiple KMS services simultaneously. Each KMS service has a unique name, and KMS keys are associated with a specific KMS service via `kmsConfigId`.

> **Note**: KMS management is role-aware. KMS configuration endpoints require `super_admin` role. KMS key management (create/delete/rotate) requires `super_admin` or `group_admin` (for their own group's keys). All authenticated users can view keys and check KMS health.

#### 9.1 Get Primary KMS Configuration (Legacy)

**URL**: `GET /api/kms/config`

**Note**: This is the legacy endpoint for backward compatibility. It returns the primary (first active) KMS configuration. Use `GET /api/kms/configs` to list all configurations.

**Required Role**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "id": "1",
  "name": "primary-kms",
  "description": "Primary KMS server",
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

**Response Fields**:

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | KMS config ID |
| `name` | string | Friendly config name |
| `description` | string | Config description |
| `endpoint` | string | KMS server address (host:port) |
| `provider` | string | KMS provider type (`kmip`, future: `aws`, `azure`, `gcp`, `vault`) |
| `username` | string | KMS auth username (for KMIP provider) |
| `password` | string | KMS auth password (for KMIP provider) |
| `clientCert` | string | TLS client certificate PEM content |
| `clientKey` | string | TLS client key PEM content |
| `caCert` | string | CA certificate PEM content |
| `timeout` | int | KMS request timeout in seconds |
| `healthCheckInterval` | int | Health check interval in seconds |
| `allowDegradedReads` | bool | Allow reads with expired BK cache when KMS is offline |
| `createdAt` | string | Creation timestamp |
| `updatedAt` | string | Last update timestamp |

**Error Responses**:
- 200: Returns `{"message": "no KMS config configured"}` if no config exists

#### 9.2 Update KMS Configuration

**URL**: `PUT /api/kms/config`

**Required Role**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**:
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

**Request Fields**:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `endpoint` | string | Yes | KMS server address (host:port) |
| `provider` | string | No | KMS provider type (`kmip`, future: `aws`, `azure`, `gcp`, `vault`; default: `kmip`) |
| `providerConfig` | object | No | Provider-specific configuration as JSON object |
| `username` | string | No | KMS auth username (for KMIP provider) |
| `password` | string | No | KMS auth password (for KMIP provider) |
| `clientCert` | string | No | TLS client certificate PEM content |
| `clientKey` | string | No | TLS client key PEM content |
| `caCert` | string | No | CA certificate PEM content |
| `timeout` | int | No | KMS request timeout in seconds (default: 10) |
| `healthCheckInterval` | int | No | Health check interval in seconds (default: 30) |
| `allowDegradedReads` | bool | No | Allow reads with expired cache when KMS is offline (default: false) |

**Note**: The connection is tested before saving. If the test fails, the configuration is not saved.

**Response**:
```json
{
  "message": "KMS config updated successfully"
}
```

**Error Responses**:
- 200: `{"error": "connection test failed: ..."}` if the KMS endpoint is unreachable
- 200: `{"warning": "config saved but reconnect failed: ..."}` if saved but reconnect failed

#### 9.3 List KMS Configs

**URL**: `GET /api/kms/configs`

**Required Role**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Query Parameters**:
- `includeInactive` (optional): Set to `true` to include inactive (soft-deleted) configs

**Response**:
```json
{
  "configs": [
    {
      "id": "1",
      "name": "primary-kms",
      "description": "Primary KMS server",
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

#### 9.4 Create KMS Config

**URL**: `POST /api/kms/configs`

**Required Role**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**:
```json
{
  "name": "my-kms",
  "description": "My KMS server",
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

**Request Fields**:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Unique config name |
| `description` | string | No | Config description |
| `endpoint` | string | Yes | KMS server address (host:port) |
| `provider` | string | No | KMS provider type (`kmip`, default: `kmip`) |
| `providerConfig` | object | No | Provider-specific configuration as JSON object |
| `username` | string | No | KMS auth username |
| `password` | string | No | KMS auth password |
| `clientCert` | string | No | TLS client certificate PEM content |
| `clientKey` | string | No | TLS client key PEM content |
| `caCert` | string | No | CA certificate PEM content |
| `timeout` | int | No | KMS request timeout in seconds (default: 10) |
| `healthCheckInterval` | int | No | Health check interval in seconds (default: 30) |
| `allowDegradedReads` | bool | No | Allow reads with expired cache when KMS is offline (default: false) |

**Note**: The connection is tested before saving. If the test fails, the configuration is not saved.

**Response**:
```json
{
  "message": "KMS config created successfully",
  "id": "1"
}
```

#### 9.5 Get KMS Config by ID

**URL**: `GET /api/kms/configs/{id}`

**Required Role**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "id": "1",
  "name": "my-kms",
  "description": "My KMS server",
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

**Error Responses**:
- 200: `{"error": "KMS config not found"}` if the config does not exist

#### 9.6 Update KMS Config by ID

**URL**: `PUT /api/kms/configs/{id}`

**Required Role**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body** (all fields optional — only specified fields are updated):
```json
{
  "name": "my-kms-updated",
  "description": "Updated description",
  "endpoint": "192.168.1.101:5696",
  "timeout": 15
}
```

**Response**:
```json
{
  "message": "KMS config updated successfully"
}
```

#### 9.7 Delete KMS Config

**URL**: `DELETE /api/kms/configs/{id}`

**Required Role**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Error Responses**:
- 200: `{"error": "cannot delete KMS config: N key(s) still reference this config"}` if the config has key references
- 200: `{"message": "KMS config deleted successfully"}` on success

**Note**: Before deleting a KMS config, all KMS keys referencing it must be deleted or migrated to another config.

#### 9.8 Test KMS Connection (Legacy)

**URL**: `POST /api/kms/config/test`

**Required Role**: `super_admin`

**Request Header**:
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**:
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

**Response**:
```json
{
  "success": true,
  "message": "KMS connection test successful"
}
```

**Error Responses**:
- 200: `{"success": false, "message": "..."}` if the connection test fails

#### 9.9 Check KMS Health

**URL**: `GET /api/kms/config/health`

**Required Role**: Any authenticated user (returns only online/offline status)

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "status": "online",
  "provider": "kmip",
  "endpoint": "192.168.1.100:5696",
  "lastCheck": "2026-01-01 12:00:00"
}
```

**Response Fields**:

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | `online` or `offline` |
| `provider` | string | KMS provider type (`kmip`, future: `aws`, `azure`, `gcp`, `vault`) |
| `endpoint` | string | KMS server address |
| `lastCheck` | string | Last health check timestamp |

#### 9.10 List KMS Keys

**URL**: `GET /api/kms/keys`

**Required Role**: `super_admin` (sees all keys, optional `groupId` filter), `group_admin`/`user` (sees global + own group keys)

**Request Parameters**:
- `page` (optional): Page number, default 1
- `pageSize` (optional): Number per page, default 20, max 100
- `groupId` (optional, super_admin only): Filter by group ID
- `showRetired` (optional, super_admin only): Set to `true` to include retired keys in the results (default: `false`)

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "keys": [
    {
      "id": "1",
      "kmsConfigId": "1",
      "keyId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
      "alias": "my-cmk",
      "description": "My first CMK",
      "provider": "kmip",
      "groupId": "0",
      "groupName": "",
      "status": "enabled",
      "createdAt": "2026-01-01 12:00:00",
      "updatedAt": "2026-01-01 12:00:00"
    },
    {
      "id": "2",
      "keyId": "f6e5d4c3-b2a1-0987-6543-210fedcba987",
      "alias": "my-cmk (rotated 2026-08-06 14:30:00)",
      "description": "My first CMK",
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

**Response Fields**:

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Database ID |
| `keyId` | string | KMS Key Unique Identifier |
| `alias` | string | User-friendly alias for the key |
| `description` | string | Optional description |
| `provider` | string | Provider type (`kmip`, future: `aws`, `azure`, `gcp`, `vault`) |
| `groupId` | string | Group ID: "0"=global key, >"0"=group key |
| `groupName` | string | Group display name (empty for global keys) |
| `status` | string | Key status: `enabled`, `disabled`, `retired`, `pre-active`, `compromised`, or `destroyed` |
| `retiredAt` | string | Retirement timestamp (present only when `status` is `retired`) |
| `createdAt` | string | Creation timestamp |
| `updatedAt` | string | Last update timestamp |

#### 9.11 Create KMS Key

**URL**: `POST /api/kms/keys`

**Required Role**: `super_admin` (can create any group's key), `group_admin` (can only create keys in their own group)

**Request Header**:
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**:
```json
{
  "alias": "my-cmk",
  "description": "My first Customer Master Key"
}
```

**Request Fields**:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `alias` | string | Yes | User-friendly alias for the key |
| `description` | string | No | Optional description |
| `groupId` | string | No | Group ID: "0"=global key (super_admin only), omit for group_admin to auto-assign |
| `kmsConfigId` | string | No | KMS config ID to use (optional, defaults to primary config) |

**Response**:
```json
{
  "id": "1",
  "kmsConfigId": "1",
  "keyId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "alias": "my-cmk",
  "description": "My first Customer Master Key",
  "provider": "kmip",
  "groupId": "0",
  "groupName": "",
  "status": "enabled",
  "createdAt": "2026-01-01 12:00:00",
  "updatedAt": "2026-01-01 12:00:00"
}
```

**Error Responses**:
- 400 Bad Request: Missing `alias`
- 403 Forbidden: Insufficient permissions
- 503 Service Unavailable: KMS client not initialized

#### 9.12 Get KMS Key

**Required Role**: Any authenticated user (only sees keys visible to their role)

**URL**: `GET /api/kms/keys/{keyId}`

**Request Header**:
```http
Authorization: Bearer <token>
```

**Response**:
```json
{
  "id": "1",
  "kmsConfigId": "1",
  "keyId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "alias": "my-cmk",
  "description": "My first Customer Master Key",
  "provider": "kmip",
  "groupId": "0",
  "groupName": "",
  "status": "enabled",
  "createdAt": "2026-01-01 12:00:00",
  "updatedAt": "2026-01-01 12:00:00"
}
```

**Error Responses**:
- 404 Not Found: Key does not exist

#### 9.13 Update KMS Key

**Required Role**: `super_admin` (any key), `group_admin` (own group keys only)

**URL**: `PUT /api/kms/keys/{keyId}`

**URL Parameters**:
- `keyId`: Database ID of the KMS key

**Request Header**:
```http
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**:
```json
{
  "status": "disabled",
  "description": "Updated description"
}
```

**Request Fields**:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `status` | string | No | New key status: `enabled` or `disabled` (note: `retired` status can only be set by key rotation) |
| `description` | string | No | Updated description |

**Note**: When `status` is set to `disabled`, the key is revoked in KMS. When set to `enabled`, the key is re-activated.

**Response**:
```json
{
  "id": "1",
  "keyId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "alias": "my-cmk",
  "description": "Updated description",
  "provider": "kmip",
  "groupId": "0",
  "groupName": "",
  "status": "disabled",
  "createdAt": "2026-01-01 12:00:00",
  "updatedAt": "2026-01-01 12:00:00"
}
```

#### 9.14 Delete KMS Key

**Required Role**: `super_admin` (any key), `group_admin` (own group keys only)

**URL**: `DELETE /api/kms/keys/{keyId}`

**URL Parameters**:
- `keyId`: Database ID of the KMS key

**Request Header**:
```http
Authorization: Bearer <token>
```

**Note**: The key cannot be deleted if it is currently in use by one or more buckets. The check is performed before deletion.

**Response**:
```json
{
  "message": "KMS key deleted"
}
```

**Error Responses**:
- 409 Conflict: Key is in use by one or more buckets
- 404 Not Found: Key does not exist
- 503 Service Unavailable: KMS client not initialized

#### 9.15 Rotate KMS Key

**Required Role**: `super_admin` (any key), `group_admin` (own group keys only)

**URL**: `POST /api/kms/keys/{keyId}/rotate`

**URL Parameters**:
- `keyId`: Database ID of the KMS key

**Request Header**:
```http
Authorization: Bearer <token>
```

**Note**: Rotation creates a new version of the CMK in KMS. For the KMIP provider, this uses the `ReKey` operation. Existing objects encrypted with the previous key version remain readable. The old key's alias is updated with a timestamp suffix (e.g., `my-cmk (rotated 2026-08-06 14:30:00)`) and its status is set to `retired`. The new key inherits the original alias.

**Response**:
```json
{
  "message": "KMS key rotated successfully",
  "newKey": {
    "id": "2",
    "kmsConfigId": "1",
    "keyId": "b2c3d4e5-f6a7-8901-bcde-f123456789ab",
    "alias": "my-cmk",
    "description": "My first Customer Master Key",
    "provider": "kmip",
    "groupId": "0",
    "groupName": "",
    "status": "enabled",
    "createdAt": "2026-08-06 14:30:00",
    "updatedAt": "2026-08-06 14:30:00"
  }
}
```

**Error Responses**:
- 404 Not Found: Key does not exist
- 503 Service Unavailable: KMS client not initialized

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

