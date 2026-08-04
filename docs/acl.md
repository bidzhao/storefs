# ACL (Access Control List) Documentation

## Overview

StoreFS implements S3-compatible Access Control Lists (ACLs) for bucket-level authorization. ACLs provide a fine-grained permission model that controls what operations different users can perform on a bucket. The system supports both the S3 XML API (GetBucketAcl / PutBucketAcl) and a JSON Admin API for managing ACLs.

## Core Concepts

### Permissions

ACLs define five permission levels:

| Permission | Description | Implied by |
|-----------|-------------|------------|
| `FULL_CONTROL` | Full access to bucket and objects, plus ability to read and write the ACL | — |
| `WRITE` | Write and delete objects in the bucket | `FULL_CONTROL` |
| `READ` | List objects in the bucket and read object contents | `FULL_CONTROL` |
| `READ_ACP` | Read the bucket's ACL | `FULL_CONTROL`, `WRITE_ACP` |
| `WRITE_ACP` | Modify the bucket's ACL | `FULL_CONTROL` |

Permission implication rules:
- `FULL_CONTROL` implies all other permissions (READ, WRITE, READ_ACP, WRITE_ACP)
- `WRITE_ACP` implies `READ_ACP`
- No other cross-implications exist (e.g., `READ` does not imply `WRITE`, and vice versa)
- Permissions must match exactly or be implied by a broader permission

### Grantee Types

Three types of grantees can be specified in an ACL grant:

| Grantee Type | Identifier | Description |
|-------------|-----------|-------------|
| `CanonicalUser` | User ID (numeric) | A specific user identified by their StoreFS user ID |
| `AllUsers` | URI: `http://acs.amazonaws.com/groups/global/AllUsers` | Every request, including anonymous (unauthenticated) requests |
| `AuthenticatedUsers` | URI: `http://acs.amazonaws.com/groups/global/AuthenticatedUsers` | Any authenticated StoreFS user |

## Default ACL

When a new bucket is created, StoreFS automatically assigns a default ACL that grants the bucket owner `FULL_CONTROL`:

```json
[
  {
    "grantee": "<owner_user_id>",
    "grantee_type": "canonical_user",
    "permission": "FULL_CONTROL"
  }
]
```

When no ACLs have been explicitly set for a bucket, the system falls back to **legacy authorization** mode (see below).

## Authorization Flow

The ACL authorization follows this sequence:

```
Request → Identify user → Look up bucket → Check ACLs
                                                ↓
                              ACLs exist? ──→ No ──→ Legacy authorization
                                │ Yes
                                ↓
                    Evaluate each ACL entry
                     ───────────────────
                     Any grantee matches    No ──→ Deny
                     AND permission implied?
                                │ Yes
                                ↓
                              Allow
```

### Legacy Authorization (Fallback)

When no ACLs exist for a bucket, the system uses a legacy authorization check:

| Condition | Anonymous User | Authenticated User |
|-----------|---------------|-------------------|
| Bucket owner | — | Full access |
| Public bucket (`is_public=true`) | READ only | Same as anonymous + owner access |
| Private bucket (`is_public=false`) | Denied | Owner only |
| Group admin | — | Can access buckets owned by users in their group |
| Super admin | — | Always has access |

## S3 API

### GetBucketAcl

Retrieve the ACL for a bucket.

**URL**: `GET /<bucket>?acl`

**Required Permission**: `READ_ACP`

**Request**:
```http
GET /mybucket?acl HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 Credential=<AK>/20230101/us-east-1/s3/aws4_request, SignedHeaders=..., Signature=...
```

**Response** (200 OK):
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

**Error Responses**:
- 404 Not Found: Bucket does not exist
- 403 Forbidden: No `READ_ACP` permission or invalid access key

When no ACLs are set, the default ACL (owner with FULL_CONTROL) is returned.

### PutBucketAcl

Set the ACL for a bucket. This operation **replaces** all existing ACL entries atomically.

**URL**: `PUT /<bucket>?acl`

**Required Permission**: `WRITE_ACP`

**Request**:
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

**Notes**:
- The **Owner** section in the XML is informational — the actual owner is determined by the bucket, not by the request body
- The owner always retains `FULL_CONTROL` automatically. If the request's grant list does not include the owner with `FULL_CONTROL`, it will be added automatically
- Duplicate grants (same grantee + same permission) are automatically deduplicated
- Invalid permissions in the request are silently skipped
- Specifying a non-existent user as a `CanonicalUser` grantee causes that grant to be silently skipped

**Response**:
```http
HTTP/1.1 200 OK
Content-Length: 0
```

**Error Responses**:
- 404 Not Found: Bucket does not exist
- 403 Forbidden: No `WRITE_ACP` permission
- 400 Bad Request: Malformed XML

### ACL Behavior in S3 Operations

All S3 operations on a bucket check the appropriate ACL permission:

