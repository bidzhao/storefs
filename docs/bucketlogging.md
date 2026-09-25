**[查看中文版](bucketlogging_cn.md)**

# Bucket Logging (Server Access Logging) Documentation

## Overview

StoreFS provides S3-compatible **Server Access Logging** for buckets. When enabled, detailed records of requests that are made to a bucket are collected, batched, and delivered as **log objects** into a designated logging bucket. This is useful for security auditing, usage analysis, and troubleshooting.

```
User request
      ↓
StoreFS logs the request (best-effort, never blocks)   → batched in memory
      ↓
Background flusher rolls the batch → log object written into the logging bucket
      ↓
Log object key: [prefix]YYYY-MM-DD-hh-mm-ss-<Unique>   (or partitioned format)
```

Logging is **best-effort by design**:

- Log recording and delivery never block or fail the original S3 request.
- A failed delivery is logged with a warning and **dropped** — it is not retried.
- Requests that write into a bucket's **own** logging target bucket are **not** recorded (recursion protection, matching AWS behavior).

### How the Target Is Resolved

For every S3 request, the delivery target is resolved in this order:

1. **Per-bucket** `?logging` configuration of the source bucket (S3 API / web console / Admin API). Takes priority.
2. **Global** cluster-level logging configuration (managed via the web console `System Config → Bucket Logging` or the Admin API). Off by default.

If neither is enabled, no log records are produced.

## Log Record Format

### Standard Mode (default)

The default `Standard` format follows the AWS Server Access Log **26-field** record:

```
BucketOwner BucketName RequestDateTime RemoteIP Requester RequestID
Operation Key RequestURI HTTPStatus ErrorCode BytesSent ObjectSize
TotalTime TurnAroundTime Referrer UserAgent VersionID HostID SigVersion
CipherSuite AuthType HostHeader TLSVersion AccessPointARN ACLRequired
```

- `BucketOwner` — the ID of the account that owns the source bucket.
- `Operation` — the request type in AWS `REST.*` notation, e.g. `REST.PUT.OBJECT`, `REST.GET.OBJECT`, `REST.DELETE.OBJECT`, `REST.PUT.BUCKET`, `REST.POST.UPLOAD`, `REST.PUT.PART`, `REST.POST.COMPLETE_MULTIPART_UPLOAD`, `REST.POST.DELETE_MULTI_OBJECT`, `REST.PUT.ACL`, ...
- `AuthType` — `AuthHeader`, `QueryString` (presigned URLs), or `-` (anonymous).
- `SigVersion` — `SigV4`, `SigV2`, or `-`.
- `ACLRequired` — `Yes` when the request authorization depended on a bucket/object ACL, otherwise `-`.
- Untracked fields are filled with `-`.

Example record:

```
2098240920662052864 source-bucket [02/Sep/2026:09:41:23 +0800] 192.168.1.10 AKIAEXAMPLE 7f9d4c2a REST.PUT.OBJECT "PUT /source-bucket/report.pdf HTTP/1.1" 200 - 0 20480 12 - - - - - SigV4 - AuthHeader - - - -
```

### Journal Mode (extension)

When `LoggingType` is set to `Journal`, each record uses a compact **8-field** format:

```
[BucketOwner] [BucketName] [RequestDateTime] [Operation] [Key] [ObjectSize] [VersionID] [ETAG]
```

Journal mode semantics:

- Only **write** operations are recorded (`REST.PUT.*`, `REST.POST.*`, `REST.DELETE.*`); read operations (`REST.GET.*`, `REST.HEAD.*`) are **not** recorded.
- When a key **filter** is configured, only records whose object key matches the `prefix`/`suffix` rules are logged.

Example record:

```
[2098240920662052864] [source-bucket] [02/Sep/2026:09:41:23 +0800] [REST.PUT.OBJECT] [report.pdf] [-] [-] [-]
```

## Log Object Key

### SimplePrefix (default)

```
[prefix]YYYY-MM-DD-hh-mm-ss-<UniqueString>
```

The timestamp is at second precision; the suffix is monotonic so multiple flushes within the same second keep a stable order.

### PartitionedPrefix (extension)

```
[prefix]<SourceAccountId>/<SourceRegion>/<SourceBucket>/<YYYY>/<MM>/<DD>/<YYYY>-<MM>-<DD>-<hh>-<mm>-<ss>-<UniqueString>
```

`PartitionDateSource` selects which date is used for the partition directories:

- `DeliveryTime` — the time the log object was delivered.
- `EventTime` — the time the request was processed.

## Configuration

### Method 1 — Per-bucket via the S3 API

Use the standard S3 `?logging` sub-resource:

| Operation | Description |
|-----------|-------------|
| `GET /<bucket>?logging` | Read the current logging configuration |
| `PUT /<bucket>?logging` | Enable / update the logging configuration |
| `DELETE /<bucket>?logging` | Disable logging for the bucket |
| `POST /<bucket>?logging` | (extension) Force-flush pending logs into a log object immediately |

Requires `s3:PutBucketLogging` (`FULL_CONTROL`) on the source bucket.

**Enable logging:**

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

**Enable logging with extensions** (roll time, log type, partitioned key, key filter):

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

