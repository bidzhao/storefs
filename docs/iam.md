**[查看中文版](iam_cn.md)**

# IAM (Identity and Access Management) Documentation

## Overview

StoreFS provides an AWS-IAM-compatible identity and access management service. It lets you manage **users**, **groups**, **policies**, **roles (with trust policies)**, **OIDC identity providers**, and **accounts**, and to control access to S3 buckets and objects through identity policies, bucket policies, and ACLs.

```
Manage identities ── users / groups / roles / policies / OIDC providers / accounts
        ↓
Attach permissions ── inline policies (user/group/role) + AWS managed policies
        ↓
Enforce on S3 ── identity policy → bucket policy → ACL (Deny > Allow > fallback)
        ↓
Use credentials ── IAM access keys (or STS temporary credentials) for S3 requests
```

Two management planes are provided:

- **S3 IAM Query API** — the AWS IAM API protocol (`POST` + `Action=`), used by tools such as `botocore`/`boto3`. Request types are auto-routed to the IAM service.
- **Web Console / Admin REST API** — StoreFS's own `GET/POST/PUT/DELETE /api/iam/*` endpoints used by the web console.

Both planes share the same identity data; changes made through one are visible through the other.

## Core Concepts

| Concept | Description |
|---------|-------------|
| **Account** | A root identity that owns users, groups, roles, and policies. In a single-tenant StoreFS deployment everything belongs to the default account (`000000000000`); the account scope is used to isolate IAM entities. |
| **User** | An identity with a name and optional path/email. Users get credentials by creating **access keys** (up to 2 per user). A user has **no implicit permissions** unless policies grant them. |
| **Group** | A named set of users. Permissions attached to a group apply to all of its members. |
| **Policy** | A JSON document granting or denying actions. Used as **inline** policies (embedded in a user/group/role) or **managed** policies (AWS built-in, attached by ARN). |
| **Role** | An identity that is not tied to a specific user. It carries a **trust policy** (who may assume it) and can have its own inline/attached permissions policies. Assumed via STS ([AssumeRole](sts.md#assumerole)). |
| **OIDC Identity Provider** | A trusted OpenID Connect issuer (e.g. Keycloak, Dex) whose ID tokens can be used to assume roles via `AssumeRoleWithWebIdentity`. |
| **Access Key** | Long-term credentials (`AccessKeyId` + `SecretAccessKey`) issued to a user. `AKIA...`-prefixed ID, base64 secret, status `Active`/`Inactive`. |

## IAM Users/Groups vs. Legacy Users/Groups

StoreFS distinguishes two identity systems: **IAM** (AWS-style) identities and **legacy** StoreFS identities. They coexist; a single deployment can have both.

### Identity type

Every user carries an identity type, set at creation and **immutable** afterwards:

| | IAM | Legacy |
|---|-----|--------|
| Identity type | `iam` | `legacy` |
| Managed via | IAM API / Admin REST `/api/iam/*` / Web Console | original StoreFS Admin API / Web Console |
| Credentials | **IAM access keys** (`iam_access_keys`, up to 2 per user, `Active`/`Inactive`) | the user's own `accesskey` column |
| IAM visibility | listed by `ListIAMUsers` and the IAM admin view | **not** visible to the IAM admin view (legacy admins/super-admins are excluded) |
| Role | `user` (no `urole` hierarchy) | StoreFS roles: `user`, `group_admin`, `super_admin` |
| Implicit permissions | **none** — must be granted via policies/ACLs | legacy implicit permissions (owner `FULL_CONTROL`, group-admin rules, `super_admin` bypass) |

### Authentication order

When a request signs with an access key, the lookup is:

1. **IAM access keys** (canonical): the key must exist and be `Active`; an **inactive** IAM key is rejected immediately (`InvalidClientTokenId`) and never falls back to legacy.
2. **Legacy fallback**: `users.accesskey` is consulted **only** if the key does not exist in `iam_access_keys` at all.

So an IAM-managed user and a legacy user can never shadow each other's credentials: the canonical IAM lookup always wins.

### Authorization differences

- An **IAM user** (authenticated via `iam_access_keys`) or a **role session** is an *IAM principal*: it has **no implicit S3 permissions**. Access requires an explicit identity-policy `Allow`, a same-account bucket-policy `Allow`, or an explicit ACL grant. Even `s3:CreateBucket` needs an explicit `Allow`.
- A **legacy user** (authenticated via `users.accesskey`) keeps the legacy implicit model: bucket owners have `FULL_CONTROL`, group administrators have `FULL_CONTROL` over buckets owned by users in their group, and `super_admin` bypasses bucket policies.
- An IAM user that is also the **account root** (or a legacy `super_admin`) is exempt from the no-implicit-permission rule and keeps administrative privileges.

### IAM administration rights

The ability to manage IAM identities is gated separately:

- legacy `super_admin` (identity type `legacy`, role `super_admin`) and the **account root** can perform any IAM management action;
- an **IAM user** needs an identity policy allowing the corresponding `iam:<Action>`, and Web Console IAM administration additionally requires the `storefs:console:identity:manage` action (via the `StoreFSConsoleAdmin` managed policy).

### IAM groups vs. legacy groups

Groups live in the same store, but are told apart by **path**:

| | IAM group | Legacy group |
|---|-----------|--------------|
| Path | anything **other than `/`** (e.g. `/storefs/`, `/engineering/`) | `/` |
| IAM visibility | listed by `ListIAMGroups` / IAM admin view | excluded from the IAM view (`NoSuchEntity` from the IAM perspective) |
| Default path | `/storefs/` (never `/`) | `/` |
| Members | IAM users | StoreFS users (by `group_id`) |

Consequences to keep in mind:

- Creating an IAM group always stores a path other than `/` (the default is `/storefs/`); setting an explicit path preserves it.
- Updating a group's path to `/` would make it disappear from the IAM view (treated as a legacy group), so the IAM update path normalizes `/` back to `/storefs/`.
- IAM group policies and memberships are managed with the same APIs as IAM users (inline policies, attached managed policies, `AddUserToGroup`/`RemoveUserFromGroup`).

## Users

Users are created with a name and an optional path; names are **case-insensitive** (uniqueness is checked case-insensitively) and must be 1–64 characters using `[A-Za-z0-9+=,.@_-]`. `UserId` is a numeric ID (Snowflake-style). Creating a user does **not** create an access key (as in AWS).

### Create a user (S3 IAM Query API)

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=CreateUser&Version=2010-05-08&UserName=alice&Path=/engineering/
```

### Create a user (Admin REST API)

```
POST /api/iam/users
Authorization: Bearer <token>
Content-Type: application/json

{ "name": "alice", "path": "/engineering/", "email": "alice@example.com" }
```

Response: the created user DTO (`id`, `userName`, `path`, `arn`, `identityType`, `createdAt`).

### User management APIs

| Admin REST API | Description |
|----------------|-------------|
| `GET /api/iam/users?page=&pageSize=` | List IAM users (paginated, sortable) |
| `GET /api/iam/users/{id}` | Get a user (id or name) |
| `PUT /api/iam/users/{id}` | Update `path`/`email` |
| `DELETE /api/iam/users/{id}` | Delete a user (cascades access keys, policies, memberships) |

S3 IAM Query actions: `CreateUser`, `GetUser`, `UpdateUser`, `DeleteUser`, `ListUsers`, `CreateAccessKey`, `UpdateAccessKey`, `DeleteAccessKey`, `ListAccessKeys`, plus `PutUserPolicy`/`GetUserPolicy`/`DeleteUserPolicy`/`ListUserPolicies`, `AttachUserPolicy`/`DetachUserPolicy`/`ListAttachedUserPolicies`, `AddUserToGroup`/`RemoveUserFromGroup`/`ListGroupsForUser`.

### Access keys

- A user may have **at most 2** access keys.
- The **secret is returned only once** at creation time; afterwards only metadata (`AccessKeyId`, `Status`, `CreateDate`) is listed.
- Keys can be deactivated (`Active` → `Inactive`); an inactive key is rejected with `InvalidClientTokenId`.
- Deleting a user removes its keys.

```
POST /api/iam/users/alice/access-keys
→ { "accessKey": "AKIA...", "secretAccessKey": "<one-time>", "status": "Active" }

PUT /api/iam/users/alice/access-keys/{keyId}   { "status": "Inactive" }
DELETE /api/iam/users/alice/access-keys/{keyId}
GET  /api/iam/users/alice/access-keys
```

## Groups

Groups are named collections of users; group permissions apply to all members. Group names are case-insensitive. The StoreFS web/Admin plane distinguishes IAM groups (path other than `/`, e.g. `/storefs/`) from legacy groups (`path = /`).

### Group management APIs

| Admin REST API | Description |
|----------------|-------------|
| `POST /api/iam/groups` | Create group `{name, path?}` |
| `GET /api/iam/groups?page=&pageSize=` | List groups |
| `GET /api/iam/groups/{id}` | Get a group |
| `PUT /api/iam/groups/{id}` | Update path |
| `DELETE /api/iam/groups/{id}` | Delete group (cascades memberships, inline/attached policies) |
| `GET /api/iam/groups/{id}/users` | List members |
| `PUT /api/iam/groups/{id}/users/{userId}` | Add a user to the group |
| `DELETE /api/iam/groups/{id}/users/{userId}` | Remove a user from the group |
| `PUT/DELETE /api/iam/groups/{id}/policies/{name}` | Manage group inline policies |
| `POST/DELETE /api/iam/groups/{id}/attached-policies` | Attach/detach managed policies |

S3 IAM Query actions: `CreateGroup`, `GetGroup`, `UpdateGroup`, `DeleteGroup`, `ListGroups`, `AddUserToGroup`, `RemoveUserFromGroup`, `ListGroupsForUser`, `PutGroupPolicy`/`GetGroupPolicy`/`DeleteGroupPolicy`/`ListGroupPolicies`, `AttachGroupPolicy`/`DetachGroupPolicy`/`ListAttachedGroupPolicies`.

Deleting a group that still contains users, inline policies, or attached policies fails with `DeleteConflict` (as in AWS).

## Policies

Policies are JSON documents with `Version`, `Statement[]` (each with `Effect`, `Action`/`NotAction`, `Resource`/`NotResource`, and optional `Principal`/`Condition`). Evaluation is **three-valued**: `Deny > Allow > NoMatch` — an explicit `Deny` always wins, an explicit `Allow` permits, and no match falls back to the next layer.

There are two kinds:

- **Inline policies** — attached directly to a user/group/role. Policy names are 1–128 characters. An identity policy **must not** contain `Principal`.
- **Managed policies** — AWS built-in policies (`AmazonS3FullAccess`, `AmazonS3ReadOnlyAccess`, `IAMReadOnlyAccess`, `AdministratorAccess`, `PowerUserAccess`) identified by ARN (`arn:aws:iam::aws:policy/...`) and attached by ARN. Built-ins are read-only (cannot be created/updated/deleted) but attachable. StoreFS also ships a protected console policy `arn:storefs:iam::storefs:policy/StoreFSConsoleAdmin`.

### Example user inline policy

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": ["s3:GetObject", "s3:ListBucket"],
      "Resource": "arn:aws:s3:::data-bucket/*"
    }
  ]
}
```

### Policy APIs

| Admin REST API | Description |
|----------------|-------------|
| `GET /api/iam/policies?filter=aws\|customer\|all` | List managed policies |
| `GET /api/iam/policies/{name}` | Get a managed policy (name or ARN) |
| `PUT /api/iam/users\|groups\|roles/{id}/policies/{name}` | Put an inline policy |
| `GET /api/iam/users\|groups\|roles/{id}/policies/{name}` | Get an inline policy |
| `DELETE /api/iam/users\|groups\|roles/{id}/policies/{name}` | Delete an inline policy |
| `POST /api/iam/users\|groups\|roles/{id}/attached-policies` | Attach a managed policy |
| `DELETE /api/iam/users\|groups\|roles/{id}/attached-policies?policyArn=` | Detach a managed policy |

S3 IAM Query actions: `PutUserPolicy`/`GetUserPolicy`/`DeleteUserPolicy`/`ListUserPolicies` and their group/role counterparts, `AttachUserPolicy`/`DetachUserPolicy`/`ListAttachedUserPolicies` and their group/role counterparts.

### How effective permissions are computed

When an S3 request is authorized, the effective identity policy of the caller is assembled from:

1. the user's inline policies,
2. inline policies of every group the user belongs to,
3. AWS managed policies attached to the user and to the user's groups,
4. for a role session: the role's inline policies and attached managed policies (a role session is authorized **only** by the role's policies, never by the original user's).

`aws:PrincipalTag`, `s3:ResourceTag`, `s3:RequestObjectTag`, `s3:ExistingObjectTag`, and OIDC-derived claims are supported in policy `Condition`s (ABAC).

## Roles

A role is a cross-identity you can assume to obtain temporary credentials (see [AssumeRole](sts.md#assumerole) in the STS documentation). Each role has:

- `RoleName` (case-insensitive unique), `Path`, `Description`
- `AssumeRolePolicyDocument` — the **trust policy** declaring who may assume the role (account root, IAM users, services, or federated OIDC principals)
- `MaxSessionDuration` — maximum session length in seconds (default 3600)
- optional **tags**, usable in trust-policy conditions (`iam:ResourceTag/...`) and ABAC

### Create a role

```
POST /api/iam/roles
Content-Type: application/json

{
  "roleName": "app-role",
  "path": "/applications/",
  "trustPolicy": {
    "Version": "2012-10-17",
    "Statement": [
      {
        "Effect": "Allow",
        "Principal": {"AWS": "arn:aws:iam::000000000000:root"},
        "Action": "sts:AssumeRole"
      }
    ]
  },
  "maxSessionDuration": 3600
}
```

> `trustPolicy` must be a valid policy document; otherwise the creation is rejected.

The S3 IAM Query equivalents are `CreateRole`, `GetRole`, `UpdateRole`, `DeleteRole`, `ListRoles`, `UpdateAssumeRolePolicy`.

### Role APIs

| Admin REST API | Description |
|----------------|-------------|
| `GET /api/iam/roles?page=&pageSize=` | List roles |
| `GET /api/iam/roles/{id}` | Get a role |
| `PUT /api/iam/roles/{id}` | Update description / maxSessionDuration |
| `DELETE /api/iam/roles/{id}` | Delete a role (fails with `DeleteConflict` if it still has inline/attached policies) |
| `GET /api/iam/roles/{id}/trust-policy` | Read the trust policy |
| `PUT /api/iam/roles/{id}/trust-policy` | Update the trust policy |
| `PUT/DELETE /api/iam/roles/{id}/policies/{name}` | Manage role inline policies |
| `POST/DELETE /api/iam/roles/{id}/attached-policies` | Attach/detach managed policies |

Deleting a role requires removing its policies first (inline and attached), mirroring AWS behavior.

## OIDC Identity Providers

To use web identity federation ([`AssumeRoleWithWebIdentity`](sts.md#assumerolewithwebidentity-web-identity-federation)), register the OIDC issuer that issues the ID tokens:

- **URL** — an absolute `http(s)` URL (no userinfo/query/fragment). Only `http`/`https` schemes are accepted.
- **Client IDs** — the OIDC client IDs allowed to authenticate (used to validate the token `aud`).
- **Thumbprints** — the expected TLS certificate thumbprints of the issuer.

### Register an OIDC provider

```
POST /api/iam/oidc-providers
Content-Type: application/json

{
  "url": "https://auth.example.com/realms/storefs",
  "clientIds": ["storefs-app"],
  "thumbprints": ["c8e9..."]
}
```

### OIDC APIs

| Admin REST API | Description |
|----------------|-------------|
| `GET /api/iam/oidc-providers` | List providers |
| `GET /api/iam/oidc-providers/{id}` | Get a provider |
| `POST /api/iam/oidc-providers` | Create a provider |
| `DELETE /api/iam/oidc-providers/{id}` | Delete a provider (fails if referenced by a role trust policy) |
| `PUT/DELETE .../oidc-providers/{id}/client-ids` | Add/remove client IDs (Query API: `AddClientIDToOpenIDConnectProvider`, `RemoveClientIDFromOpenIDConnectProvider`) |
| `PUT .../oidc-providers/{id}/thumbprints` | Update thumbprints (`UpdateOpenIDConnectProviderThumbprint`) |

S3 IAM Query actions: `CreateOpenIDConnectProvider`, `ListOpenIDConnectProviders`, `GetOpenIDConnectProvider`, `DeleteOpenIDConnectProvider`, plus the client-id/thumbprint actions above.

Once registered, a role's trust policy can grant the federated principal:

```json
{
  "Effect": "Allow",
  "Principal": {"Federated": "arn:aws:iam::000000000000:oidc-provider/auth.example.com/realms/storefs"},
  "Action": "sts:AssumeRoleWithWebIdentity",
  "Condition": {"StringEquals": {"{iss}/app_id": "storefs-app"}}
}
```

See [AssumeRoleWithWebIdentity (Web Identity Federation)](sts.md#assumerolewithwebidentity-web-identity-federation) in the STS documentation for the full web-identity flow (OIDC discovery, JWKS signature verification, `aud`/`iss`/`sub` claim validation, and claim-derived ABAC tags), including the [trust chain](sts.md#trust-chain-cryptographic-verification) and [role trust policy](sts.md#role-trust-policy-for-web-identity).

## Account

The **account** is the top-level scope for IAM entities. In a single-tenant StoreFS deployment all entities belong to the default account. The Admin REST API accepts an optional `?account=` scope parameter (defaulting to the default account; other values are rejected with `InvalidArgument`).

`GetAccountSummary` returns quotas and counts:

| Admin REST API | Description |
|----------------|-------------|
| `GET /api/iam/account-summary` | Summary counters and quotas |

S3 IAM Query action: `GetAccountSummary` (returns `SummaryMap` with `Users`, `UsersQuota`=1000, `Groups`, `GroupsQuota`=200, `AccessKeysPerUser`=2, `AccountUserPoliciesMax`=1000, etc.).

## S3 Authorization Semantics

IAM principals authenticate through `iam_access_keys` (or STS temporary credentials). The S3 authorization chain is:

```
identity Deny → bucket policy Deny → identity Allow → bucket policy Allow → ACL fallback
```

Key rules:

- **IAM principals have no implicit permissions.** A user (or role session) that is not the account root and not `super_admin` is only authorized by an explicit identity-policy `Allow`, a same-account bucket-policy `Allow`, or an explicit ACL grant. This is enforced for object and bucket operations alike (e.g. `s3:CreateBucket` requires an `Allow`, checked *before* bucket existence).
- **Deny always wins**, including over owners and `super_admin` (identity-policy `Deny`).
- **Bucket policy `Allow`** is sufficient for a **same-account** caller; a **cross-account** caller additionally needs an identity-policy `Allow`.
- **ACL grants** are not effective for IAM principals on their own; cross-account access needs both an identity `Allow` and an ACL grant.
- **Role sessions** are authorized only by the role's own policies; a [session policy](sts.md#session-policies) (from STS) can only **narrow** the intersection, never expand it.
- A legacy StoreFS user (authenticated via `users.accesskey`, not IAM) keeps the legacy implicit permissions (owner `FULL_CONTROL`, group-admin rules).

### IAM management authorization

Managing identities is itself permission-gated:

- **Account root / `super_admin`** may perform any IAM management action.
- **Other IAM principals** need an identity policy that `Allow`s the corresponding `iam:<Action>` (e.g. `iam:ListRoles`, `iam:GetRole`, `iam:CreateOpenIDConnectProvider`). Without it the call is denied (`AccessDenied`).
- **Web Console IAM administration** is gated by the `storefs:console:identity:manage` action — granted to legacy `super_admin`, or to an IAM user with the `StoreFSConsoleAdmin` managed policy attached.

## Constraints

| Item | Constraint |
|------|-----------|
| User name | 1–64 chars, `[A-Za-z0-9+=,.@_-]`, case-insensitive unique |
| Group name | case-insensitive unique |
| Policy name | 1–128 chars |
| Access keys per user | 2 |
| Roles per user? | unlimited (account `Roles` in summary) |
| Group path | must not be `/` for IAM groups (reserved for legacy groups) |
| OIDC provider URL | absolute `http(s)`, no userinfo/query/fragment |
| Users quota | 1000 |
| Groups quota | 200 |
| Inline policies (account) | 1000 max (`AccountUserPoliciesMax`) |
| Identity policy | must not contain `Principal` |
| Trust policy | must be a valid policy document |

## Using IAM with boto3

```python
import boto3

iam = boto3.client('iam', endpoint_url='http://127.0.0.1:8901',
                   aws_access_key_id='<AK>', aws_secret_access_key='<SK>')

# 1. Create a user
iam.create_user(UserName='alice', Path='/engineering/')

# 2. Create an access key (secret shown once)
key = iam.create_access_key(UserName='alice')

# 3. Attach a managed policy
iam.attach_user_policy(
    UserName='alice',
    PolicyArn='arn:aws:iam::aws:policy/AmazonS3ReadOnlyAccess',
)

# 4. Create a group and add the user
iam.create_group(GroupName='developers', Path='/storefs/')
iam.add_user_to_group(GroupName='developers', UserName='alice')
```

The access key returned by step 2 can then be used against the S3 endpoint:

```python
s3 = boto3.client('s3', endpoint_url='http://127.0.0.1:8901',
                  aws_access_key_id=key['AccessKey']['AccessKeyId'],
                  aws_secret_access_key=key['AccessKey']['SecretAccessKey'])
```

## Security Notes

- IAM access-key secrets are shown only once at creation; keep them safe and rotate regularly (create a new key, update your application, then delete the old key).
- IAM principals have **no implicit permissions** by design — grant the minimum via policies.
- Identity-policy `Deny` overrides everything, including owner privileges; a mis-scoped `Deny` can lock out access.
- Registering an OIDC provider is a **trust boundary** operation: whoever can do it decides which external issuers StoreFS trusts. Restrict `iam:CreateOpenIDConnectProvider` accordingly.
- Deleting a user/role with attached policies or group memberships fails rather than silently dropping permissions — clean up dependencies first.
