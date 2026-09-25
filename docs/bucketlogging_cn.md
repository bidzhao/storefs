**[English](bucketlogging.md)**

# 桶日志（Server Access Logging）文档

## 概述

StoreFS 提供与 S3 兼容的**桶访问日志（Server Access Logging）**功能。启用后，针对桶发出的请求详细记录会被采集、攒批，并作为**日志对象**投递到指定的日志桶（logging bucket）。该功能适用于安全审计、用量分析与故障排查。

```
用户请求
      ↓
StoreFS 记录请求（best-effort，绝不阻塞）   → 内存攒批
      ↓
后台投递协程滚动批次 → 日志对象写入日志桶
      ↓
日志对象 key：[前缀]YYYY-MM-DD-hh-mm-ss-<唯一串>  （或分区格式）
```

日志记录**按 best-effort 原则设计**：

- 日志的记录与投递绝不阻塞或影响原始 S3 请求。
- 投递失败仅记录告警并**丢弃**该批次，不重试。
- 对某个桶自身的日志目标桶发出的写入请求**不记录**（递归保护，与 AWS 行为一致）。

### 投递目标解析顺序

对每个 S3 请求，投递目标按以下顺序解析：

1. **桶级** `?logging` 配置（源桶的 S3 API / Web 控制台 / Admin API）。优先级最高。
2. **全局** 集群级日志配置（Web 控制台 `系统配置 → Bucket Logging` 或 Admin API 管理）。默认关闭。

两者均未启用时不产生任何日志记录。

## 日志记录格式

### Standard 模式（默认）

默认 `Standard` 格式遵循 AWS Server Access Log 的 **26 字段**记录：

```
BucketOwner BucketName RequestDateTime RemoteIP Requester RequestID
Operation Key RequestURI HTTPStatus ErrorCode BytesSent ObjectSize
TotalTime TurnAroundTime Referrer UserAgent VersionID HostID SigVersion
CipherSuite AuthType HostHeader TLSVersion AccessPointARN ACLRequired
```

- `BucketOwner` — 源桶所属账号的 ID。
- `Operation` — AWS `REST.*` 记法的请求类型，例如 `REST.PUT.OBJECT`、`REST.GET.OBJECT`、`REST.DELETE.OBJECT`、`REST.PUT.BUCKET`、`REST.POST.UPLOAD`、`REST.PUT.PART`、`REST.POST.COMPLETE_MULTIPART_UPLOAD`、`REST.POST.DELETE_MULTI_OBJECT`、`REST.PUT.ACL` 等。
- `AuthType` — `AuthHeader`、`QueryString`（预签名 URL），匿名时为 `-`。
- `SigVersion` — `SigV4`、`SigV2`，或 `-`。
- `ACLRequired` — 当请求授权依赖桶/对象 ACL 时为 `Yes`，否则为 `-`。
- 未追踪的字段以 `-` 占位。

示例记录：

```
2098240920662052864 source-bucket [02/Sep/2026:09:41:23 +0800] 192.168.1.10 AKIAEXAMPLE 7f9d4c2a REST.PUT.OBJECT "PUT /source-bucket/report.pdf HTTP/1.1" 200 - 0 20480 12 - - - - - SigV4 - AuthHeader - - - -
```

### Journal 模式（扩展）

当 `LoggingType` 设为 `Journal` 时，每条记录使用精简的 **8 字段**格式：

```
[BucketOwner] [BucketName] [RequestDateTime] [Operation] [Key] [ObjectSize] [VersionID] [ETAG]
```

Journal 模式语义：

- 只记录**写**操作（`REST.PUT.*`、`REST.POST.*`、`REST.DELETE.*`）；**读**操作（`REST.GET.*`、`REST.HEAD.*`）**不记录**。
- 配置了 key **过滤器**时，仅记录对象 key 匹配 `prefix`/`suffix` 规则的记录。

示例记录：

```
[2098240920662052864] [source-bucket] [02/Sep/2026:09:41:23 +0800] [REST.PUT.OBJECT] [report.pdf] [-] [-] [-]
```

## 日志对象 Key

### SimplePrefix（默认）

```
[前缀]YYYY-MM-DD-hh-mm-ss-<唯一串>
```

时间戳为秒级精度，后缀单调递增，保证同一秒内多次 flush 的日志对象按 key 排序时顺序稳定。

### PartitionedPrefix（扩展）

