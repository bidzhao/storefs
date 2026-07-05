**[查看中文版](metrics_cn.md)**

# StoreFS Monitoring Guide

This document introduces the StoreFS monitoring and alerting system, covering the overall architecture, deployment, metrics list, Grafana dashboards, and notification alert configuration.

---

## 1. Overall Architecture

StoreFS uses the standard **Prometheus + Grafana + Alertmanager** stack for monitoring and alerting:

```
┌──────────────────┐     scrape(/metrics)     ┌──────────────┐
│  StoreFS Node 1  │◄─────────────────────────┤              │
│  (port 7946)     │                          │              │
├──────────────────┤                          │  Prometheus  │
│  StoreFS Node 2  │◄─────────────────────────┤  (port 9090) │
│  (port 7947)     │                          │              │
├──────────────────┤                          │              │
│  StoreFS Node N  │◄─────────────────────────┤              │
│  (port 7948)     │                          └──────┬───────┘
└──────────────────┘                                 │
                                                      │
                        ┌─────────────────────────────┼──────────────────────────┐
                        │                             │                          │
                        ▼                             ▼                          ▼
                 ┌───────────┐              ┌─────────────────┐       ┌────────────────────┐
                 │  Grafana  │              │  Alertmanager   │       │  Alert Rules       │
                 │ (port 3000)│              │  (port 9093)    │       │  (storefs_alerts.yml)│
                 │           │              │                 │       └────────────────────┘
                 │ Dashboards│              │ Slack / Email   │
                 └───────────┘              │ Webhook / No-op │
                                            └─────────────────┘
```

### Flow

1. **Metrics Collection**: Each StoreFS node exposes a `/metrics` endpoint on the admin port (same port as cluster gossip, configurable via `node.port`). The handler returns metrics in Prometheus text-based exposition format (Content-Type: `text/plain; version=0.0.4`).

2. **Metrics Scraping**: Prometheus periodically scrapes the `/metrics` endpoint of all StoreFS nodes according to the `scrape_interval` (default: 15s) defined in `prometheus.yml`.

3. **Alert Evaluation**: Prometheus evaluates alert rules defined in `alerts/storefs_alerts.yml` against the scraped metrics. When a rule condition is met for the specified duration (`for`), the alert fires and is forwarded to Alertmanager.

4. **Alert Notification**: Alertmanager receives fired alerts, groups them according to routing rules, and dispatches notifications through configured receivers (Slack, Email, Webhook, or no-op default).

5. **Visualization**: Grafana reads metrics from Prometheus and provides pre-configured dashboards for visualizing node-level and cluster-level metrics.

### Key Components

| Component | Image | Port | Purpose |
|-----------|-------|------|---------|
| Prometheus | prom/prometheus:v3.5.4 | 9090 | Metrics storage and alert rule evaluation |
| Grafana | grafana/grafana:13.1.0 | 3000 | Metrics visualization and dashboards |
| Alertmanager | prom/alertmanager:v0.28.1 | 9093 | Alert notification routing and delivery |

### Metrics Endpoint

The metrics endpoint is available at `http://<node-ip>:<node-port>/metrics` and does **not** require authentication so that Prometheus can scrape it directly.

Each metric carries the following common labels:
- `node`: The StoreFS node name (from config `cluster.node.name`)
- `ip`: The StoreFS node IP address (from config `cluster.node.ip`)

---

## 2. Deployment

### Prerequisites

- Docker and Docker Compose installed on the monitoring host
- StoreFS cluster running and accessible from the monitoring host

### Directory Structure

```
monitor/
├── docker-compose.yml           # Docker Compose file
├── prometheus.yml               # Prometheus configuration
├── alertmanager.yml             # Alertmanager configuration
├── alertmanager.tmpl            # Notification templates
├── alerts/
│   └── storefs_alerts.yml       # Alert rules definition
└── grafana/
    └── provisioning/
        ├── dashboards/
        │   ├── dashboard.yml                    # Grafana dashboard provisioning config
        │   ├── storefs.json                     # Node Metrics dashboard
        │   └── storefs-all-nodes-summary.json   # All Nodes Summary dashboard
        └── datasources/
            └── datasource.yml     # Prometheus datasource config
```

### Step-by-Step Deployment

