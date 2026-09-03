**[English](lakehouse.md)**

# StoreFS Lakehouse — Iceberg 湖仓分析

StoreFS 内置了完整的 **湖仓（Lakehouse）** 能力：基于 **Apache Iceberg** 的开放格式分析数据湖，通过标准 **Iceberg REST Catalog** 对外服务。一套 StoreFS 集群同时充当元数据控制面和 S3 对象存储数据面——直接对接 Apache Spark、Flink、Trino、DuckDB，无需外部 Hive Metastore、无需单独 S3。

---

## 快速了解

| 维度 | 说明 |
|------|------|
| **是什么** | 一套 StoreFS = Iceberg REST Catalog（元数据，`7946`）+ S3 兼容数据面（`8901`），自包含湖仓 |
| **怎么用** | 任意 Iceberg 引擎连 REST Catalog 建库建表读写；引擎把 Parquet 数据文件写到 S3 数据面 |
| **核心亮点** | 完整 Iceberg v1 REST 规范 · 多 Catalog 隔离 · 动态 warehouse 桶解析 · 一键维护 · Web 控制台 · MCP 管理 |
| **对接引擎** | Spark / Flink / Trino / DuckDB（任意 Iceberg 客户端） |

### 快速开始（三步）

```bash
ADMIN=http://<host>:7946

# 1. 登录拿 token
TOKEN=$(curl -s -X POST "$ADMIN/api/auth/login" -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"<password>"}' \
  | python3 -c 'import sys,json;print(json.load(sys.stdin).get("token",""))')

# 2. 启用 Iceberg（自动创建 warehouse 桶）
curl -s -X POST "$ADMIN/api/iceberg/admin/enable" -H "Authorization: Bearer $TOKEN"

# 3. 用 Spark 连上建表读写（见下方 [配合 Apache Spark 使用]）
```

---

## 架构

```
┌────────────────────────────────────────────────────────────────┐
│                       Apache Spark / Flink / Trino               │
│                            (Iceberg engine client)               │
└───────────────┬────────────────────────────────┬────────────────┘
                │ 元数据 / 控制                    │ 数据 / 存储
                │ Iceberg REST Catalog           │ S3 兼容 API (SigV4)
                │ HTTP :7946 Bearer token        │ HTTP :8901
                ▼                                ▼
┌────────────────────────────────────────────────────────────────┐
│                          StoreFS Cluster                        │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Admin API :7946                                         │  │
│  │  /api/iceberg/{catalog}/v1/...  → Iceberg REST Catalog   │  │
│  │     namespaces · tables · views · config · create ·      │  │
│  │     commit · transactions · register · credentials/sign  │  │
│  │  /api/iceberg/admin/...        → 控制面 + 维护            │  │
│  └──────────────────────────────────────────────────────────┘  │
│           │ 原生写入路径（LazyDispatcherStore → 对象层）        │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ S3 API :8901 (数据面)                                    │  │
│  │   s3://<warehouse-bucket>/<ns>/<table>/                  │  │
│  │     metadata/*.metadata.json · manifest-list · manifests │  │
│  │     data/*.parquet                                       │  │
│  └──────────────────────────────────────────────────────────┘  │
│  Doris 元数据：iceberg_catalogs · iceberg_tables ·            │
│                iceberg_table_heads · iceberg_commits · …       │
└────────────────────────────────────────────────────────────────┘
```

**关键机制**：
- **Commit 引擎**：幂等、恰好一次、原子 CAS 表头切换（并发提交返回 `409`，不会覆盖）。
- **多 Catalog 隔离**：每个 catalog 拥有独立 warehouse 根（`s3://bucket/prefix/`）。系统不存在隐式 catalog —— catalog 通过管理 API 显式创建，且各自固定自己的 warehouse 根。
- **动态 warehouse 桶解析**：`LazyDispatcherStore` 每次操作动态解析目标桶，切换 warehouse 无需重启。

---

## 设置

### 方式一：Admin API（curl）

