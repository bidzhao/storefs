**[查看中文版](sts_cn.md)**

# STS (Security Token Service) Documentation

## Overview

StoreFS provides an AWS-STS-compatible **Security Token Service**. It issues **temporary credentials** (an access key, secret key, and session token) that can be used in place of long-term IAM access keys for S3 requests. Temporary credentials expire after a configurable duration and are limited to the permissions granted by the role (and any session policy).

```
Caller (long-term credentials)
      ↓
AssumeRole / GetSessionToken / AssumeRoleWithWebIdentity
      ↓
STS issues temporary credentials (AccessKeyId + SecretAccessKey + SessionToken + Expiration)
      ↓
S3 requests authenticate with the temporary credentials
      ↓
Authorized by the role's policies (∩ session policy, if any)
```

STS actions are exposed through the AWS query protocol (`POST` + `Action=`), the same way as the IAM API. Supported actions:

| Action | Description |
|--------|-------------|
| `AssumeRole` | Assume an IAM role and receive temporary credentials scoped to that role |
| `GetSessionToken` | Receive temporary credentials that represent the caller's own identity |
| `GetCallerIdentity` | Return the account/ARN/user ID of the caller's credentials |
| `AssumeRoleWithWebIdentity` | Exchange an OIDC ID token for temporary role credentials (federation) |

## Prerequisites