#### 1. Configure Prometheus Targets

Edit `monitor/prometheus.yml` to set the correct StoreFS node addresses:

```yaml
scrape_configs:
  - job_name: "storefs-metrics"
    static_configs:
      - targets:
          - "<host1>:7946"   # Node 1
          - "<host2>:7947"   # Node 2
          - "<host3>:7948"   # Node 3
        labels:
          app: "storefs"
```

> **Note:** When StoreFS runs on the host machine and monitoring runs in Docker, use `host.docker.internal` instead of IP addresses for cross-container communication (already pre-configured with `extra_hosts: ["host.docker.internal:host-gateway"]` in the Prometheus service).

#### 2. Configure Grafana Credentials (Optional)

Edit `monitor/docker-compose.yml` to change the default Grafana admin credentials:

```yaml
environment:
  - GF_SECURITY_ADMIN_USER=admin
  - GF_SECURITY_ADMIN_PASSWORD=admin123
```

#### 3. Start the Monitoring Stack

```bash
cd monitor
docker-compose up -d
```

This starts Prometheus, Grafana, and Alertmanager.

#### 4. Verify Deployment

- **Prometheus**: Open http://localhost:9090, go to Status > Targets to verify all StoreFS nodes are UP.
- **Grafana**: Open http://localhost:3000, log in with the configured credentials, navigate to Dashboards to find the pre-provisioned StoreFS dashboards.
- **Alertmanager**: Open http://localhost:9093 to view fired alerts.

#### 5. Stop the Monitoring Stack

```bash
cd monitor
docker-compose down
```

To also remove persistent data volumes:

```bash
docker-compose down -v
```

---

## 3. Metrics Data List

StoreFS exposes the following categories of metrics via the `/metrics` endpoint. All metrics except Go runtime metrics use the `storefs_` prefix. Each metric carries common labels `node` and `ip`.

### 3.1 CPU Metrics

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_cpu_usage_percent` | Gauge | CPU usage percentage (overall) |

### 3.2 Uptime Metrics

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_host_uptime_seconds` | Gauge | Host uptime in seconds (time since system boot) |
| `storefs_node_uptime_seconds` | Counter | Node uptime in seconds (time since StoreFS process start) |

### 3.3 Memory Metrics

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_memory_total_bytes` | Gauge | Total physical memory in bytes |
| `storefs_memory_used_bytes` | Gauge | Used physical memory in bytes |
| `storefs_memory_usage_percent` | Gauge | Memory usage percentage |

### 3.4 Disk Usage Metrics (per configured disk path)

Labels: `path` (configured disk path)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_disk_total_bytes` | Gauge | Total disk capacity in bytes |
| `storefs_disk_used_bytes` | Gauge | Used disk space in bytes |
| `storefs_disk_usage_percent` | Gauge | Disk usage percentage |

### 3.5 Disk I/O Metrics (per device)

Labels: `device` (e.g., `nvme0`, `sda1`)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_disk_reads_completed_total` | Counter | Successful reads completed |
| `storefs_disk_writes_completed_total` | Counter | Successful writes completed |
| `storefs_disk_read_bytes_total` | Counter | Bytes read from disk |
| `storefs_disk_write_bytes_total` | Counter | Bytes written to disk |
| `storefs_disk_read_time_ms_total` | Counter | Time spent reading in ms |
| `storefs_disk_write_time_ms_total` | Counter | Time spent writing in ms |

### 3.6 Network I/O Metrics (per interface)

Labels: `interface` (e.g., `eth0`, `lo`)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_network_receive_bytes_total` | Counter | Bytes received |
| `storefs_network_transmit_bytes_total` | Counter | Bytes transmitted |
| `storefs_network_receive_packets_total` | Counter | Packets received |
| `storefs_network_transmit_packets_total` | Counter | Packets transmitted |

### 3.7 Object Operation Metrics (cumulative counters)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_download_object_start_total` | Counter | Total number of download (ReadObject) requests started |
| `storefs_download_object_complete_total` | Counter | Total number of download (ReadObject) requests completed |
| `storefs_upload_object_start_total` | Counter | Total number of upload (WriteObject) requests started |
| `storefs_upload_object_complete_total` | Counter | Total number of upload (WriteObject) requests completed |