**Disable logging** (empty `LoggingEnabled`):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<BucketLoggingStatus xmlns="http://s3.amazonaws.com/doc/2006-03-01/"/>
```

Both `PUT` and `POST ?logging` return a `FlushedLoggingObject` field in the response — the key of the log object produced by flushing the pending logs at that moment (may be empty).

### Method 2 — Global configuration

The cluster-wide logging switch is managed by a **super admin**:

- Web console: `System Config → Bucket Logging`.
- Admin API: `GET /api/bucket-logging/config`, `PUT /api/bucket-logging/config`.

```json
{
  "enabled": true,
  "logging_bucket": "s3-bucket-logging",
  "target_prefix": "s3-access/"
}
```

- When `enabled` is `true`, `logging_bucket` is required.
- The default logging bucket name is `s3-bucket-logging`.
- A per-bucket `?logging` configuration always **overrides** the global setting for that bucket.

### Method 3 — Per-bucket via web console / Admin API

Each bucket's detail page provides a logging configuration panel backed by the Admin API:

```
GET    /api/buckets/{id}/logging
PUT    /api/buckets/{id}/logging
DELETE /api/buckets/{id}/logging
```

`PUT` body:

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

The web console / Admin API view and the S3 `?logging` sub-resource share the same configuration, so changes made through one are visible through the other.

## Authorizing the Log Bucket

The logging bucket must carry a **bucket policy** that allows the StoreFS logging service principal to write log objects; otherwise log delivery is denied (both at configuration time and at delivery time). The policy must:

1. Grant `s3:PutObject` on the log bucket's prefix to the service principal `logging.s3.amazonaws.com`.
2. Bind the delivery with `aws:SourceArn` (matching the source bucket ARN) and `aws:SourceAccount`.

Example policy on `log-bucket`:

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
        "StringEquals": {"aws:SourceAccount": "<source-account-id>"}
      }
    }
  ]
}
```

If the log bucket has **no policy**, or the policy does not authorize the service principal, configuring logging fails with `AccessDenied`. If the policy is later tightened so delivery no longer matches, in-flight log delivery is denied and dropped (a `POST ?logging` force-flush reports `AccessDenied`).

## Constraints

| Constraint | Behavior |
|-----------|----------|
| Log bucket must exist | Else `NoSuchKey` |
| Log bucket must not be the source bucket | `InvalidArgument` |
| Log bucket must not itself have a logging configuration | `InvalidArgument` (prevents cycles) |
| Log bucket must not be encrypted | `InvalidArgument` |
| Log bucket must not be Requester Pays | `InvalidArgument` |
| `LoggingType` must be `Standard` or `Journal` | Else `MalformedXML` |
| `PartitionDateSource` must be `DeliveryTime` or `EventTime` | Else `MalformedXML` |
| Log bucket must have an authorizing policy for the service principal | Else `AccessDenied` |
| Same configuration re-`PUT` (no change) | No flush, no cleanup; `Last-Modified` on `GET` is unchanged |

## Behavior Notes

- **Batching & roll time**: records are batched per (log bucket, prefix) in memory and rolled into log objects on the configured `ObjectRollTime` (per-bucket; default 5 seconds), or on the 2-second background flush interval for the global configuration. Log objects appear in the log bucket within a few seconds.
- **Delivery is synchronous only on demand**: pending logs are force-flushed on `PUT ?logging` (configuration update / disable) and on `POST ?logging`, with the produced log object key returned in `FlushedLoggingObject`.
- **Updating the configuration**: the pending logs under the *old* prefix are flushed first (so no records are lost), and an empty log object is rolled out even if there is nothing pending.
- **Deleting a source bucket** flushes its pending logs and removes its logging configuration.
- **Journal mode** records only matching (filtered) write operations; Standard mode maps multipart-upload completion to `REST.POST.UPLOAD`.
- **Recursion protection**: requests that write into a bucket's own logging target bucket are never recorded.
- Logs are recorded for every S3 request against the source bucket, including bucket-level operations (`REST.GET.BUCKET`, `REST.PUT.BUCKET`, `REST.GET.ACL`, ...).

## Examples

### Enabling per-bucket logging with the AWS SDK for Python (boto3)

```python
import boto3

s3 = boto3.client('s3', endpoint_url='http://127.0.0.1:8901',
                  aws_access_key_id='<AK>', aws_secret_access_key='<SK>')

# log-bucket must exist and carry an authorizing policy
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

### Reading the current configuration

```python
status = s3.get_bucket_logging(Bucket='source-bucket')
print(status.get('LoggingEnabled'))
```

### Disabling logging

```python
s3.put_bucket_logging(Bucket='source-bucket', BucketLoggingStatus={})
```

### Viewing the produced log objects

```python
resp = s3.list_objects_v2(Bucket='log-bucket', Prefix='s3-access/')
for obj in resp.get('Contents', []):
    print(obj['Key'])
```

Each log object contains one or more access-log records (one per line).

## Security

- Logs contain metadata about requests (source IP, requester, operation, key, HTTP status, object size, response time) — protect the log bucket accordingly.
- Log delivery is subject to the log bucket's policy; tightening the policy denies delivery rather than silently weakening access control.
- Bucket-level audit logging (cluster-level operation audit trail) is provided separately by the **Audit Log** feature.