见上方 [快速开始](#快速开始三步) 的第 1、2 步。启用后用 `PUT /api/iceberg/admin/config` 调整配置：

```json
{
  "enabled": true,
  "warehouse_bucket": "lakehouse",
  "default_namespace": "default",
  "idempotency_retention_days": 7,
  "orphan_grace_period_hours": 168
}
```

| 配置项 | 默认 | 说明 |
|-------|------|------|
| `warehouse_bucket` | `lakehouse` | 存放所有 Iceberg 元数据 + 数据文件的 S3 桶 |
| `default_namespace` | `default` | 默认命名空间 |
| `idempotency_retention_days` | `7` | 提交幂等记录保留天数 |
| `orphan_grace_period_hours` | `168`(7天) | 孤儿文件判定最小年龄 |

### 方式二：Web 控制台（推荐）

**1. 创建 warehouse 桶**

进入 **Bucket 管理** → **新建 Bucket**，填写：
- **Bucket 名称**：`lakehouse`（或你喜欢的名字）
- **存储策略**：可选，用默认即可
- 其他选项（加密/版本控制/对象锁）按需设置

**2. 启用 Iceberg 并关联该桶**

进入 **系统配置 → Iceberg**：
- 打开 **启用** 开关
- 在 **warehouse_bucket** 填入你刚建的桶名（如 `lakehouse`）
- 点击 **保存**

保存时后端会自动确保该桶存在；若桶由当前用户在 Web 控制台创建，Owner 即为你，权限正确。

**3. 在 Lakehouse 首页创建 Catalog**

进入 **Iceberg / Lakehouse** 页面：
- 在 catalog 下拉旁点击 **创建 Catalog**
- **Catalog 名称**：如 `lakehouse`（或自定义，如 `analytics`）
- **Warehouse**：填 `s3://lakehouse/`（指向 bucket）
- 确认后即可在此 catalog 下用 Spark 建库建表。

> **要点**：系统不存在隐式 `default` catalog，每个 catalog 都需显式创建，并固定自己的 warehouse 根。集群配置中的 `warehouse_bucket` 只用于 `POST /api/iceberg/admin/enable` 时自动创建默认桶，不代表任何 catalog。要让某个 warehouse bucket 可被 Spark 使用：建桶 → 创建一个 warehouse 指向该桶的 catalog。

### 多 Catalog

```bash
# 在任意可写桶上创建额外 catalog
curl -s -X POST "$ADMIN/api/iceberg/admin/catalogs" -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"name":"analytics","warehouse":"s3://analytics-wh/"}'
```

名称规则：小写 `^[a-zA-Z][a-zA-Z0-9_-]{0,63}$`；已删除名称不复用。

---

## 配合 Apache Spark 使用

### Step 1 — 前置

完成 [设置](#设置)，确认 `GET /api/iceberg/{catalog}/v1/config` 返回所建 catalog 的 `warehouse-location`。

### Step 2 — 启动 Spark（Docker）

```bash
mkdir -p ~/spark-work/jars && cd ~/spark-work/jars
wget https://repo1.maven.org/maven2/org/apache/iceberg/iceberg-spark-runtime-3.5_2.12/1.6.1/iceberg-spark-runtime-3.5_2.12-1.6.1.jar
wget https://repo1.maven.org/maven2/org/apache/iceberg/iceberg-aws-bundle/1.6.1/iceberg-aws-bundle-1.6.1.jar
```

> 缺少 `iceberg-aws-bundle` 会报 `ClassNotFound: S3FileIO`。

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

要点：
- `uri` 里的 `<catalog>` 是 StoreFS 侧 catalog 名（如 `lakehouse`），**不要带 `v1`**
- `header.Authorization` 携带写请求的 Bearer token
- `s3.*` 指向 StoreFS S3 数据面（`8901`）；`IO-impl=S3FileIO`
- warehouse 无需设置——REST config 自动下发 `s3://lakehouse/`

### Step 4 — 启动并操作

```bash
sudo docker run -it --rm --name spark-iceberg -e AWS_REGION=us-east-1 \
  -v ~/spark-work:/opt/spark-work apache/spark:3.5.1 \
  /opt/spark/bin/spark-sql --properties-file /opt/spark-work/storefs.conf \
  --jars /opt/spark-work/jars/iceberg-spark-runtime-3.5_2.12-1.6.1.jar,/opt/spark-work/jars/iceberg-aws-bundle-1.6.1.jar
```

```sql
CREATE DATABASE storefs.sales;                       -- 建库（namespace）
USE storefs.sales;
CREATE TABLE orders (order_id BIGINT, customer STRING, amount DOUBLE,
  created_at TIMESTAMP) USING iceberg PARTITIONED BY (days(created_at));
INSERT INTO orders VALUES (1,'alice',19.9,TIMESTAMP '2026-08-01 10:00:00'),
  (2,'bob',42.5,TIMESTAMP '2026-08-02 11:30:00');
SELECT customer, sum(amount) FROM orders GROUP BY customer ORDER BY 2 DESC;
UPDATE orders SET amount = amount + 1 WHERE customer = 'alice';   -- 行级更新
DELETE FROM orders WHERE customer = 'bob';
```

### Step 5 — 验证（StoreFS 侧）

```bash
curl -s -H "Authorization: Bearer $TOKEN" "$ADMIN/api/iceberg/admin/tables?namespace=sales"
curl -s -H "Authorization: Bearer $TOKEN" "$ADMIN/api/iceberg/admin/tables/orders/snapshots?namespace=sales"
curl -s -H "Authorization: Bearer $TOKEN" "$ADMIN/api/iceberg/admin/storage-usage"
```

---

## Iceberg REST Catalog（完整 v1 规范）

基础 URL：`http://<host>:7946/api/iceberg/{catalog}/v1/`。**读公开；写需 `Authorization: Bearer <token>`**。

| 类别 | 端点 |
|------|------|
| 命名空间 | `GET/POST /namespaces` · `GET/HEAD/DELETE /namespaces/{ns}` · `POST /namespaces/{ns}/properties` |
| 表 | `GET/POST /namespaces/{ns}/tables` · `GET/HEAD/POST/DELETE /namespaces/{ns}/tables/{t}` · `POST /tables/rename` · `POST /namespaces/{ns}/register` |
| 视图 | `GET/POST /namespaces/{ns}/views` · `GET/HEAD/POST/DELETE /namespaces/{ns}/views/{v}` · `POST /views/rename` |
| 提交/事务 | `POST /namespaces/{ns}/tables/{t}`(commit) · `POST /transactions/commit` |
| 安全 | `GET .../credentials` · `POST .../sign` |
| v2 扩展 | scan planning `/plan`、`/tasks`、metrics |

---

## 湖仓维护

所有操作安全设计，均支持 `dry_run` 先预览。运维也可用 MCP 工具一键执行。

| 操作 | 端点 | 行为 |
|------|------|------|
| 过期快照 | `POST /admin/tables/{t}/expire?namespace=<ns>` | refs 感知：保留 main/分支/标签 + 祖先链 + 最近 N 个；仅删不可达快照，CAS 推进后清理 manifest-list |
| 清理孤儿文件 | `POST /admin/tables/{t}/orphan?namespace=<ns>` | 不被保留快照引用 且 年龄 > 宽限期(默认7天)；无法构建可达集则 fail-closed |
| 元数据压缩 | `POST /admin/tables/{t}/compact-metadata?namespace=<ns>` | 元数据链重写为单个新文件，CAS 成功后清理旧 metadata-log |

```bash
# 示例：dry-run 查看候选，再执行
curl -s -X POST -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"retain_last":5,"dry_run":true}' "$ADMIN/api/iceberg/admin/tables/orders/expire?namespace=sales"
```

---

## Web 管理控制台

| 页面 | 路由 | 角色 |
|------|------|------|
| Iceberg 配置 | `/system-config/iceberg` | super_admin |
| Lakehouse 首页（catalogs + namespaces） | `/iceberg` | 已认证 |
| 表列表 | `/iceberg/namespaces/:ns/tables` | 已认证 |
| 表详情 | `/iceberg/namespaces/:ns/tables/:table` | 已认证 |

功能：Iceberg 配置开关与 warehouse 桶关联（见 [设置-方式二](#方式二web-控制台推荐)）、catalog 创建、命名空间下钻为表列表、表详情含 Schema / 快照历史 / 存储占用。

---

## MCP 湖仓工具

配置好 [MCP server](mcp.md) 后，可用自然语言管理湖仓：`storefs_iceberg_get_status` / `enable` / `disable` / `get_config` / `set_config` / `list|get|create|delete_catalog` / `list_namespaces` / `list_tables` / `get_table` / `get_schema` / `list_snapshots` / `list_commits` / `storage_usage` / `expire_snapshots` / `remove_orphan_files` / `compact_metadata` / `query`。

示例：
> "Create a catalog 'analytics' on warehouse 's3://analytics-wh/'" → `storefs_iceberg_create_catalog(...)`
>
> "Dry-run expire old snapshots of sales.orders keeping 3" → `storefs_iceberg_expire_snapshots(namespace="sales", table="orders", retain_last=3, dry_run=true)`

---

## 故障排查

| 症状 | 处理 |
|------|------|
| 连 `:7946` 被拒 | StoreFS 未启动 / IP 端口错；检查 `storefs.conf` 的 `uri` |
| 建表 `401` | `header.Authorization` 过期；重新登录更新 token |
| `ClassNotFound: S3FileIO` | `--jars` 缺 `iceberg-aws-bundle` |
| 写数据失败 | `s3.access-key-id/secret` 缺失或桶无写权限 |
| `checksum`/`400` | 设 `spark.sql.catalog.storefs.s3.checksum-enabled=false` |
| `warehouse-location` 不匹配 | `GET /api/iceberg/{catalog}/v1/config` 返回的 `warehouse-location` 与 catalog 的 warehouse 根一致（如 `s3://lakehouse/`） |
| 引擎版本不匹配 | Iceberg runtime jar 与 Spark 版本匹配（3.5→1.6.x） |
| 维护返回 `409` | 并发提交推进了表头；重试 |