### 3.8 Fragment Operation Metrics (cumulative counters)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_write_fragment_start_total` | Counter | Total number of fragment write operations started |
| `storefs_write_fragment_complete_total` | Counter | Total number of fragment write operations completed |
| `storefs_read_fragment_start_total` | Counter | Total number of fragment read operations started |
| `storefs_read_fragment_complete_total` | Counter | Total number of fragment read operations completed |

### 3.9 Multipart Upload Metrics (cumulative counters)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_create_multipart_start_total` | Counter | Total number of CreateMultipartUpload requests started |
| `storefs_create_multipart_complete_total` | Counter | Total number of CreateMultipartUpload requests completed |
| `storefs_upload_part_start_total` | Counter | Total number of UploadPart requests started |
| `storefs_upload_part_complete_total` | Counter | Total number of UploadPart requests completed |
| `storefs_complete_parts_start_total` | Counter | Total number of CompleteParts requests started |
| `storefs_complete_parts_complete_total` | Counter | Total number of CompleteParts requests completed |
| `storefs_abort_multipart_start_total` | Counter | Total number of AbortMultipartUpload requests started |
| `storefs_abort_multipart_complete_total` | Counter | Total number of AbortMultipartUpload requests completed |

### 3.10 Go Runtime Metrics

| Metric Name | Type | Description |
|-------------|------|-------------|
| `go_goroutines` | Gauge | Number of goroutines that currently exist |
| `go_memstats_gc_cpu_fraction` | Gauge | Fraction of available CPU time used by GC since program start |
| `go_memory_gc_cycles_count` | Counter | Count of completed GC cycles |
| `go_memory_gc_pause_duration` | Gauge | Total GC pause duration in seconds |
| `go_memory_used` | Gauge | Number of heap bytes allocated and still in use |

### 3.11 Hot Bucket Metrics (per node, trailing 2 minutes)

These are locally computed per node using a sliding window (2-minute window with 5-second slot granularity). The top 100 buckets are exported.

Labels: `bucket`

| Metric Name | Type | Description |
|-------------|------|-------------|
| `storefs_upload_hot_bucket_2m` | Gauge | Upload count per bucket in trailing 2 minutes (top 100) |
| `storefs_download_hot_bucket_2m` | Gauge | Download count per bucket in trailing 2 minutes (top 100) |
| `storefs_upload_part_hot_bucket_2m` | Gauge | UploadPart count per bucket in trailing 2 minutes (top 100) |
| `storefs_multipart_complete_hot_bucket_2m` | Gauge | CompleteMultipartUpload count per bucket in trailing 2 minutes (top 100) |

---

## 4. Grafana Dashboards

StoreFS provides two pre-provisioned Grafana dashboards. They are automatically loaded when the Grafana container starts (configured via `grafana/provisioning/`).

### 4.1 Dashboard: StoreFS / Node Metrics

**Purpose**: Detailed view of a single StoreFS node. Use the "Node" dropdown to select which node to inspect.

**Refresh**: Every 30s | **Default Time Range**: Last 1 hour | **Timezone**: browser

**Template Variables**:
- `$node` (dropdown): Select a node, populated from `label_values(storefs_cpu_usage_percent, node)`
- `$path`, `$device`, `$interface`: Auto-populated based on selected node (hidden in UI)

#### Overview Row (non-collapsed)

| Panel | Type | Query | Unit / Description |
|-------|------|-------|-------------------|
| CPU Usage | Gauge | `storefs_cpu_usage_percent{node="$node"}` | percent (thresholds: green<70, orange<90, red>=90) |
| Memory Usage | Gauge | `storefs_memory_usage_percent{node="$node"}` | percent (thresholds: green<70, orange<90, red>=90) |
| Disk Usage | Bar Gauge | `storefs_disk_usage_percent{node="$node"}` legend=`{{path}}` | percent (thresholds: green<80, orange<95, red>=95) |
| Memory Total | Stat | `storefs_memory_total_bytes{node="$node"}` | bytes |
| Memory Used | Stat | `storefs_memory_used_bytes{node="$node"}` | bytes |
| Disk Capacity | Stat | `storefs_disk_total_bytes{node="$node"}` legend=`{{path}}` | bytes |
| Host Uptime | Stat | `storefs_host_uptime_seconds{node="$node"}` | duration (dtdurations) |
| Node Uptime | Stat | `storefs_node_uptime_seconds{node="$node"}` | duration (dtdurations) |
| Upload Object Count | Stat | `storefs_upload_object_start_total{node="$node"}` | Cumulative upload requests started |
| Download Object Count | Stat | `storefs_download_object_start_total{node="$node"}` | Cumulative download requests started |
| Multipart Create Count | Stat | `storefs_create_multipart_start_total{node="$node"}` | Cumulative multipart create requests |
| Multipart Complete Count | Stat | `storefs_complete_parts_start_total{node="$node"}` | Cumulative complete-parts requests |

