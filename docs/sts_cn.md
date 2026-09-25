**[English](sts.md)**

# STS（安全令牌服务）文档

## 概述

StoreFS 提供与 AWS STS 兼容的**安全令牌服务（Security Token Service）**。它签发**临时凭证**（访问密钥 + 秘密密钥 + 会话令牌），可替代长期 IAM 访问密钥用于 S3 请求。临时凭证在可配置的时长后过期，且权限被限制为角色所授予（以及任何会话策略）的范围。

```
调用者（长期凭证）
      ↓
AssumeRole / GetSessionToken / AssumeRoleWithWebIdentity
      ↓
STS 签发临时凭证（AccessKeyId + SecretAccessKey + SessionToken + Expiration）
      ↓
使用临时凭证认证 S3 请求
      ↓
按角色的策略（∩ 会话策略，如有）授权
```

STS action 通过 AWS 查询协议（`POST` + `Action=`）暴露，与 IAM API 方式相同。支持的 action：

| Action | 说明 |
|--------|------|
| `AssumeRole` | assume 一个 IAM 角色，获得限定为该角色权限的临时凭证 |
| `GetSessionToken` | 获得代表调用者**自身身份**的临时凭证 |
| `GetCallerIdentity` | 返回调用凭证所属的账户/ARN/用户 ID |
| `AssumeRoleWithWebIdentity` | 用 OIDC ID Token 换取角色临时凭证（联邦） |

## 前置条件

