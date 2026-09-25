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
    high_write_time  DATETIME    NOT NULL DEFAULT '1970-01-01 00:00:00', -- Incremental scan watermark
    PRIMARY KEY (bucket_id, range_idx)
);

CREATE TABLE IF NOT EXISTS catalog_instances
(
    instance_id    VARCHAR(64) NOT NULL,
    hostname       VARCHAR(128) NOT NULL DEFAULT '',
    pid            INT          NOT NULL DEFAULT '0',
    started_at     DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_heartbeat DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (instance_id)
);