<!-- TODO: Insert screenshot of Overview row -->

#### Object Task Row

| Panel | Type | Query | Description |
|-------|------|-------|-------------|
| Upload Object Request Count | Stat | `increase(storefs_upload_object_start_total{node="$node"}[$__rate_interval])` | Upload request rate |
| Upload Object Complete Count | Stat | `increase(storefs_upload_object_complete_total{node="$node"}[$__rate_interval])` | Upload completion rate |
| Upload Object Processing Count | Stat | `storefs_upload_object_start_total{node="$node"} - storefs_upload_object_complete_total{node="$node"}` | Currently inflight uploads |
| Download Object Request Count | Stat | `increase(storefs_download_object_start_total{node="$node"}[$__rate_interval])` | Download request rate |
| Download Object Complete Count | Stat | `increase(storefs_download_object_complete_total{node="$node"}[$__rate_interval])` | Download completion rate |
| Download Object Processing Count | Stat | `storefs_download_object_start_total{node="$node"} - storefs_download_object_complete_total{node="$node"}` | Currently inflight downloads |
| Upload Object | Time Series | `increase(...start_total[$__rate_interval])` (Request), `increase(...complete_total[$__rate_interval])` (Complete), `start_total - complete_total` (Processing) | Upload trend |
| Download Object | Time Series | `increase(...start_total[$__rate_interval])` (Request), `increase(...complete_total[$__rate_interval])` (Complete), `start_total - complete_total` (Processing) | Download trend |

<!-- TODO: Insert screenshot of Object Task row -->

#### Multipart Task Row

This row contains 4 Time Series panels (no Stat panels):

| Panel | Type | Queries | Description |
|-------|------|---------|-------------|
| Create Multipart | Time Series | `increase(storefs_create_multipart_start_total{...[}...)`, `increase(...complete_total[...])`, `start_total - complete_total` | CreateMultipart request, completion, inflight trend |
| Upload Part | Time Series | `increase(storefs_upload_part_start_total{...[}...)`, `increase(...complete_total[...])`, `start_total - complete_total` | UploadPart request, completion, inflight trend |
| Complete Multipart | Time Series | `increase(storefs_complete_parts_start_total{...[}...)`, `increase(...complete_total[...])`, `start_total - complete_total` | CompleteParts request, completion, inflight trend |
| Abort Multipart | Time Series | `increase(storefs_abort_multipart_start_total{...[}...)`, `increase(...complete_total[...])`, `start_total - complete_total` | AbortMultipart request, completion, inflight trend |

Each time series panel shows 3 lines:
- `increase(storefs_<op>_start_total{node="$node"}[$__rate_interval])` (Request total)
- `increase(storefs_<op>_complete_total{node="$node"}[$__rate_interval])` (Complete total)
- `storefs_<op>_start_total{node="$node"} - storefs_<op>_complete_total{node="$node"}` (Processing)

<!-- TODO: Insert screenshot of Multipart Task row -->

#### Fragment Task Row