```
[前缀]<源账号ID>/<源区域>/<源桶>/<YYYY>/<MM>/<DD>/<YYYY>-<MM>-<DD>-<hh>-<mm>-<ss>-<唯一串>
```

`PartitionDateSource` 选择分区目录使用的日期：

- `DeliveryTime` — 日志对象投递时间。
- `EventTime` — 请求处理时间。

## 配置方式

### 方式一：通过 S3 API 配置单桶

使用标准的 S3 `?logging` 子资源：

| 操作 | 说明 |
|------|------|
| `GET /<bucket>?logging` | 读取当前日志配置 |
| `PUT /<bucket>?logging` | 启用 / 更新日志配置 |
| `DELETE /<bucket>?logging` | 关闭该桶的日志 |
| `POST /<bucket>?logging` | （扩展）立即将待投递日志 flush 成日志对象 |

在源桶上需要 `s3:PutBucketLogging`（`FULL_CONTROL`）权限。

**启用日志：**

```http
PUT /source-bucket?logging HTTP/1.1
Host: 127.0.0.1:8901
Authorization: AWS4-HMAC-SHA256 ...
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<BucketLoggingStatus xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <LoggingEnabled>
    <TargetBucket>log-bucket</TargetBucket>
    <TargetPrefix>s3-access/</TargetPrefix>
  </LoggingEnabled>
</BucketLoggingStatus>
```

**带扩展字段启用日志**（滚动时间、日志类型、分区 key、key 过滤器）：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<BucketLoggingStatus xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <LoggingEnabled>
    <TargetBucket>log-bucket</TargetBucket>
    <TargetPrefix>s3-access/</TargetPrefix>
    <ObjectRollTime>5</ObjectRollTime>
    <LoggingType>Standard</LoggingType>
    <TargetObjectKeyFormat>
      <PartitionedPrefix>
        <PartitionDateSource>DeliveryTime</PartitionDateSource>
      </PartitionedPrefix>
    </TargetObjectKeyFormat>
    <Filter>
      <S3Key>
        <FilterRule>
          <Name>prefix</Name>
          <Value>test/</Value>
        </FilterRule>
        <FilterRule>
          <Name>suffix</Name>
          <Value>.txt</Value>
        </FilterRule>
      </S3Key>
    </Filter>
  </LoggingEnabled>
</BucketLoggingStatus>
```

**关闭日志**（空 `LoggingEnabled`）：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<BucketLoggingStatus xmlns="http://s3.amazonaws.com/doc/2006-03-01/"/>
```

`PUT` 和 `POST ?logging` 的响应都会返回 `FlushedLoggingObject` 字段 —— 本次 flush 待投递日志后生成的日志对象 key（可能为空）。

### 方式二：全局配置

集群级日志开关由**超级管理员**管理：

- Web 控制台：`系统配置 → Bucket Logging`。
- Admin API：`GET /api/bucket-logging/config`、`PUT /api/bucket-logging/config`。

```json
{
  "enabled": true,
  "logging_bucket": "s3-bucket-logging",
  "target_prefix": "s3-access/"
}
```

- `enabled` 为 `true` 时 `logging_bucket` 必填。
- 默认日志桶名为 `s3-bucket-logging`。
- 单桶的 `?logging` 配置始终**覆盖**该桶的全局设置。

### 方式三：Web 控制台 / Admin API 配置单桶

每个桶详情页提供日志配置面板，底层使用 Admin API：

```
GET    /api/buckets/{id}/logging
PUT    /api/buckets/{id}/logging
DELETE /api/buckets/{id}/logging
```

`PUT` 请求体：

```json
{
  "targetBucket": "log-bucket",
  "targetPrefix": "s3-access/",
  "loggingType": "Standard",
  "objectRollTime": 5,
  "partitionDateSource": "DeliveryTime",
  "filter": {
    "Key": {
      "FilterRules": [
        {"Name": "prefix", "Value": "test/"}
      ]
    }
  }
}
```

Web 控制台 / Admin API 与 S3 `?logging` 子资源共享同一份配置，任一处修改另一处可见。

## 授权日志桶

日志桶必须配置**桶策略（bucket policy）**，允许 StoreFS 日志服务主体写入日志对象，否则日志投递会被拒绝（配置时与投递时都会校验）。策略需要：

