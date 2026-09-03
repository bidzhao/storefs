**[查看中文版](lakehouse_cn.md)**

# StoreFS Lakehouse — Iceberg Lakehouse Analytics

StoreFS ships a complete **Lakehouse** capability: an open-format analytics data lake built on **Apache Iceberg**, served through a standards-compliant **Iceberg REST Catalog**. A single StoreFS cluster acts as both the metadata control plane and the S3 object-storage data plane — connect Apache Spark, Flink, Trino, or DuckDB directly, with no external Hive Metastore and no separate S3.

---

## At a Glance

| Dimension | Description |
|-----------|-------------|
| **What it is** | One StoreFS = Iceberg REST Catalog (metadata, `7946`) + S3-compatible data plane (`8901`), a self-contained lakehouse |
| **How to use** | Any Iceberg engine connects to the REST Catalog to create databases/tables and read/write; engines write Parquet data files to the S3 data plane |
| **Highlights** | Full Iceberg v1 REST spec · Multi-catalog isolation · Dynamic warehouse-bucket resolution · One-click maintenance · Web console · MCP management |
| **Engines** | Spark / Flink / Trino / DuckDB (any Iceberg client) |

### Quick Start (three steps)

```bash
ADMIN=http://<host>:7946

# 1. Log in to get a token
TOKEN=$(curl -s -X POST "$ADMIN/api/auth/login" -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"<password>"}' \
  | python3 -c 'import sys,json;print(json.load(sys.stdin).get("token",""))')

# 2. Enable Iceberg (auto-creates the warehouse bucket)
curl -s -X POST "$ADMIN/api/iceberg/admin/enable" -H "Authorization: Bearer $TOKEN"

# 3. Connect Spark to create tables and read/write (see Using with Apache Spark below)
```

---

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                       Apache Spark / Flink / Trino               │
│                            (Iceberg engine client)               │
└───────────────┬────────────────────────────────┬────────────────┘
                │ Metadata / control             │ Data / storage
                │ Iceberg REST Catalog           │ S3-compatible API (SigV4)
                │ HTTP :7946 Bearer token        │ HTTP :8901
                ▼                                ▼