- IAM users and roles must exist first — see the [IAM documentation](iam.md) for **users**, **groups**, **policies**, and **roles (with trust policies)**.
- To use `AssumeRole`, the role's **trust policy** must allow your identity (or account) to call `sts:AssumeRole`.
- To use `AssumeRoleWithWebIdentity`, register the OIDC issuer as an **OIDC Identity Provider** — see the [OIDC Identity Providers section](iam.md#oidc-identity-providers) of the IAM documentation — and the role's trust policy must grant the federated principal `sts:AssumeRoleWithWebIdentity`.

## AssumeRole

`AssumeRole` returns temporary credentials whose permissions are those of the role, not the caller's. The caller must be authenticated (long-term IAM access key) and must be allowed by the role's trust policy.

### Request (AWS query protocol)

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=AssumeRole&Version=2011-06-15
&RoleArn=arn:aws:iam::000000000000:role/app-role
&RoleSessionName=app-session
&DurationSeconds=3600
```

Optional parameters:

| Parameter | Meaning |
|-----------|---------|
| `Policy` | An inline **session policy** JSON document that further narrows the role's permissions |
| `PolicyArns` | A list of managed policy ARNs (`PolicyArns.member.N`) attached as session policies |
| `DurationSeconds` | Session duration in seconds (default 3600; capped at the role's `MaxSessionDuration`) |

### Response

```xml
<AssumeRoleResponse xmlns="https://sts.amazonaws.com/doc/2011-06-15/">
  <AssumeRoleResult>
    <Credentials>
      <AccessKeyId>AKIA...</AccessKeyId>
      <SecretAccessKey>...</SecretAccessKey>
      <SessionToken>...</SessionToken>
      <Expiration>2026-09-24T10:41:23Z</Expiration>
    </Credentials>
    <AssumedRoleUser>
      <AssumedRoleId>AROA...</AssumedRoleId>
      <Arn>arn:aws:sts::assumed-role/app-role/app-session</Arn>
    </AssumedRoleUser>
  </AssumeRoleResult>
</AssumeRoleResponse>
```

### Error cases

| Condition | Error |
|-----------|-------|
| Role not found | `NoSuchEntity` |
| Trust policy does not allow the caller | `AccessDenied` (`sts:AssumeRole` on the role) |
| Unauthenticated caller | `InvalidClientTokenId` |

### Using boto3

```python
import boto3

sts = boto3.client('sts', endpoint_url='http://127.0.0.1:8901',
                   aws_access_key_id='<long-term-AK>', aws_secret_access_key='<long-term-SK>')

resp = sts.assume_role(
    RoleArn='arn:aws:iam::000000000000:role/app-role',
    RoleSessionName='app-session',
    DurationSeconds=3600,
)
creds = resp['Credentials']
```

Use the temporary credentials against the S3 endpoint (passing the session token):

```python
s3 = boto3.client(
    's3', endpoint_url='http://127.0.0.1:8901',
    aws_access_key_id=creds['AccessKeyId'],
    aws_secret_access_key=creds['SecretAccessKey'],
    aws_session_token=creds['SessionToken'],
)
```

## GetSessionToken

`GetSessionToken` issues temporary credentials that **represent the caller's own identity** — the caller keeps their existing permissions. This is useful for issuing short-lived credentials for safer programmatic access.

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=GetSessionToken&Version=2011-06-15&DurationSeconds=3600
```

The response contains `Credentials` with `AccessKeyId`, `SecretAccessKey`, `SessionToken`, and `Expiration`.

> Unlike `AssumeRole`, the session has no role (`RoleID` = 0), so S3 authorization falls back to the caller's own identity and policies — the caller can do everything it could with its long-term key, within the session lifetime.

### Using boto3

```python
sts = boto3.client('sts', endpoint_url='http://127.0.0.1:8901',
                   aws_access_key_id='<long-term-AK>', aws_secret_access_key='<long-term-SK>')
creds = sts.get_session_token(DurationSeconds=3600)['Credentials']
```

## GetCallerIdentity

`GetCallerIdentity` returns the identity of the credentials used to sign the request: `Account`, `Arn`, and `UserId`.

| Caller | `UserId` / `Account` / `Arn` |
|--------|------------------------------|
| Account root | `UserId` = account root name, `Account` = account root name, `Arn` = `arn:aws:iam::<name>:root` |
| Ordinary IAM user | `UserId` = numeric user ID, `Account` = account ID, `Arn` = user ARN |
| Role session | `UserId` = `{RoleId}:{sessionName}`, `Arn` = assumed-role ARN (`arn:aws:sts::assumed-role/...`) |

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=GetCallerIdentity&Version=2011-06-15
```

### Using boto3

```python
identity = sts.get_caller_identity()
print(identity['Arn'], identity['UserId'], identity['Account'])
```

## Session Policies

A **session policy** is an extra layer of restriction applied when a session is created. It is an *intersection*: the session's effective permissions are

```
role permissions ∩ session policy
```

so a session policy can only **narrow** access, never expand it. Session policies are supported by `AssumeRole` (and `AssumeRoleWithWebIdentity`) through the `Policy` (inline JSON) and `PolicyArns` (managed ARNs) parameters.

```
Action allowed?
  role identity policy  →  Allow?   (no Allow ⇒ denied)
  session policy        →  Allow?   (no Allow ⇒ denied)
  both Allow ⇒ allowed, either Deny ⇒ denied
```

Consequences:

- Without any role policy, even a permissive session policy does **not** grant access (nothing to intersect).
- A session policy cannot grant anything the role itself cannot do.
- If either the role policy or the session policy is a `NoMatch`/`Deny` for an action, the action is denied.

### Example session policy

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "s3:GetObject",
      "Resource": "arn:aws:s3:::reports-bucket/*"
    }
  ]
}
```

## AssumeRoleWithWebIdentity (Web Identity Federation)

`AssumeRoleWithWebIdentity` lets an identity obtained from an **OIDC provider** (e.g. Keycloak, Dex) assume a role and receive temporary credentials — without needing a StoreFS long-term access key. The request carries an OIDC **ID token** (JWT).

### Trust chain (cryptographic verification)

```
JWT iss (untrusted, used only as a lookup hint)
  → must match a registered OIDC Identity Provider  (whitelist gate)
  → OIDC discovery (discovery.issuer == registered issuer)
  → JWKS endpoint (same-origin, no cross-origin redirect)
  → signature verification (RS256/384/512 or ES256/384/512, kid-selected key)
  → claims validation (exp, nbf, iat, non-empty sub, optional aud)
  → role trust policy (Federated principal + claim conditions)
```

Key points:

- The **issuer whitelist** is enforced first: the token's `iss` must match a provider registered via IAM (see [OIDC Identity Providers](iam.md#oidc-identity-providers)).
- **Signature verification happens before any trust-policy evaluation** — the policy never sees unverified claims.
- Supported algorithms: `RS256`, `RS384`, `RS512`, `ES256`, `ES384`, `ES512`. The key comes only from the registered issuer's JWKS (`kid`-selected), never from the JWT header.
- `exp`, `nbf`, `iat`, `iss`, and non-empty `sub` are validated (with 30 s clock skew).
- `aud` is enforced **only when** the provider has configured client IDs; `azp` is never used as an `aud` fallback.
- Production should use **HTTPS** for the OIDC issuer (TLS verification is always on; there is no `InsecureSkipVerify`).

### Role trust policy for web identity

Create the role and give its trust policy a `Federated` principal plus claim conditions:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "Federated": "arn:aws:iam::000000000000:oidc-provider/auth.example.com/realms/storefs"
      },
      "Action": "sts:AssumeRoleWithWebIdentity",
      "Condition": {
        "StringEquals": {
          "{iss}/app_id": "storefs-app"
        }
      }
    }
  ]
}
```

All scalar string claims are injected under three prefixes so conditions can reference any claim:

- `<iss-host>:<claim>` (e.g. `192.168.1.10:8080/auth/realms/storefs:azp`)
- `<iss-host>/<claim>`
- `<iss-url>:<claim>`

For the `aud` claim specifically, the key `<iss-host>:app_id` (and `<iss-url>:aud`) is also injected. Multi-value claims (e.g. `Department=[Engineering,Marketing]`) are available for `ForAllValues`/`ForAnyValue` collection conditions.

**Claim-derived tags (ABAC):** all claims are turned into session **principal tags** and request tags:

- `aws:PrincipalTag/<key>` — usable in role policies during S3 authorization.
- `aws:RequestTag/<key>` — usable in the web-identity trust policy conditions.
- Role tags are available as `iam:ResourceTag/<key>` in trust-policy conditions.

### Request (AWS query protocol)

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=AssumeRoleWithWebIdentity&Version=2011-06-15
&RoleArn=arn:aws:iam::000000000000:role/app-role
&RoleSessionName=federated-session
&WebIdentityToken=<JWT>
&DurationSeconds=3600
```