1. 授予服务主体 `logging.s3.amazonaws.com` 对日志桶前缀的 `s3:PutObject` 权限。
2. 用 `aws:SourceArn`（匹配源桶 ARN）和 `aws:SourceAccount` 绑定投递来源。

`log-bucket` 上的示例策略：

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {"Service": "logging.s3.amazonaws.com"},
      "Action": "s3:PutObject",
      "Resource": "arn:aws:s3:::log-bucket/s3-access/*",
      "Condition": {
        "ArnLike": {"aws:SourceArn": "arn:aws:s3:::source-bucket"},
        "StringEquals": {"aws:SourceAccount": "<源账号ID>"}
      }
    }
  ]
}
```

若日志桶**无策略**，或策略未授权服务主体，配置日志会失败并返回 `AccessDenied`。若之后策略被收紧导致投递不再匹配，在途日志投递会被拒绝并丢弃（`POST ?logging` 强制 flush 会返回 `AccessDenied`）。

## 约束

| 约束 | 行为 |
|------|------|
| 日志桶必须存在 | 否则返回 `NoSuchKey` |
| 日志桶不能是源桶本身 | `InvalidArgument` |
| 日志桶不能已配置日志 | `InvalidArgument`（防止循环投递） |
| 日志桶不能启用加密 | `InvalidArgument` |
| 日志桶不能是 Requester Pays | `InvalidArgument` |
| `LoggingType` 必须为 `Standard` 或 `Journal` | 否则返回 `MalformedXML` |
| `PartitionDateSource` 必须为 `DeliveryTime` 或 `EventTime` | 否则返回 `MalformedXML` |
| 日志桶必须有授权服务主体的策略 | 否则返回 `AccessDenied` |
| 相同配置重复 `PUT`（无变化） | 不触发 flush / 清理；`GET` 的 `Last-Modified` 不变 |

## 行为说明

- **攒批与滚动**：记录按（日志桶、前缀）在内存攒批，按配置的 `ObjectRollTime`（桶级，默认 5 秒）滚动成日志对象；全局配置按 2 秒的后台投递间隔滚动。日志对象在数秒内出现在日志桶中。
- **按需同步投递**：`PUT ?logging`（配置更新 / 关闭）与 `POST ?logging` 会强制 flush 待投递日志，并在 `FlushedLoggingObject` 中返回生成的日志对象 key。
- **更新配置**：先投递*旧*前缀下的待投递日志（保证记录不丢失）；即使没有待投递日志也会滚出一个空日志对象。
- **删除源桶**：先投递其待投递日志，再移除日志配置。
- **Journal 模式**只记录（过滤后）匹配的写操作；Standard 模式将分片上传完成映射为 `REST.POST.UPLOAD`。
- **递归保护**：对某个桶自身日志目标桶的写入请求永不记录。
- 对源桶的所有 S3 请求都会被记录，包括桶级操作（`REST.GET.BUCKET`、`REST.PUT.BUCKET`、`REST.GET.ACL` 等）。

## 示例

### 使用 boto3 启用单桶日志

```python
import boto3

s3 = boto3.client('s3', endpoint_url='http://127.0.0.1:8901',
                  aws_access_key_id='<AK>', aws_secret_access_key='<SK>')

# log-bucket 需已存在并配置了授权策略
s3.put_bucket_logging(
    Bucket='source-bucket',
    BucketLoggingStatus={
        'LoggingEnabled': {
            'TargetBucket': 'log-bucket',
            'TargetPrefix': 's3-access/'
        }
    }
)
```

### 读取当前配置

```python
status = s3.get_bucket_logging(Bucket='source-bucket')
print(status.get('LoggingEnabled'))
```

### 关闭日志

```python
s3.put_bucket_logging(Bucket='source-bucket', BucketLoggingStatus={})
```

### 查看生成的日志对象

```python
resp = s3.list_objects_v2(Bucket='log-bucket', Prefix='s3-access/')
for obj in resp.get('Contents', []):
    print(obj['Key'])
```

每个日志对象包含一条或多条访问日志记录（每行一条）。

## 安全说明

- 日志包含请求元数据（源 IP、请求者、操作、key、HTTP 状态、对象大小、响应时间），请妥善保护日志桶。
- 日志投递受日志桶策略约束；收紧策略会拒绝投递，而不是静默削弱访问控制。
- 集群级的操作审计（审计日志功能）由独立的**审计日志（Audit Log）**功能提供。
