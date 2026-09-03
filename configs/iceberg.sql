USE mydb;

-- Iceberg Lakehouse catalog tables.
-- Doris Unique model: UNIQUE KEY(...) must appear AFTER the column list,
-- not inside it. group_commit props required for Doris 4.x.

-- Catalog Registry: each Iceberg catalog owns a warehouse root (s3://bucket/prefix).
-- status supports a soft-delete lifecycle: ACTIVE -> DELETING -> DELETED.
-- catalog names are lowercased and immutable; a DELETED name is not reused.
CREATE TABLE IF NOT EXISTS iceberg_catalogs (
    catalog_name VARCHAR(256) NOT NULL,
    warehouse    VARCHAR(2048) NOT NULL,
    owner_id     BIGINT NOT NULL,
    status       VARCHAR(32) NOT NULL DEFAULT 'ACTIVE',
    properties   VARCHAR(4096) DEFAULT '{}',
    created_at   DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=OLAP
UNIQUE KEY (catalog_name)
DISTRIBUTED BY HASH(catalog_name) BUCKETS 16
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

CREATE TABLE IF NOT EXISTS iceberg_namespaces (
    catalog_name VARCHAR(256) NOT NULL DEFAULT '',
    ns_name     VARCHAR(512) NOT NULL,
    id          BIGINT,
    location    VARCHAR(2048) NOT NULL DEFAULT '',
    properties  VARCHAR(4096) DEFAULT '{}',
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=OLAP
UNIQUE KEY (catalog_name, ns_name)
DISTRIBUTED BY HASH(catalog_name, ns_name) BUCKETS 16
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

CREATE TABLE IF NOT EXISTS iceberg_tables (
    catalog_name VARCHAR(256) NOT NULL DEFAULT '',
    ns_name     VARCHAR(512) NOT NULL,
    table_name  VARCHAR(512) NOT NULL,
    id          BIGINT,
    table_uuid  VARCHAR(64)  NOT NULL,
    location    VARCHAR(2048) NOT NULL,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=OLAP
UNIQUE KEY (catalog_name, ns_name, table_name)
DISTRIBUTED BY HASH(catalog_name, ns_name, table_name) BUCKETS 16
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

-- Staged tables: Iceberg CREATE TABLE ... with stage-create=true writes the
-- metadata to the warehouse but does NOT register the table. The staged entry is
-- recorded here; a later commit that carries the assert-create requirement
-- promotes it into a real iceberg_tables row (+ head). Between stage-create and
-- commit the table must NOT appear in listTables/tableExists.
CREATE TABLE IF NOT EXISTS iceberg_staged_tables (
    catalog_name       VARCHAR(256) NOT NULL DEFAULT '',
    ns_name            VARCHAR(512) NOT NULL,
    table_name         VARCHAR(512) NOT NULL,
    metadata_location  VARCHAR(2048) NOT NULL,
    table_uuid         VARCHAR(64)  NOT NULL DEFAULT '',
    location           VARCHAR(2048) NOT NULL DEFAULT '',
    created_at         DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=OLAP
UNIQUE KEY (catalog_name, ns_name, table_name)
DISTRIBUTED BY HASH(catalog_name, ns_name, table_name) BUCKETS 16
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

CREATE TABLE IF NOT EXISTS iceberg_table_heads (
    catalog_name       VARCHAR(256) NOT NULL DEFAULT '',
    table_id           BIGINT       NOT NULL,
    head_generation    BIGINT       NOT NULL DEFAULT 0,
    metadata_location  VARCHAR(2048) NOT NULL,
    metadata_etag      VARCHAR(128) NOT NULL DEFAULT '',   -- S3 ETag of metadata_location
    metadata_sha256    VARCHAR(64)  NOT NULL DEFAULT '',   -- SHA-256 of metadata.json (hex, 64 chars)
    metadata_version   INT          NOT NULL DEFAULT 0,    -- Iceberg metadata version fast-path
    snapshot_id        BIGINT       NOT NULL DEFAULT 0,    -- current snapshot id fast-path
    last_commit_id     VARCHAR(64)  NOT NULL DEFAULT '',
    updated_at         DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=OLAP
UNIQUE KEY (catalog_name, table_id)
DISTRIBUTED BY HASH(table_id) BUCKETS 16
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

CREATE TABLE IF NOT EXISTS iceberg_commit_idempotency (
    table_id           BIGINT       NOT NULL,
    commit_id          VARCHAR(64)  NOT NULL,
    metadata_location  VARCHAR(2048) NOT NULL,
    snapshot_id        BIGINT,
    request_digest     CHAR(64)     NOT NULL,
    attempt_id         VARCHAR(64)  NOT NULL,
    created_at         DATETIME DEFAULT CURRENT_TIMESTAMP,
    expires_at         DATETIME NULL
) ENGINE=OLAP
UNIQUE KEY (table_id, commit_id)
DISTRIBUTED BY HASH(table_id) BUCKETS 32
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

CREATE TABLE IF NOT EXISTS iceberg_commits (
    table_id                BIGINT       NOT NULL,
    attempt_id              VARCHAR(64)  NOT NULL,
    head_generation         BIGINT,
    base_head_generation    BIGINT       NOT NULL,
    commit_id               VARCHAR(64)  NOT NULL,
    base_metadata_location  VARCHAR(2048) NOT NULL,
    metadata_location       VARCHAR(2048),
    writer_id               VARCHAR(64)  NOT NULL,
    attempt_status          VARCHAR(16)  NOT NULL DEFAULT 'committed',
    created_at              DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=OLAP
UNIQUE KEY (table_id, attempt_id)
DISTRIBUTED BY HASH(table_id) BUCKETS 64
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);
-- Views: identity + metadata pointer only (view metadata JSON lives in S3),
-- mirroring the table identity model.
CREATE TABLE IF NOT EXISTS iceberg_views (
    catalog_name    VARCHAR(256) NOT NULL DEFAULT '',
    ns_name         VARCHAR(512) NOT NULL,
    view_name       VARCHAR(512) NOT NULL,
    id              BIGINT,
    view_uuid       VARCHAR(64)  NOT NULL,
    location        VARCHAR(2048) NOT NULL,
    metadata_location VARCHAR(2048) NOT NULL DEFAULT '',
    properties      VARCHAR(4096) DEFAULT '{}',
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=OLAP
UNIQUE KEY (catalog_name, ns_name, view_name)
DISTRIBUTED BY HASH(catalog_name, ns_name, view_name) BUCKETS 16
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

-- Functions: namespace + name key, with the SQL/dialect body stored inline.
CREATE TABLE IF NOT EXISTS iceberg_functions (
    catalog_name VARCHAR(256) NOT NULL DEFAULT '',
    ns_name      VARCHAR(512) NOT NULL,
    function_name VARCHAR(512) NOT NULL,
    id           BIGINT,
    kind         VARCHAR(32)  NOT NULL DEFAULT 'scalar',
    body         VARCHAR(16384) NOT NULL DEFAULT '',
    dialect      VARCHAR(32)  NOT NULL DEFAULT 'spark_sql',
    properties   VARCHAR(4096) DEFAULT '{}',
    created_at   DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=OLAP
UNIQUE KEY (catalog_name, ns_name, function_name)
DISTRIBUTED BY HASH(catalog_name, ns_name, function_name) BUCKETS 16
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

-- Server-side scan planning: opaque plan-id -> status + result JSON.
CREATE TABLE IF NOT EXISTS iceberg_scan_plans (
    plan_id      VARCHAR(64) NOT NULL,
    catalog_name VARCHAR(256) NOT NULL DEFAULT '',
    table_id     BIGINT      NOT NULL,
    status       VARCHAR(16) NOT NULL DEFAULT 'submitted',
    result_json  VARCHAR(16384) DEFAULT '{}',
    created_at   DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at   DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=OLAP
UNIQUE KEY (plan_id)
DISTRIBUTED BY HASH(plan_id) BUCKETS 16
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