The response includes `Credentials`, `SubjectFromWebIdentityToken` (the token `sub`), and `AssumedRoleUser`.

### Using boto3

```python
import boto3

sts = boto3.client('sts', endpoint_url='http://127.0.0.1:8901')  # no long-term key needed

resp = sts.assume_role_with_web_identity(
    RoleArn='arn:aws:iam::000000000000:role/app-role',
    RoleSessionName='federated-session',
    WebIdentityToken=id_token,          # the OIDC ID token (JWT)
    DurationSeconds=3600,
)
creds = resp['Credentials']
```

### Error cases

| Condition | Error |
|-----------|-------|
| Missing/oversized token | `InvalidIdentityToken` |
| Issuer not registered | `InvalidIdentityToken` |
| Signature / claims / audience validation failure | `InvalidIdentityToken` |
| Role not found | `NoSuchEntity` |
| Trust policy does not allow the federated principal | `AccessDenied` |

## Temporary Credential Usage and Expiration

- Temporary credentials consist of `AccessKeyId` (`AKIA...`-prefixed), `SecretAccessKey`, and `SessionToken`. Use all three when signing S3 requests (e.g. `aws_session_token` in boto3).
- Sessions are kept **in-memory** on the node that issued them; they are not persisted.
- A session **expires** at `Expiration` (issued as UTC). Default duration is **3600 seconds**; `DurationSeconds` can raise it up to the role's `MaxSessionDuration` (default 3600), which caps it.
- Expiration is checked as `now >= ExpiresAt` (the boundary instant counts as expired).
- After expiry, requests signed with the session credentials fail with `ExpiredToken` (HTTP 400).
- An **inactive** long-term access key still fails with `InvalidClientTokenId`.
- A role session (`AssumeRole` / `AssumeRoleWithWebIdentity`) is authorized **only** by the role's own policies (plus the session policy intersection) — never by the original caller's policies.
- `GetSessionToken` sessions keep the caller's own identity and permissions.

### Who can assume which role

Trust policy matching is strict:

- A `Deny` statement matching the caller wins over any `Allow`.
- `AssumeRole` matches the caller's user/account ARN against the trust policy `Principal` (and requires `sts:AssumeRole` in the statement action).
- `AssumeRoleWithWebIdentity` matches the `Federated` provider ARN (account segment is ignored for the match, so `arn:aws:iam:::oidc-provider/...` and `arn:aws:iam::000000000000:oidc-provider/...` are treated as equivalent).

## Constraints

| Item | Constraint |
|------|-----------|
| Default session duration | 3600 s |
| Maximum session duration | capped by role `MaxSessionDuration` (default 3600 s) |
| WebIdentityToken size | ≤ 64 KiB |
| OIDC JWT algorithms | RS256/384/512, ES256/384/512 |
| JWKS response size | ≤ 1 MiB |
| Discovery/JWKS fetch timeout | 5 s |
| JWKS cache TTL | 1 h |
| Clock skew tolerance | 30 s |
| OIDC issuer scheme | `http`/`https`; production must use HTTPS |
| Session storage | in-memory on the issuing node |

## Security Notes

- Temporary credentials are the least-privilege, short-lived alternative to long-term keys — prefer them for programmatic or delegated access.
- Grant `sts:AssumeRole` in trust policies to the **smallest** set of principals that need it.
- Session policies can only narrow a role's permissions — use them to constrain one-off sessions.
- Registering an OIDC provider is a **trust boundary**: only register issuers you control, and keep `aud` (client IDs) enforced by configuring them on the provider.
- Web-identity tokens are validated cryptographically before any policy is evaluated; never accept an unsigned or mismatched-issuer token.
- Expired sessions are rejected (`ExpiredToken`); plan for credential refresh well before `Expiration`.