| S3 Operation | Permission Checked |
|-------------|-------------------|
| List Buckets | `READ` on each bucket |
| Head Bucket | `READ` |
| List Objects | `READ` |
| Put Object | `WRITE` |
| Get Object | `READ` |
| Delete Object | `WRITE` |
| Copy Object | `READ` (source) + `WRITE` (target) |
| Multipart Upload Init | `WRITE` |
| Upload Part | `WRITE` |
| Complete Multipart | `WRITE` |
| Abort Multipart | `WRITE` |
| GetBucketAcl | `READ_ACP` |
| PutBucketAcl | `WRITE_ACP` |
| GetBucketVersioning | `READ` |
| PutBucketVersioning | `WRITE` |
| GetBucketTagging | `READ` |
| PutBucketTagging | `WRITE` |
| DeleteBucketTagging | `WRITE` |

## Admin API

The Admin API provides JSON-based endpoints for managing bucket ACLs.

### Get Bucket ACL

**URL**: `GET /api/buckets/{id}/acl`

**Required Role**: Bucket owner, group admin, or super admin

**Response** (200 OK):
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

When no ACLs are set, the default ACL is returned as:
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

**Error Responses**:
- 401 Unauthorized: Not authenticated
- 403 Forbidden: Not authorized to view this bucket's ACL
- 404 Not Found: Bucket does not exist

### Set Bucket ACL

**URL**: `PUT /api/buckets/{id}/acl`

**Required Role**: Bucket owner, group admin, or super admin

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

**Request Fields**:

| Field | Required | Description |
|-------|----------|-------------|
| `granteeType` | Yes | `canonical_user`, `all_users`, or `authenticated_users` |
| `granteeId` | For `canonical_user` | Numeric user ID of the grantee |
| `granteeUri` | For group types | URI for `AllUsers` or `AuthenticatedUsers` groups |
| `permission` | Yes | `FULL_CONTROL`, `WRITE`, `READ`, `READ_ACP`, or `WRITE_ACP` |

**Notes**:
- The owner always retains `FULL_CONTROL` automatically (added if not present in the request)
- Duplicate grants (same grantee + same permission) are deduplicated
- Invalid permissions or non-existent user IDs are silently skipped
- This operation **replaces** all existing ACL entries atomically

**Response**:
```json
{
  "status": "ok"
}
```

**Error Responses**:
- 400 Bad Request: Invalid request body
- 401 Unauthorized: Not authenticated
- 403 Forbidden: Not authorized
- 404 Not Found: Bucket does not exist

## Database Schema

ACL entries are stored in the `bucket_acls` table:

```sql
CREATE TABLE IF NOT EXISTS bucket_acls (
    id            BIGINT       NOT NULL COMMENT 'ACL entry unique id',
    bucket_id     BIGINT       NOT NULL COMMENT 'bucket id',
    grantee       VARCHAR(256) NOT NULL COMMENT 'grantee identifier: user ID for canonical_user, or URI for AllUsers/AuthenticatedUsers',
    grantee_type  VARCHAR(32)  NOT NULL COMMENT 'grantee type: canonical_user | all_users | authenticated_users',
    permission    VARCHAR(32)  NOT NULL COMMENT 'S3 permission: FULL_CONTROL | WRITE | READ | READ_ACP | WRITE_ACP',
    created_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX index_bucket_acls_bucket_id (bucket_id) USING BITMAP
) PRIMARY KEY (id)
DISTRIBUTED BY HASH(id) BUCKETS 3;
```

## Usage Examples

### Grant Anonymous Read Access

Make a bucket publicly readable via the S3 API:

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

After this, anonymous users can list objects and read objects in `mybucket`, but cannot write or delete.

### Grant Write Access to Another User

Grant user ID `42` write access to the bucket:

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

### Grant Authenticated Users Read Access

Allow any authenticated StoreFS user to read from the bucket:

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

### Using the Admin API to Set ACLs

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

## Key Behaviors

1. **Atomic replacement**: Setting a new ACL replaces all existing entries. There is no "add grant" operation — the full desired set must be provided each time.

2. **Owner always retains FULL_CONTROL**: If the owner is not explicitly included with `FULL_CONTROL` in the request, it is automatically added. This prevents accidental lockout.

3. **ACL existence triggers ACL-based auth**: Once any ACL is set on a bucket, the legacy `is_public` and owner-based authorization is no longer used — only the ACL entries are evaluated. To revert to legacy mode, delete all ACL entries.

4. **Deduplication**: Duplicate grants (same grantee + same permission) are automatically removed. This is handled both in the S3 API and Admin API.

5. **super_admin bypass**: Users with the `super_admin` role bypass all ACL checks entirely.

6. **Group admin access**: Group administrators can access buckets owned by users in their group (legacy fallback only — not evaluated when ACLs exist).