| Panel | Type | Query | Description |
|-------|------|-------|-------------|
| Write Fragment Request Count | Stat | `increase(storefs_write_fragment_start_total{node="$node"}[$__rate_interval])` | Write request rate |
| Write Fragment Complete Count | Stat | `increase(storefs_write_fragment_complete_total{node="$node"}[$__rate_interval])` | Write completion rate |
| Write Fragment Processing Count | Stat | `storefs_write_fragment_start_total{node="$node"} - storefs_write_fragment_complete_total{node="$node"}` | Currently inflight writes |
| Read Fragment Request Count | Stat | `increase(storefs_read_fragment_start_total{node="$node"}[$__rate_interval])` | Read request rate |
| Read Fragment Complete Count | Stat | `increase(storefs_read_fragment_complete_total{node="$node"}[$__rate_interval])` | Read completion rate |
| Read Fragment Processing Count | Stat | `storefs_read_fragment_start_total{node="$node"} - storefs_read_fragment_complete_total{node="$node"}` | Currently inflight reads |
| Write Fragment | Time Series | `increase(...start_total[$__rate_interval])`, `increase(...complete_total[$__rate_interval])`, `start_total - complete_total` | Write request, completion, inflight trend |
| Read Fragment | Time Series | `increase(...start_total[$__rate_interval])`, `increase(...complete_total[$__rate_interval])`, `start_total - complete_total` | Read request, completion, inflight trend |

<!-- TODO: Insert screenshot of Fragment Task row -->

#### Bucket Task Row

| Panel | Type | Query | Description |
|-------|------|-------|-------------|
| Top20 Hot Buckets (Object Upload) | Table | `topk(20, storefs_upload_hot_bucket_2m{node="$node"})` | Top 20 upload buckets (trailing 2 min) |
| Top20 Hot Buckets (Object Download) | Table | `topk(20, storefs_download_hot_bucket_2m{node="$node"})` | Top 20 download buckets (trailing 2 min) |
| Top20 Hot Buckets (Upload Part) | Table | `topk(20, storefs_upload_part_hot_bucket_2m{node="$node"})` | Top 20 UploadPart buckets (trailing 2 min) |
| Top20 Hot Buckets (Multipart Complete) | Table | `topk(20, storefs_multipart_complete_hot_bucket_2m{node="$node"})` | Top 20 CompleteMultipart buckets (trailing 2 min) |

<!-- TODO: Insert screenshot of Bucket Task row -->

#### GC Row (collapsed by default)

Stat panels (instant values):

| Panel | Query | Unit |
|-------|-------|------|
| GC Cycles | `go_memory_gc_cycles_count{node="$node"}` | short |
| GC Pause Duration | `go_memory_gc_pause_duration{node="$node"}` | seconds |
| GC CPU Fraction | `go_memstats_gc_cpu_fraction{node="$node"}` | percentunit (0-1) |
| Goroutines | `go_goroutines{node="$node"}` | short |
| Go Memory Used | `go_memory_used{node="$node"}` | bytes |

Time series panels (historical trends):

| Panel | Query | Unit |
|-------|-------|------|
| GC Cycles Rate | `rate(go_memory_gc_cycles_count{node="$node"}[1m])` | ops |
| GC Pause Duration | `go_memory_gc_pause_duration{node="$node"}` | seconds |
| GC CPU Fraction | `go_memstats_gc_cpu_fraction{node="$node"}` | percentunit |
| Goroutines | `go_goroutines{node="$node"}` | short |
| Go Memory Used | `go_memory_used{node="$node"}` | bytes |

<!-- TODO: Insert screenshot of GC row (expanded) -->

#### CPU & Memory Row

| Panel | Type | Query | Unit |
|-------|------|-------|------|
| CPU Usage | Time Series | `storefs_cpu_usage_percent{node="$node"}` | percent |
| Memory | Time Series | `storefs_memory_total_bytes{node="$node"}` (Total), `storefs_memory_used_bytes{node="$node"}` (Used) | bytes |

<!-- TODO: Insert screenshot of CPU & Memory row -->

#### Disk Row

| Panel | Type | Queries | Unit |
|-------|------|---------|------|
| Disk Usage % | Time Series | `storefs_disk_usage_percent{node="$node"}` legend=`{{path}}` | percent |
| Disk Throughput | Time Series | `rate(storefs_disk_read_bytes_total{node="$node"}[$__rate_interval])` (Read `{{device}}`), `rate(storefs_disk_write_bytes_total{node="$node"}[$__rate_interval])` (Write `{{device}}`) | Bps |
| Disk IOPS | Time Series | `rate(storefs_disk_reads_completed_total{node="$node"}[$__rate_interval])` (Read `{{device}}`), `rate(storefs_disk_writes_completed_total{node="$node"}[$__rate_interval])` (Write `{{device}}`) | iops |