- 需先存在 IAM 用户与角色 — 见 [IAM 文档](iam_cn.md) 中的**用户**、**组**、**策略**与**角色（含信任策略）**章节。
- 使用 `AssumeRole` 时，角色的**信任策略**必须允许您的身份（或账户）调用 `sts:AssumeRole`。
- 使用 `AssumeRoleWithWebIdentity` 时，需先将 OIDC 签发方注册为 **OIDC 身份提供方** — 见 [IAM 文档的 OIDC 身份提供方章节](iam_cn.md#oidc-身份提供方) — 且角色的信任策略必须授予联邦主体 `sts:AssumeRoleWithWebIdentity`。

## AssumeRole

`AssumeRole` 返回的临时凭证权限为**角色**的权限，而非调用者自身权限。调用者必须已认证（长期 IAM 访问密钥），并被角色的信任策略允许。

### 请求（AWS 查询协议）

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=AssumeRole&Version=2011-06-15
&RoleArn=arn:aws:iam::000000000000:role/app-role
&RoleSessionName=app-session
&DurationSeconds=3600
```

可选参数：

| 参数 | 含义 |
|------|------|
| `Policy` | 内联**会话策略** JSON 文档，进一步收窄角色权限 |
| `PolicyArns` | 托管策略 ARN 列表（`PolicyArns.member.N`），作为会话策略附加 |
| `DurationSeconds` | 会话时长（秒，默认 3600；上限为角色的 `MaxSessionDuration`） |

### 响应

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

### 错误情形

| 条件 | 错误 |
|------|------|
| 角色不存在 | `NoSuchEntity` |
| 信任策略不允许调用者 | `AccessDenied`（对角色执行 `sts:AssumeRole`） |
| 调用者未认证 | `InvalidClientTokenId` |

### 使用 boto3

```python
import boto3

sts = boto3.client('sts', endpoint_url='http://127.0.0.1:8901',
                   aws_access_key_id='<长期-AK>', aws_secret_access_key='<长期-SK>')

resp = sts.assume_role(
    RoleArn='arn:aws:iam::000000000000:role/app-role',
    RoleSessionName='app-session',
    DurationSeconds=3600,
)
creds = resp['Credentials']
```

使用临时凭证访问 S3 端点（需带上会话令牌）：

```python
s3 = boto3.client(
    's3', endpoint_url='http://127.0.0.1:8901',
    aws_access_key_id=creds['AccessKeyId'],
    aws_secret_access_key=creds['SecretAccessKey'],
    aws_session_token=creds['SessionToken'],
)
```

## GetSessionToken

`GetSessionToken` 签发的临时凭证**代表调用者自身身份**——调用者保留原有权限。适用于为更安全的程序化访问签发短时凭证。

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=GetSessionToken&Version=2011-06-15&DurationSeconds=3600
```

响应包含 `Credentials`（`AccessKeyId`、`SecretAccessKey`、`SessionToken`、`Expiration`）。

> 与 `AssumeRole` 不同，该会话无角色（`RoleID` = 0），S3 授权回落到调用者自身的身份与策略——在会话有效期内，调用者能做其长期密钥能做的一切。

### 使用 boto3

```python
sts = boto3.client('sts', endpoint_url='http://127.0.0.1:8901',
                   aws_access_key_id='<长期-AK>', aws_secret_access_key='<长期-SK>')
creds = sts.get_session_token(DurationSeconds=3600)['Credentials']
```

## GetCallerIdentity

`GetCallerIdentity` 返回用于签署请求的凭证所属身份：`Account`、`Arn`、`UserId`。

| 调用者 | `UserId` / `Account` / `Arn` |
|--------|------------------------------|
| 账户根 | `UserId` = 账户根名称，`Account` = 账户根名称，`Arn` = `arn:aws:iam::<名称>:root` |
| 普通 IAM 用户 | `UserId` = 数字用户 ID，`Account` = 账户 ID，`Arn` = 用户 ARN |
| 角色会话 | `UserId` = `{RoleId}:{sessionName}`，`Arn` = assumed-role ARN（`arn:aws:sts::assumed-role/...`） |

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=GetCallerIdentity&Version=2011-06-15
```

### 使用 boto3

```python
identity = sts.get_caller_identity()
print(identity['Arn'], identity['UserId'], identity['Account'])
```

## 会话策略（Session Policy）

**会话策略**是在创建会话时附加的一层额外限制，是**交集**语义：会话的有效权限为

```
角色权限 ∩ 会话策略
```

因此会话策略只能**收窄**访问，绝不扩大。`AssumeRole`（及 `AssumeRoleWithWebIdentity`）通过 `Policy`（内联 JSON）与 `PolicyArns`（托管 ARN）参数支持会话策略。

```
Action 是否允许？
  角色身份策略  →  Allow？   （无 Allow ⇒ 拒绝）
  会话策略      →  Allow？   （无 Allow ⇒ 拒绝）
  两者均 Allow ⇒ 允许；任一 Deny ⇒ 拒绝
```

推论：

- 角色无任何策略时，即使会话策略再宽松也无法授予访问权（没有交集对象）。
- 会话策略不能授予角色自身做不到的操作。
- 只要角色策略或会话策略之一对该 action 是 `NoMatch`/`Deny`，该 action 即被拒绝。

### 会话策略示例

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

## AssumeRoleWithWebIdentity（Web 身份联邦）

`AssumeRoleWithWebIdentity` 允许从 **OIDC 提供方**（如 Keycloak、Dex）获得的身份 assume 一个角色并取得临时凭证——无需 StoreFS 长期访问密钥。请求携带 OIDC **ID Token**（JWT）。

### 信任链（密码学验证）

```
JWT iss（不可信，仅作查找提示）
  → 必须匹配已注册的 OIDC 身份提供方  （白名单 gate）
  → OIDC discovery（discovery.issuer == 已注册签发方）
  → JWKS 端点（同源，禁止跨源 redirect）
  → 签名验证（RS256/384/512 或 ES256/384/512，按 kid 选 key）
  → claims 校验（exp、nbf、iat、非空 sub、可选 aud）
  → 角色信任策略（Federated 主体 + claim 条件）
```

关键点：

- **签发方白名单**最先执行：token 的 `iss` 必须匹配通过 IAM 注册的提供方（见 [OIDC 身份提供方](iam_cn.md#oidc-身份提供方)）。
- **签名验证先于任何信任策略评估**——策略永远看不到未验证的 claims。
- 支持的算法：`RS256`、`RS384`、`RS512`、`ES256`、`ES384`、`ES512`。验签 key 只能来自已注册签发方的 JWKS（按 `kid` 选择），绝不来自 JWT header。
- `exp`、`nbf`、`iat`、`iss` 与非空 `sub` 均被校验（允许 30 秒时钟偏差）。
- `aud` **仅在**提供方配置了 client ID 时强制校验；`azp` 绝不作为 `aud` 的替代。
- 生产环境 OIDC 签发方应使用 **HTTPS**（TLS 校验始终开启，不存在 `InsecureSkipVerify`）。

### Web 身份的信任策略

创建角色，并给信任策略一个 `Federated` 主体与 claim 条件：

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

所有标量 string claim 会以三种前缀注入，使条件可引用任意 claim：

- `<iss-主机>:<claim>`（例如 `192.168.1.10:8080/auth/realms/storefs:azp`）
- `<iss-主机>/<claim>`
- `<iss-URL>:<claim>`

对 `aud` claim 特别地，同时注入 `<iss-主机>:app_id`（及 `<iss-URL>:aud`）。多值 claim（如 `Department=[Engineering,Marketing]`）可用于 `ForAllValues`/`ForAnyValue` 集合条件。

**Claim 派生标签（ABAC）：** 所有 claims 都会转为会话**主体标签**与请求标签：

- `aws:PrincipalTag/<key>` — 可在 S3 授权阶段的角色策略中使用。
- `aws:RequestTag/<key>` — 可在 Web 身份信任策略条件中使用。
- 角色标签可作为 `iam:ResourceTag/<key>` 供信任策略条件使用。

### 请求（AWS 查询协议）

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=AssumeRoleWithWebIdentity&Version=2011-06-15
&RoleArn=arn:aws:iam::000000000000:role/app-role
&RoleSessionName=federated-session
&WebIdentityToken=<JWT>
&DurationSeconds=3600
```

响应包含 `Credentials`、`SubjectFromWebIdentityToken`（token 的 `sub`）与 `AssumedRoleUser`。

### 使用 boto3

```python
import boto3

sts = boto3.client('sts', endpoint_url='http://127.0.0.1:8901')  # 无需长期密钥

resp = sts.assume_role_with_web_identity(
    RoleArn='arn:aws:iam::000000000000:role/app-role',
    RoleSessionName='federated-session',
    WebIdentityToken=id_token,          # OIDC ID Token（JWT）
    DurationSeconds=3600,
)
creds = resp['Credentials']
```

### 错误情形

| 条件 | 错误 |
|------|------|
| token 缺失或超限 | `InvalidIdentityToken` |
| 签发方未注册 | `InvalidIdentityToken` |
| 签名 / claims / audience 校验失败 | `InvalidIdentityToken` |
| 角色不存在 | `NoSuchEntity` |
| 信任策略不允许联邦主体 | `AccessDenied` |

## 临时凭证的使用与过期

- 临时凭证由 `AccessKeyId`（`AKIA...` 前缀）、`SecretAccessKey` 与 `SessionToken` 组成。签署 S3 请求时三者都要使用（例如 boto3 的 `aws_session_token`）。
- 会话保存在**签发节点的进程内**（不落库、不持久化）。
- 会话在 `Expiration`（UTC）时刻**过期**。默认时长 **3600 秒**；`DurationSeconds` 可提高时长，但被角色的 `MaxSessionDuration`（默认 3600）封顶。
- 过期判定为 `now >= ExpiresAt`（边界时刻计为已过期）。
- 过期后，用该会话凭证签署的请求失败，错误码 `ExpiredToken`（HTTP 400）。
- **停用**的长期访问密钥仍返回 `InvalidClientTokenId`。
- 角色会话（`AssumeRole` / `AssumeRoleWithWebIdentity`）**仅**按角色自身策略（再叠加会话策略交集）授权——绝不混入原调用者的策略。
- `GetSessionToken` 会话保留调用者自身的身份与权限。

### 谁可以 assume 哪个角色

信任策略匹配是严格的：

- 匹配调用者的 `Deny` 语句优先于任何 `Allow`。
- `AssumeRole` 将调用者的用户/账户 ARN 与信任策略 `Principal` 匹配（并要求语句 action 含 `sts:AssumeRole`）。
- `AssumeRoleWithWebIdentity` 匹配 `Federated` 提供方 ARN（匹配时忽略 account 段，因此 `arn:aws:iam:::oidc-provider/...` 与 `arn:aws:iam::000000000000:oidc-provider/...` 视为等价）。

## 约束

| 项目 | 约束 |
|------|------|
| 默认会话时长 | 3600 秒 |
| 最大会话时长 | 由角色 `MaxSessionDuration` 封顶（默认 3600 秒） |
| WebIdentityToken 大小 | ≤ 64 KiB |
| OIDC JWT 算法 | RS256/384/512、ES256/384/512 |
| JWKS 响应大小 | ≤ 1 MiB |
| discovery/JWKS 获取超时 | 5 秒 |
| JWKS 缓存 TTL | 1 小时 |
| 时钟偏差容忍 | 30 秒 |
| OIDC 签发方协议 | `http`/`https`；生产必须 HTTPS |
| 会话存储 | 签发节点进程内 |

## 安全说明

- 临时凭证是比长期密钥更"最小权限、短时"的选择——程序化或委托访问优先使用。
- 在信任策略中把 `sts:AssumeRole` 只授予**最小**的必要主体集合。
- 会话策略只能收窄角色权限——用它约束一次性会话。
- 注册 OIDC 提供方是**信任边界**：只注册您控制的签发方，并在提供方上配置 client ID 以强制 `aud` 校验。
- Web 身份 token 在任何策略评估之前完成密码学验证；绝不接受未签名或签发方不匹配的 token。
- 过期会话会被拒绝（`ExpiredToken`）；请在 `Expiration` 之前规划好凭证刷新。