┌────────────────────────────────────────────────────────────────┐
│                          StoreFS Cluster                        │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Admin API :7946                                         │  │
│  │  /api/iceberg/{catalog}/v1/...  → Iceberg REST Catalog   │  │
│  │     namespaces · tables · views · config · create ·      │  │
│  │     commit · transactions · register · credentials/sign  │  │
│  │  /api/iceberg/admin/...        → control plane + maint.  │  │
│  └──────────────────────────────────────────────────────────┘  │
│           │ Native write path (LazyDispatcherStore → object)   │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ S3 API :8901 (data plane)                                │  │
│  │   s3://<warehouse-bucket>/<ns>/<table>/                  │  │
│  │     metadata/*.metadata.json · manifest-list · manifests │  │
│  │     data/*.parquet                                       │  │
│  └──────────────────────────────────────────────────────────┘  │
│  Doris metadata: iceberg_catalogs · iceberg_tables ·           │
│                  iceberg_table_heads · iceberg_commits · …      │
└────────────────────────────────────────────────────────────────┘
```

**Key mechanisms**:
- **Commit engine**: idempotent, exactly-once, atomic CAS head transition (concurrent commits get `409`, never overwritten).
- **Multi-catalog isolation**: each catalog owns an independent warehouse root (`s3://bucket/prefix/`). There is no implicit catalog — catalogs are created explicitly via the admin API and each pins its own warehouse root.
- **Dynamic warehouse-bucket resolution**: `LazyDispatcherStore` resolves the target bucket on every operation — switching warehouses requires no restart.

---

## Setup

### Method 1 — Admin API (curl)

See [Quick Start](#quick-start-three-steps) steps 1–2. After enabling, adjust the configuration with `PUT /api/iceberg/admin/config`:

```json
{
  "enabled": true,
  "warehouse_bucket": "lakehouse",
  "default_namespace": "default",
  "idempotency_retention_days": 7,
  "orphan_grace_period_hours": 168
}
```

| Field | Default | Description |
|-------|---------|-------------|
| `warehouse_bucket` | `lakehouse` | The S3 bucket that stores all Iceberg metadata + data files |
| `default_namespace` | `default` | Default namespace |
| `idempotency_retention_days` | `7` | How long commit idempotency records are kept |
| `orphan_grace_period_hours` | `168` (7 days) | Minimum age before a file is considered an orphan |

### Method 2 — Web Console (recommended)

**1. Create the warehouse bucket**

Go to **Bucket Management** → **New Bucket** and fill in:
- **Bucket name**: `lakehouse` (or any name you like)
- **Storage policy**: optional, the default is fine
- Other options (encryption / versioning / object lock) as needed

**2. Enable Iceberg and link this bucket**

Go to **System Config → Iceberg**:
- Turn on **Enabled**
- Set **warehouse_bucket** to the bucket you just created (e.g. `lakehouse`)
- Click **Save**

On save, the backend automatically ensures the bucket exists; if you created it in the Web console, you are its owner and the permissions are correct.

**3. Create a Catalog on the Lakehouse home page**

Go to the **Iceberg / Lakehouse** page:
- Click **Create Catalog** next to the catalog dropdown
- **Catalog name**: e.g. `lakehouse` (or custom, e.g. `analytics`)
- **Warehouse**: `s3://lakehouse/` (pointing at the bucket)
- Confirm — you can now create databases/tables under this catalog with Spark.

> **Key point**: There is no implicit `default` catalog. Every catalog is created explicitly and its warehouse root is pinned at creation. The cluster-wide `warehouse_bucket` config only names the bucket that `POST /api/iceberg/admin/enable` auto-creates; it does not map to any catalog. To use a warehouse bucket with Spark: create the bucket → create a catalog whose warehouse points at that bucket.

### Multi-catalog

```bash
# Create an additional catalog on any bucket you can write
curl -s -X POST "$ADMIN/api/iceberg/admin/catalogs" -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"name":"analytics","warehouse":"s3://analytics-wh/"}'
```

Naming rules: lowercase `^[a-zA-Z][a-zA-Z0-9_-]{0,63}$`; deleted names are never reused.

---

## Using with Apache Spark

### Step 1 — Prerequisites

Complete [Setup](#setup) and confirm `GET /api/iceberg/{catalog}/v1/config` returns the `warehouse-location` of the catalog you created.

### Step 2 — Start Spark (Docker)

```bash
mkdir -p ~/spark-work/jars && cd ~/spark-work/jars
wget https://repo1.maven.org/maven2/org/apache/iceberg/iceberg-spark-runtime-3.5_2.12/1.6.1/iceberg-spark-runtime-3.5_2.12-1.6.1.jar
wget https://repo1.maven.org/maven2/org/apache/iceberg/iceberg-aws-bundle/1.6.1/iceberg-aws-bundle-1.6.1.jar
```

> Missing `iceberg-aws-bundle` results in `ClassNotFound: S3FileIO`.

### Step 3 — `storefs.conf`

```properties
spark.sql.extensions=org.apache.iceberg.spark.extensions.IcebergSparkSessionExtensions
spark.sql.catalog.storefs=org.apache.iceberg.spark.SparkCatalog
spark.sql.catalog.storefs.type=rest
spark.sql.catalog.storefs.uri=http://<host>:7946/api/iceberg/<catalog>
spark.sql.catalog.storefs.header.Authorization=Bearer <PAT>
spark.sql.catalog.storefs.io-impl=org.apache.iceberg.aws.s3.S3FileIO

spark.sql.catalog.storefs.s3.endpoint=http://<host>:8901
spark.sql.catalog.storefs.s3.region=us-east-1
spark.sql.catalog.storefs.s3.path-style-access=true
spark.sql.catalog.storefs.s3.access-key-id=<access key>
spark.sql.catalog.storefs.s3.secret-access-key=<secret key>
spark.sql.catalog.storefs.s3.checksum-enabled=false
```

Key points:
- `<catalog>` in `uri` is the StoreFS-side catalog name (e.g. `lakehouse`); **do not include `v1`**
- `header.Authorization` carries the Bearer token for write requests
- `s3.*` point at the StoreFS S3 data plane (`8901`); `io-impl=S3FileIO`
- No warehouse setting needed — the REST config pushes `s3://lakehouse/`

### Step 4 — Start and operate

```bash
sudo docker run -it --rm --name spark-iceberg -e AWS_REGION=us-east-1 \
  -v ~/spark-work:/opt/spark-work apache/spark:3.5.1 \
  /opt/spark/bin/spark-sql --properties-file /opt/spark-work/storefs.conf \
  --jars /opt/spark-work/jars/iceberg-spark-runtime-3.5_2.12-1.6.1.jar,/opt/spark-work/jars/iceberg-aws-bundle-1.6.1.jar
```

```sql
CREATE DATABASE storefs.sales;                       -- create database (namespace)
USE storefs.sales;
CREATE TABLE orders (order_id BIGINT, customer STRING, amount DOUBLE,
  created_at TIMESTAMP) USING iceberg PARTITIONED BY (days(created_at));
INSERT INTO orders VALUES (1,'alice',19.9,TIMESTAMP '2026-08-01 10:00:00'),
  (2,'bob',42.5,TIMESTAMP '2026-08-02 11:30:00');
SELECT customer, sum(amount) FROM orders GROUP BY customer ORDER BY 2 DESC;
UPDATE orders SET amount = amount + 1 WHERE customer = 'alice';   -- row-level update
DELETE FROM orders WHERE customer = 'bob';
```

### Step 5 — Verify (StoreFS side)

```bash
curl -s -H "Authorization: Bearer $TOKEN" "$ADMIN/api/iceberg/admin/tables?namespace=sales"
curl -s -H "Authorization: Bearer $TOKEN" "$ADMIN/api/iceberg/admin/tables/orders/snapshots?namespace=sales"
curl -s -H "Authorization: Bearer $TOKEN" "$ADMIN/api/iceberg/admin/storage-usage"
```

---

## Iceberg REST Catalog (full v1 spec)

Base URL: `http://<host>:7946/api/iceberg/{catalog}/v1/`. **Reads are public; writes need `Authorization: Bearer <token>`**.

| Category | Endpoints |
|----------|-----------|
| Namespaces | `GET/POST /namespaces` · `GET/HEAD/DELETE /namespaces/{ns}` · `POST /namespaces/{ns}/properties` |
| Tables | `GET/POST /namespaces/{ns}/tables` · `GET/HEAD/POST/DELETE /namespaces/{ns}/tables/{t}` · `POST /tables/rename` · `POST /namespaces/{ns}/register` |
| Views | `GET/POST /namespaces/{ns}/views` · `GET/HEAD/POST/DELETE /namespaces/{ns}/views/{v}` · `POST /views/rename` |
| Commit / transactions | `POST /namespaces/{ns}/tables/{t}`(commit) · `POST /transactions/commit` |
| Security | `GET .../credentials` · `POST .../sign` |
| v2 extras | scan planning `/plan`, `/tasks`, metrics |

---

## Lakehouse Maintenance

All operations are safe by design and support `dry_run` to preview first. Operators can also run them one-click via the MCP tools.

| Operation | Endpoint | Behavior |
|-----------|----------|----------|
| Expire snapshots | `POST /admin/tables/{t}/expire?namespace=<ns>` | Refs-aware: keeps main/branches/tags + ancestor chain + latest N; deletes only unreachable snapshots, then prunes manifest-lists after the CAS advance |
| Remove orphan files | `POST /admin/tables/{t}/orphan?namespace=<ns>` | Files not referenced by retained snapshots AND older than the grace period (default 7 days); fail-closed if the reachable set cannot be built |
| Compact metadata | `POST /admin/tables/{t}/compact-metadata?namespace=<ns>` | Rewrites the metadata chain into a single new file; prunes old metadata-log after the CAS advance |

```bash
# Example: dry-run to preview candidates, then execute
curl -s -X POST -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"retain_last":5,"dry_run":true}' "$ADMIN/api/iceberg/admin/tables/orders/expire?namespace=sales"
```

---

## Web Admin Console

| Page | Route | Role |
|------|-------|------|
| Iceberg config | `/system-config/iceberg` | super_admin |
| Lakehouse home (catalogs + namespaces) | `/iceberg` | authenticated |
| Table list | `/iceberg/namespaces/:ns/tables` | authenticated |
| Table detail | `/iceberg/namespaces/:ns/tables/:table` | authenticated |

Features: Iceberg config toggle and warehouse-bucket linking (see [Setup – Method 2](#method-2--web-console-recommended)), catalog creation, drill-down from namespaces to table lists, table detail with Schema / snapshot history / storage usage.

---

## MCP Lakehouse Tools

With the [MCP server](mcp.md) configured, manage the lakehouse in natural language: `storefs_iceberg_get_status` / `enable` / `disable` / `get_config` / `set_config` / `list|get|create|delete_catalog` / `list_namespaces` / `list_tables` / `get_table` / `get_schema` / `list_snapshots` / `list_commits` / `storage_usage` / `expire_snapshots` / `remove_orphan_files` / `compact_metadata` / `query`.

Examples:
> "Create a catalog 'analytics' on warehouse 's3://analytics-wh/'" → `storefs_iceberg_create_catalog(...)`
>
> "Dry-run expire old snapshots of sales.orders keeping 3" → `storefs_iceberg_expire_snapshots(namespace="sales", table="orders", retain_last=3, dry_run=true)`

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Cannot connect to `:7946` | StoreFS not started / wrong IP or port; check `uri` in `storefs.conf` |
| `401` on create table | `header.Authorization` expired; re-login and update the token |
| `ClassNotFound: S3FileIO` | Missing `iceberg-aws-bundle` in `--jars` |
| Write data fails | `s3.access-key-id`/`secret` missing or no write permission on the bucket |
| `checksum`/`400` | Set `spark.sql.catalog.storefs.s3.checksum-enabled=false` |
| `warehouse-location` mismatch | `GET /api/iceberg/{catalog}/v1/config` must match the catalog's warehouse root (e.g. `s3://lakehouse/`) |
| Engine version mismatch | Iceberg runtime jar must match the Spark version (3.5 → 1.6.x) |
| Maintenance returns `409` | A concurrent commit advanced the head; retry |