<!-- TODO: Insert screenshot of Disk row -->

#### Network Row

| Panel | Type | Queries | Unit |
|-------|------|---------|------|
| Network Traffic (bits) | Time Series | `rate(storefs_network_receive_bytes_total{node="$node"}[$__rate_interval]) * 8` (RX `{{interface}}`), `rate(storefs_network_transmit_bytes_total{node="$node"}[$__rate_interval]) * 8` (TX `{{interface}}`) | bps |
| Network Packets | Time Series | `rate(storefs_network_receive_packets_total{node="$node"}[$__rate_interval])` (RX `{{interface}}`), `rate(storefs_network_transmit_packets_total{node="$node"}[$__rate_interval])` (TX `{{interface}}`) | pps |

<!-- TODO: Insert screenshot of Network row -->

### 4.2 Dashboard: StoreFS / All Nodes Summary Metrics

**Purpose**: Cluster-wide summary view across all nodes. Shows aggregated operation counts and resource usage.

**Refresh**: Every 10s | **Default Time Range**: Last 30 minutes

#### Cluster Overview Row

| Panel | Type | Query | Description |
|-------|------|-------|-------------|
| Node Uptime | Stat | `storefs_node_uptime_seconds{}` legend=`{{node}}` | Uptime for each node (duration format). Display name overrides: node1, node2, node3 |

<!-- TODO: Insert screenshot -->

#### Object Task Row (cluster-wide)

Stat panels:

| Panel | Query | Description |
|-------|-------|-------------|
| Total Upload Request | `sum(increase(storefs_upload_object_start_total{}[$__rate_interval]))` | Aggregated upload request rate across all nodes |
| Total Upload Complete | `sum(increase(storefs_upload_object_complete_total{}[$__rate_interval]))` | Aggregated upload completion rate |
| Total Upload Processing | `sum(storefs_upload_object_start_total{}) - sum(storefs_upload_object_complete_total{})` | Total inflight uploads |
| Total Download Request | `sum(increase(storefs_download_object_start_total{}[$__rate_interval]))` | Aggregated download request rate |
| Total Download Complete | `sum(increase(storefs_download_object_complete_total{}[$__rate_interval]))` | Aggregated download completion rate |
| Total Download Processing | `sum(storefs_download_object_start_total{}) - sum(storefs_download_object_complete_total{})` | Total inflight downloads |

Time series panel:

| Panel | Queries |
|-------|---------|
| Total Object Operations | `sum(increase(...upload_object_start_total{}[$__rate_interval]))` (Upload), `sum(increase(...download_object_start_total{}[$__rate_interval]))` (Download), `sum(...upload_start_total - upload_complete_total) + sum(...download_start_total - download_complete_total)` (Processing) |

<!-- TODO: Insert screenshot -->

#### Multipart Task Row (cluster-wide)

12 Stat panels (4 operations x 3 metrics each):

| Panel | Query |
|-------|-------|
| Total CreateMultipart Request | `sum(increase(storefs_create_multipart_start_total{}[$__rate_interval]))` |
| Total CreateMultipart Complete | `sum(increase(storefs_create_multipart_complete_total{}[$__rate_interval]))` |
| Total CreateMultipart Processing | `sum(storefs_create_multipart_start_total{}) - sum(storefs_create_multipart_complete_total{})` |
| Total UploadPart Request | `sum(increase(storefs_upload_part_start_total{}[$__rate_interval]))` |
| Total UploadPart Complete | `sum(increase(storefs_upload_part_complete_total{}[$__rate_interval]))` |
| Total UploadPart Processing | `sum(storefs_upload_part_start_total{}) - sum(storefs_upload_part_complete_total{})` |
| Total CompleteParts Request | `sum(increase(storefs_complete_parts_start_total{}[$__rate_interval]))` |
| Total CompleteParts Complete | `sum(increase(storefs_complete_parts_complete_total{}[$__rate_interval]))` |
| Total CompleteParts Processing | `sum(storefs_complete_parts_start_total{}) - sum(storefs_complete_parts_complete_total{})` |
| Total AbortMultipart Request | `sum(increase(storefs_abort_multipart_start_total{}[$__rate_interval]))` |
| Total AbortMultipart Complete | `sum(increase(storefs_abort_multipart_complete_total{}[$__rate_interval]))` |
| Total AbortMultipart Processing | `sum(storefs_abort_multipart_start_total{}) - sum(storefs_abort_multipart_complete_total{})` |

