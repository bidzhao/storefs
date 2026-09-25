**[English](iam.md)**

# IAM（身份与访问管理）文档

## 概述

StoreFS 提供与 AWS IAM 兼容的身份与访问管理服务。您可以通过它管理**用户（User）**、**组（Group）**、**策略（Policy）**、**角色（Role，含信任策略）**、**OIDC 身份提供方（Identity Provider）**与**账户（Account）**，并通过身份策略、桶策略与 ACL 组合控制对 S3 桶和对象的访问。

```
管理身份 ── 用户 / 组 / 角色 / 策略 / OIDC 提供方 / 账户
        ↓
授予权限 ── 内联策略（用户/组/角色）+ AWS 托管策略
        ↓
S3 授权 ── 身份策略 → 桶策略 → ACL（Deny > Allow > 回退）
        ↓
使用凭据 ── IAM 访问密钥（或 STS 临时凭证）访问 S3
```

提供两个管理平面：

- **S3 IAM 查询 API** — 与 AWS IAM 兼容的协议（`POST` + `Action=` 参数），供 `botocore`/`boto3` 等工具使用；请求会自动路由到 IAM 服务。
- **Web 控制台 / Admin REST API** — StoreFS 自有的 `GET/POST/PUT/DELETE /api/iam/*` 端点，供 Web 控制台使用。

两个平面共享同一份身份数据，任一处修改另一处可见。

## 核心概念

