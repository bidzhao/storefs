CREATE DATABASE IF NOT EXISTS mydb;
USE mydb;

CREATE TABLE IF NOT EXISTS catalog_scan_claims
(
    bucket_id        BIGINT      NOT NULL,
    range_idx        INT         NOT NULL,                               -- Hash range index: 0-255
    instance_id      VARCHAR(64) NOT NULL DEFAULT '',                    -- Instance holding the lease
    completed_at     DATETIME    NULL,                                   -- NULL = not completed, set to NOW() when done
    processed_count  BIGINT      NOT NULL DEFAULT '0',
    total_count      BIGINT      NOT NULL DEFAULT '0',
    high_write_time  DATETIME    NOT NULL DEFAULT '1970-01-01 00:00:00'  -- Incremental scan watermark
)
ENGINE = OLAP
UNIQUE KEY (bucket_id, range_idx)
DISTRIBUTED BY HASH(bucket_id, range_idx) BUCKETS 128
PROPERTIES (
    "replication_num" = "1",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);

CREATE TABLE IF NOT EXISTS catalog_instances
  (
      instance_id    VARCHAR(64) NOT NULL,
      hostname       VARCHAR(128) NOT NULL DEFAULT '',
      pid            INT          NOT NULL DEFAULT '0',
      started_at     DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
      last_heartbeat DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP
  )
ENGINE = OLAP
UNIQUE KEY (instance_id)
DISTRIBUTED BY HASH(instance_id) BUCKETS 16
PROPERTIES (
    "replication_num" = "1",
    "enable_unique_key_merge_on_write" = "true",
    "group_commit_mode" = "sync_mode",
    "group_commit_interval_ms" = "10",
    "group_commit_data_bytes" = "67108864"
);