Time series panel:

| Panel | Queries |
|-------|---------|
| Total Multipart Operations | 5 lines: `sum(increase(...create_multipart_start_total{}[...]))` (CreateMultipart), `sum(increase(...upload_part_start_total{}[...]))` (UploadPart), `sum(increase(...complete_parts_start_total{}[...]))` (CompleteParts), `sum(increase(...abort_multipart_start_total{}[...]))` (AbortMultipart), sum of all processing formulas (Processing) |

<!-- TODO: Insert screenshot -->

#### Fragment Task Row (cluster-wide)

6 Stat panels:

| Panel | Query |
|-------|-------|
| Total Fragment Write Request | `sum(increase(storefs_write_fragment_start_total{}[$__rate_interval]))` |
| Total Fragment Write Complete | `sum(increase(storefs_write_fragment_complete_total{}[$__rate_interval]))` |
| Total Fragment Write Processing | `sum(storefs_write_fragment_start_total{}) - sum(storefs_write_fragment_complete_total{})` |
| Total Fragment Read Request | `sum(increase(storefs_read_fragment_start_total{}[$__rate_interval]))` |
| Total Fragment Read Complete | `sum(increase(storefs_read_fragment_complete_total{}[$__rate_interval]))` |
| Total Fragment Read Processing | `sum(storefs_read_fragment_start_total{}) - sum(storefs_read_fragment_complete_total{})` |

Time series panel:

| Panel | Queries |
|-------|---------|
| Total Fragment Operations | 3 lines: `sum(increase(...write_fragment_start_total{}[...]))` (Write), `sum(increase(...read_fragment_start_total{}[...]))` (Read), `sum(...write_fragment_start_total - write_fragment_complete_total) + sum(...read_fragment_start_total - read_fragment_complete_total)` (Processing) |

<!-- TODO: Insert screenshot -->

#### Bucket Task Row (All Nodes)

| Panel | Type | Query |
|-------|------|-------|
| Top 10 Hot Buckets (Upload, All Nodes) | Table | `topk(10, sum by (bucket) (storefs_upload_hot_bucket_2m))` |
| Top 10 Hot Buckets (Download, All Nodes) | Table | `topk(10, sum by (bucket) (storefs_download_hot_bucket_2m))` |
| Top 10 Hot Buckets (Upload Part, All Nodes) | Table | `topk(10, sum by (bucket) (storefs_upload_part_hot_bucket_2m))` |
| Top 10 Hot Buckets (Multipart Complete, All Nodes) | Table | `topk(10, sum by (bucket) (storefs_multipart_complete_hot_bucket_2m))` |

<!-- TODO: Insert screenshot -->

#### Cluster Resource Summary Row

| Panel | Type | Query | Unit |
|-------|------|-------|------|
| Free Disk by Node | Time Series | `100 - (avg by (node) (storefs_disk_usage_percent{}))` legend=`{{node}} free` | percent |
| CPU by Node | Time Series | `avg by (node) (storefs_cpu_usage_percent{})` legend=`{{node}}` | percent |
| Memory by Node | Time Series | `avg by (node) (storefs_memory_usage_percent{})` legend=`{{node}}` | percent |

<!-- TODO: Insert screenshot -->

---

## 5. Notification Alert Configuration

### 5.1 Alert Rules

Alert rules are defined in `monitor/alerts/storefs_alerts.yml` and evaluated by Prometheus every 15s.