| 概念 | 说明 |
|------|------|
| **账户（Account）** | 拥有用户、组、角色与策略的顶级身份。单租户部署下所有实体都属于默认账户（`000000000000`）；账户作用域用于隔离 IAM 实体。 |
| **用户（User）** | 拥有名称与可选 path/email 的身份。用户通过创建**访问密钥**（每用户最多 2 个）获得凭据。用户默认**无任何隐式权限**，除非策略显式授予。 |
| **组（Group）** | 一组用户的集合。附加到组的权限对其所有成员生效。 |
| **策略（Policy）** | 授予或拒绝操作的 JSON 文档。可作为**内联**策略（内嵌在用户/组/角色上）或**托管**策略（AWS 内置，按 ARN 附加）。 |
| **角色（Role）** | 不绑定到具体用户的身份。它带有一份**信任策略**（声明谁可以 assume 它），并可拥有自己的内联/附加权限策略。通过 STS 进行 assume（[AssumeRole](sts_cn.md#assumerole)）。 |
| **OIDC 身份提供方** | 受信任的 OpenID Connect 签发方（如 Keycloak、Dex），其 ID Token 可用于通过 `AssumeRoleWithWebIdentity` assume 角色。 |
| **访问密钥（Access Key）** | 颁发给用户的长期凭据（`AccessKeyId` + `SecretAccessKey`）。ID 以 `AKIA` 开头，secret 为 base64，状态为 `Active`/`Inactive`。 |

## IAM 用户/组 与 Legacy 用户/组的关系与差别

StoreFS 区分两套身份体系：**IAM**（AWS 风格）身份与 **legacy**（原有）StoreFS 身份。两者共存，同一部署中可同时存在。

### 身份类型

每个用户都带有一个身份类型，创建时确定、**之后不可翻转**：

| | IAM | Legacy |
|---|-----|--------|
| 身份类型 | `iam` | `legacy` |
| 管理入口 | IAM API / Admin REST `/api/iam/*` / Web 控制台 | 原有 StoreFS Admin API / Web 控制台 |
| 凭据 | **IAM 访问密钥**（`iam_access_keys`，每用户最多 2 个，`Active`/`Inactive`） | 用户自身的 `accesskey` 列 |
| IAM 可见性 | `ListIAMUsers` 与 IAM 管理页会列出 | 对 IAM 管理页**不可见**（legacy 管理员/super_admin 被排除） |
| 角色 | `user`（无 `urole` 层级） | StoreFS 角色：`user`、`group_admin`、`super_admin` |
| 隐式权限 | **无**——必须通过策略/ACL 显式授予 | 保留 legacy 隐式权限（owner `FULL_CONTROL`、组管理员规则、`super_admin` 绕过） |

### 认证顺序

请求用访问密钥签署时，查找顺序为：

1. **IAM 访问密钥**（权威来源）：key 必须存在且为 `Active`；**停用**的 IAM key 立即被拒绝（`InvalidClientTokenId`），绝不回退到 legacy。
2. **Legacy 回退**：只有当 key 在 `iam_access_keys` 中**完全不存在**时，才查询 `users.accesskey`。

因此 IAM 用户与 legacy 用户的凭据互不遮蔽：权威的 IAM 查找永远优先。

### 授权差别

- **IAM 用户**（经 `iam_access_keys` 认证）或**角色会话**是 *IAM 主体*：默认**无隐式 S3 权限**。访问必须通过显式身份策略 `Allow`、同账户桶策略 `Allow`、或显式 ACL grant。连 `s3:CreateBucket` 都需要显式 `Allow`。
- **Legacy 用户**（经 `users.accesskey` 认证）保留 legacy 隐式模型：桶 owner 具有 `FULL_CONTROL`，组管理员对其组内用户拥有的桶具有 `FULL_CONTROL`，`super_admin` 绕过桶策略。
- 同时是**账户根**的 IAM 用户（或 legacy `super_admin`）不受"无隐式权限"规则约束，保留管理特权。

### IAM 管理权限

管理 IAM 身份的权限单独门控：

- legacy `super_admin`（身份类型 `legacy`、角色 `super_admin`）与**账户根**可执行任意 IAM 管理 action；
- **IAM 用户**需要身份策略 `Allow` 对应的 `iam:<Action>`，且 Web 控制台的 IAM 管理还要求 `storefs:console:identity:manage` action（通过 `StoreFSConsoleAdmin` 托管策略授予）。

### IAM 组 与 legacy 组

组存在同一存储中，但按 **path** 区分：

| | IAM 组 | Legacy 组 |
|---|--------|-----------|
| Path | 非 `/`（如 `/storefs/`、`/engineering/`） | `/` |
| IAM 可见性 | `ListIAMGroups` / IAM 管理页会列出 | 从 IAM 视角排除（`NoSuchEntity`） |
| 默认 path | `/storefs/`（绝不为 `/`） | `/` |
| 成员 | IAM 用户 | StoreFS 用户（按 `group_id`） |

需要注意的推论：

- 创建 IAM 组始终存储非 `/` 的 path（默认 `/storefs/`）；显式设置 path 时原样保留。
- 若把组的 path 更新为 `/`，它会从 IAM 视角消失（被当作 legacy 组），因此 IAM 更新路径会把 `/` 归一化回 `/storefs/`。
- IAM 组策略与成员关系使用与 IAM 用户相同的 API 管理（内联策略、附加托管策略、`AddUserToGroup`/`RemoveUserFromGroup`）。

## 用户

创建用户时需提供名称与可选 path；名称**大小写不敏感**（唯一性按大小写不敏感检查），长度 1–64，字符集为 `[A-Za-z0-9+=,.@_-]`。`UserId` 为数字 ID（Snowflake 风格）。创建用户**不会**创建访问密钥（与 AWS 一致）。

### 创建用户（S3 IAM 查询 API）

```
POST / HTTP/1.1
Content-Type: application/x-www-form-urlencoded

Action=CreateUser&Version=2010-05-08&UserName=alice&Path=/engineering/
```

### 创建用户（Admin REST API）

```
POST /api/iam/users
Authorization: Bearer <token>
Content-Type: application/json

{ "name": "alice", "path": "/engineering/", "email": "alice@example.com" }
```

响应返回创建的用户 DTO（`id`、`userName`、`path`、`arn`、`identityType`、`createdAt`）。

### 用户管理 API

| Admin REST API | 说明 |
|----------------|------|
| `GET /api/iam/users?page=&pageSize=` | 列出 IAM 用户（分页、可排序） |
| `GET /api/iam/users/{id}` | 获取用户（可用 id 或名称） |
| `PUT /api/iam/users/{id}` | 更新 `path`/`email` |
| `DELETE /api/iam/users/{id}` | 删除用户（级联删除访问密钥、策略、成员关系） |

S3 IAM 查询 action：`CreateUser`、`GetUser`、`UpdateUser`、`DeleteUser`、`ListUsers`、`CreateAccessKey`、`UpdateAccessKey`、`DeleteAccessKey`、`ListAccessKeys`，以及 `PutUserPolicy`/`GetUserPolicy`/`DeleteUserPolicy`/`ListUserPolicies`、`AttachUserPolicy`/`DetachUserPolicy`/`ListAttachedUserPolicies`、`AddUserToGroup`/`RemoveUserFromGroup`/`ListGroupsForUser`。

### 访问密钥

- 每个用户**最多 2 个**访问密钥。
- **secret 仅在创建时返回一次**；之后只能列出元数据（`AccessKeyId`、`Status`、`CreateDate`）。
- 密钥可停用（`Active` → `Inactive`）；停用的密钥被拒绝，错误码 `InvalidClientTokenId`。
- 删除用户会一并删除其密钥。

```
POST /api/iam/users/alice/access-keys
→ { "accessKey": "AKIA...", "secretAccessKey": "<仅此一次>", "status": "Active" }

PUT /api/iam/users/alice/access-keys/{keyId}   { "status": "Inactive" }
DELETE /api/iam/users/alice/access-keys/{keyId}
GET  /api/iam/users/alice/access-keys
```

## 组

组是用户的命名集合；组权限对其所有成员生效。组名大小写不敏感。Web / Admin 平面将 IAM 组（path 非 `/`，如 `/storefs/`）与 legacy 组（`path = /`）区分开。

### 组管理 API

| Admin REST API | 说明 |
|----------------|------|
| `POST /api/iam/groups` | 创建组 `{name, path?}` |
| `GET /api/iam/groups?page=&pageSize=` | 列出组 |
| `GET /api/iam/groups/{id}` | 获取组 |
| `PUT /api/iam/groups/{id}` | 更新 path |
| `DELETE /api/iam/groups/{id}` | 删除组（级联删除成员关系、内联/附加策略） |
| `GET /api/iam/groups/{id}/users` | 列出组成员 |
| `PUT /api/iam/groups/{id}/users/{userId}` | 把用户加入组 |
| `DELETE /api/iam/groups/{id}/users/{userId}` | 把用户移出组 |
| `PUT/DELETE /api/iam/groups/{id}/policies/{name}` | 管理组内联策略 |
| `POST/DELETE /api/iam/groups/{id}/attached-policies` | 附加/解除托管策略 |

S3 IAM 查询 action：`CreateGroup`、`GetGroup`、`UpdateGroup`、`DeleteGroup`、`ListGroups`、`AddUserToGroup`、`RemoveUserFromGroup`、`ListGroupsForUser`、`PutGroupPolicy`/`GetGroupPolicy`/`DeleteGroupPolicy`/`ListGroupPolicies`、`AttachGroupPolicy`/`DetachGroupPolicy`/`ListAttachedGroupPolicies`。

删除仍包含用户、内联策略或附加策略的组会失败，错误码 `DeleteConflict`（与 AWS 一致）。

## 策略

策略为 JSON 文档，包含 `Version`、`Statement[]`（每条含 `Effect`、`Action`/`NotAction`、`Resource`/`NotResource`，可选 `Principal`/`Condition`）。评估为**三值逻辑**：`Deny > Allow > NoMatch` —— 显式 `Deny` 永远优先，显式 `Allow` 放行，无匹配则回退到下一层。

有两种类型：

- **内联策略** — 直接附加到用户/组/角色上。策略名 1–128 字符。身份策略**不得**包含 `Principal`。
- **托管策略** — AWS 内置策略（`AmazonS3FullAccess`、`AmazonS3ReadOnlyAccess`、`IAMReadOnlyAccess`、`AdministratorAccess`、`PowerUserAccess`），以 ARN 标识（`arn:aws:iam::aws:policy/...`）并按 ARN 附加。内置策略只读（不可创建/更新/删除），但可附加。StoreFS 还提供受保护的控制台策略 `arn:storefs:iam::storefs:policy/StoreFSConsoleAdmin`。

### 用户内联策略示例

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

### 策略 API

| Admin REST API | 说明 |
|----------------|------|
| `GET /api/iam/policies?filter=aws\|customer\|all` | 列出托管策略 |
| `GET /api/iam/policies/{name}` | 获取托管策略（名称或 ARN） |
| `PUT /api/iam/users\|groups\|roles/{id}/policies/{name}` | 放置内联策略 |
| `GET /api/iam/users\|groups\|roles/{id}/policies/{name}` | 获取内联策略 |
| `DELETE /api/iam/users\|groups\|roles/{id}/policies/{name}` | 删除内联策略 |
| `POST /api/iam/users\|groups\|roles/{id}/attached-policies` | 附加托管策略 |
| `DELETE /api/iam/users\|groups\|roles/{id}/attached-policies?policyArn=` | 解除托管策略 |

S3 IAM 查询 action：`PutUserPolicy`/`GetUserPolicy`/`DeleteUserPolicy`/`ListUserPolicies` 及其组/角色对应版本，`AttachUserPolicy`/`DetachUserPolicy`/`ListAttachedUserPolicies` 及其组/角色对应版本。

### 有效权限如何计算

对 S3 请求授权时，调用者的有效身份策略由以下部分组成：

1. 用户的内联策略；
2. 用户所属每个组的内联策略；
3. 附加到用户及其所属组的 AWS 托管策略；
4. 对角色会话：角色的内联策略与附加托管策略（角色会话**仅**按角色策略授权，绝不混入原用户的策略）。

策略 `Condition` 支持 `aws:PrincipalTag`、`s3:ResourceTag`、`s3:RequestObjectTag`、`s3:ExistingObjectTag` 以及 OIDC 派生 claims（ABAC）。

## 角色

角色是一种可被 assume 以获取临时凭证的跨身份（见 STS 文档的 [AssumeRole](sts_cn.md#assumerole)）。每个角色包含：

- `RoleName`（大小写不敏感唯一）、`Path`、`Description`
- `AssumeRolePolicyDocument` — **信任策略**，声明谁可以 assume 该角色（账户根、IAM 用户、服务、或联邦 OIDC 主体）
- `MaxSessionDuration` — 会话最大时长（秒，默认 3600）
- 可选 **tags**，可用于信任策略条件（`iam:ResourceTag/...`）与 ABAC

### 创建角色

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

> `trustPolicy` 必须是合法的策略文档，否则创建被拒绝。

S3 IAM 查询对应 action：`CreateRole`、`GetRole`、`UpdateRole`、`DeleteRole`、`ListRoles`、`UpdateAssumeRolePolicy`。

### 角色 API

| Admin REST API | 说明 |
|----------------|------|
| `GET /api/iam/roles?page=&pageSize=` | 列出角色 |
| `GET /api/iam/roles/{id}` | 获取角色 |
| `PUT /api/iam/roles/{id}` | 更新 description / maxSessionDuration |
| `DELETE /api/iam/roles/{id}` | 删除角色（若仍有内联/附加策略则返回 `DeleteConflict`） |
| `GET /api/iam/roles/{id}/trust-policy` | 读取信任策略 |
| `PUT /api/iam/roles/{id}/trust-policy` | 更新信任策略 |
| `PUT/DELETE /api/iam/roles/{id}/policies/{name}` | 管理角色内联策略 |
| `POST/DELETE /api/iam/roles/{id}/attached-policies` | 附加/解除托管策略 |

删除角色前必须先移除其内联与附加策略，与 AWS 行为一致。

## OIDC 身份提供方

要使用 Web 身份联合（[`AssumeRoleWithWebIdentity`](sts_cn.md#assumerolewithwebidentityweb-身份联邦)），需注册签发 ID Token 的 OIDC 签发方：

- **URL** — 绝对的 `http(s)` URL（不允许 userinfo/query/fragment）。仅接受 `http`/`https` 协议。
- **Client IDs** — 允许认证的 OIDC 客户端 ID（用于校验 token 的 `aud`）。
- **Thumbprints** — 签发方的预期 TLS 证书指纹列表。

### 注册 OIDC 提供方

```
POST /api/iam/oidc-providers
Content-Type: application/json

{
  "url": "https://auth.example.com/realms/storefs",
  "clientIds": ["storefs-app"],
  "thumbprints": ["c8e9..."]
}
```

### OIDC API

| Admin REST API | 说明 |
|----------------|------|
| `GET /api/iam/oidc-providers` | 列出提供方 |
| `GET /api/iam/oidc-providers/{id}` | 获取提供方 |
| `POST /api/iam/oidc-providers` | 创建提供方 |
| `DELETE /api/iam/oidc-providers/{id}` | 删除提供方（若被角色信任策略引用则失败） |
| `PUT/DELETE .../oidc-providers/{id}/client-ids` | 添加/移除 client ID（查询 API：`AddClientIDToOpenIDConnectProvider`、`RemoveClientIDFromOpenIDConnectProvider`） |
| `PUT .../oidc-providers/{id}/thumbprints` | 更新指纹（`UpdateOpenIDConnectProviderThumbprint`） |

S3 IAM 查询 action：`CreateOpenIDConnectProvider`、`ListOpenIDConnectProviders`、`GetOpenIDConnectProvider`、`DeleteOpenIDConnectProvider`，以及上述 client-id/指纹 action。

注册后，角色的信任策略可授予联邦主体：

```json
{
  "Effect": "Allow",
  "Principal": {"Federated": "arn:aws:iam::000000000000:oidc-provider/auth.example.com/realms/storefs"},
  "Action": "sts:AssumeRoleWithWebIdentity",
  "Condition": {"StringEquals": {"{iss}/app_id": "storefs-app"}}
}
```

完整的 Web 身份流程（OIDC discovery、JWKS 签名校验、`aud`/`iss`/`sub` claims 校验、claim 派生 ABAC tags）见 STS 文档的 [AssumeRoleWithWebIdentity（Web 身份联邦）](sts_cn.md#assumerolewithwebidentityweb-身份联邦) 章节，包括[信任链（密码学验证）](sts_cn.md#信任链密码学验证)与[Web 身份的信任策略](sts_cn.md#web-身份的信任策略)。

## 账户

**账户**是 IAM 实体的顶级作用域。单租户部署下所有实体都属于默认账户。Admin REST API 接受可选的 `?account=` 作用域参数（缺省为默认账户；其它值返回 `InvalidArgument`）。

`GetAccountSummary` 返回配额与计数：

| Admin REST API | 说明 |
|----------------|------|
| `GET /api/iam/account-summary` | 汇总计数与配额 |

S3 IAM 查询 action：`GetAccountSummary`（返回 `SummaryMap`，含 `Users`、`UsersQuota`=1000、`Groups`、`GroupsQuota`=200、`AccessKeysPerUser`=2、`AccountUserPoliciesMax`=1000 等）。

## S3 授权语义

IAM 主体通过 `iam_access_keys`（或 STS 临时凭证）认证。S3 授权链为：

```
身份 Deny → 桶策略 Deny → 身份 Allow → 桶策略 Allow → ACL 回退
```

关键规则：

- **IAM 主体无隐式权限。** 非账户根、非 `super_admin` 的用户（或角色会话）只能通过显式身份策略 `Allow`、同账户桶策略 `Allow`、或显式 ACL grant 获得授权。这对对象与桶操作同样生效（例如 `s3:CreateBucket` 需要 `Allow`，且在校验桶是否存在**之前**检查）。
- **Deny 永远优先**，包括对 owner 与 `super_admin`（身份策略 `Deny`）。
- **桶策略 `Allow`** 对**同账户**调用者足够；**跨账户**调用者还需要身份策略 `Allow`。
- **ACL grant** 对 IAM 主体单独不生效；跨账户访问需要身份 `Allow` 与 ACL grant 两者。
- **角色会话**仅按角色自身策略授权；STS 的[会话策略](sts_cn.md#会话策略session-policy)只能**收窄**交集，绝不扩大。
- legacy StoreFS 用户（通过 `users.accesskey` 认证、非 IAM）保留 legacy 隐式权限（owner `FULL_CONTROL`、组管理员规则）。

### IAM 管理授权

管理身份本身也是权限门控的：

- **账户根 / `super_admin`** 可执行任意 IAM 管理 action。
- **其它 IAM 主体**需要身份策略 `Allow` 对应的 `iam:<Action>`（如 `iam:ListRoles`、`iam:GetRole`、`iam:CreateOpenIDConnectProvider`），否则调用被拒绝（`AccessDenied`）。
- **Web 控制台 IAM 管理**由 `storefs:console:identity:manage` action 门控 —— 授予 legacy `super_admin`，或附加了 `StoreFSConsoleAdmin` 托管策略的 IAM 用户。

## 约束

| 项目 | 约束 |
|------|------|
| 用户名 | 1–64 字符，`[A-Za-z0-9+=,.@_-]`，大小写不敏感唯一 |
| 组名 | 大小写不敏感唯一 |
| 策略名 | 1–128 字符 |
| 每用户访问密钥数 | 2 |
| 组 path | IAM 组不能为 `/`（保留给 legacy 组） |
| OIDC 提供方 URL | 绝对 `http(s)`，无 userinfo/query/fragment |
| 用户配额 | 1000 |
| 组配额 | 200 |
| 内联策略（账户） | 上限 1000（`AccountUserPoliciesMax`） |
| 身份策略 | 不得包含 `Principal` |
| 信任策略 | 必须是合法策略文档 |

## 使用 boto3 的示例

```python
import boto3

iam = boto3.client('iam', endpoint_url='http://127.0.0.1:8901',
                   aws_access_key_id='<AK>', aws_secret_access_key='<SK>')

# 1. 创建用户
iam.create_user(UserName='alice', Path='/engineering/')

# 2. 创建访问密钥（secret 仅显示一次）
key = iam.create_access_key(UserName='alice')

# 3. 附加托管策略
iam.attach_user_policy(
    UserName='alice',
    PolicyArn='arn:aws:iam::aws:policy/AmazonS3ReadOnlyAccess',
)

# 4. 创建组并加入用户
iam.create_group(GroupName='developers', Path='/storefs/')
iam.add_user_to_group(GroupName='developers', UserName='alice')
```

步骤 2 返回的访问密钥随后可用于访问 S3 端点：

```python
s3 = boto3.client('s3', endpoint_url='http://127.0.0.1:8901',
                  aws_access_key_id=key['AccessKey']['AccessKeyId'],
                  aws_secret_access_key=key['AccessKey']['SecretAccessKey'])
```

## 安全说明

- IAM 访问密钥 secret 仅在创建时显示一次；请妥善保管并定期轮换（创建新密钥 → 更新应用 → 删除旧密钥）。
- IAM 主体默认**无隐式权限**——请按最小权限原则通过策略授权。
- 身份策略 `Deny` 覆盖一切，包括 owner 权限；一个错误的作用域 `Deny` 可能锁死访问。
- 注册 OIDC 提供方是**信任边界**操作：谁能注册就决定 StoreFS 信任哪些外部签发方。请严格限制 `iam:CreateOpenIDConnectProvider`。
- 删除带附加策略或成员关系的用户/角色会失败，而不是静默丢弃权限——请先清理依赖。