| Alert Name | Condition | For | Severity | Description |
|------------|-----------|-----|----------|-------------|
| `StoreFSNodeDown` | `up{job="storefs-metrics"} == 0` | 1m | critical | Node is unreachable by Prometheus |
| `StoreFSNodeRestarted` | `storefs_node_uptime_seconds < 60` | 1m | critical | Node process just restarted (< 60s ago) |
| `StoreFSDiskUsageWarning` | `storefs_disk_usage_percent > 75` | 2m | warning | Disk usage exceeds 75% |
| `StoreFSDiskUsageCritical` | `storefs_disk_usage_percent > 90` | 1m | critical | Disk usage exceeds 90% |
| `StoreFSHighCPUUsage` | `storefs_cpu_usage_percent > 80` | 5m | warning | CPU usage exceeds 80% for > 5 min |
| `StoreFSHighCPUUsageCritical` | `storefs_cpu_usage_percent > 90` | 3m | critical | CPU usage exceeds 90% for > 3 min |
| `StoreFSHighMemoryUsage` | `storefs_memory_usage_percent > 85` | 2m | warning | Memory usage exceeds 85% |
| `StoreFSHighMemoryUsageCritical` | `storefs_memory_usage_percent > 93` | 1m | critical | Memory usage exceeds 93% |
| `StoreFSGoroutineWarning` | `go_goroutines > 20000` | 2m | warning | Goroutine count exceeds 20000 |
| `StoreFSGoroutineCritical` | `go_goroutines > 50000` | 1m | critical | Goroutine count exceeds 50000 |

### 5.2 Alertmanager Configuration

The Alertmanager config is in `monitor/alertmanager.yml`. By default, all alerts are routed to a **no-op receiver** (alerts are swallowed silently). This means alerts are still visible in the Alertmanager UI (http://localhost:9093) but no notifications are sent.

#### Default Configuration

```yaml
route:
  group_by: ["alertname", "node"]
  group_wait: 10s
  group_interval: 5m
  repeat_interval: 4h
  receiver: "default-noop"    # No notifications by default
```

#### Enabling Slack Notifications

1. Create a Slack Webhook URL at https://api.slack.com/messaging/webhooks
2. Uncomment the Slack receiver in `alertmanager.yml`:

```yaml
receivers:
  - name: "slack-notifications"
    slack_configs:
      - api_url: "https://hooks.slack.com/services/YOUR/WEBHOOK/URL"
        channel: "#storefs-alerts"
        send_resolved: true
        title: '{{ template "slack.title" . }}'
        text: '{{ template "slack.text" . }}'
        footer: "StoreFS Alertmanager"
```

3. Add the route:

```yaml
route:
  receiver: "default-noop"
  routes:
    - receiver: "slack-notifications"
      continue: false
```

4. Restart Alertmanager: `docker-compose restart alertmanager`

#### Enabling Email Notifications

1. Update the `global.smtp_*` settings in `alertmanager.yml` with your SMTP server details
2. Uncomment the email receiver:

```yaml
receivers:
  - name: "email-notifications"
    email_configs:
      - to: "ops@example.com"
        send_resolved: true
        headers:
          subject: '{{ template "email.subject" . }}'
```

3. Add the route and restart Alertmanager.

#### Enabling Webhook Notifications

```yaml
receivers:
  - name: "webhook-notifications"
    webhook_configs:
      - url: "http://your-webhook-host:8080/alert"
        send_resolved: true
```

### 5.3 Notification Templates

Templates are defined in `monitor/alertmanager.tmpl` and cover three channels:

- **Slack**: Templates `slack.title` and `slack.text` format alerts with severity, description, and value.
- **Email**: Templates `email.subject` and `email.body.html` format an HTML email with a table of firing alerts.
- **Webhook**: Uses the default Alertmanager webhook JSON payload format.

### 5.4 Alert Grouping and Repeat

- Alerts are grouped by `alertname` and `node`
- **group_wait**: 10s (wait before sending the first notification for a new group)
- **group_interval**: 5m (minimum interval between notifications for the same group)
- **repeat_interval**: 4h (only repeat notifications every 4 hours if the alert is still firing)

### 5.5 Viewing Alerts

- **Prometheus UI**: http://localhost:9090/alerts — view all alert rules and their current state
- **Alertmanager UI**: http://localhost:9093 — view fired and silenced alerts

---

## Appendix: Common Operations

### Check If Metrics Are Working

```bash
# Directly query a StoreFS node's metrics endpoint
curl http://<node-ip>:<node-port>/metrics | head -20

# Query Prometheus for a specific metric
curl 'http://localhost:9090/api/v1/query?query=storefs_node_uptime_seconds'
```

### Add a New Node to Monitoring

1. Edit `prometheus.yml` and add the new node's address to `static_configs.targets`
2. Restart Prometheus: `docker-compose restart prometheus`