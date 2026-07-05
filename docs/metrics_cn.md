**[English](metrics.md)**

# StoreFS 监控指南

本文档介绍 StoreFS 的监控与告警系统，涵盖整体架构、部署方式、指标数据列表、Grafana 面板以及通知告警配置。

---

## 1. 整体架构

StoreFS 采用业界标准的 **Prometheus + Grafana + Alertmanager** 监控告警栈：

```
┌──────────────────┐     scrape(/metrics)     ┌──────────────┐
│  StoreFS 节点 1  │◄─────────────────────────┤              │
│  (端口 7946)     │                          │              │
├──────────────────┤                          │  Prometheus  │
│  StoreFS 节点 2  │◄─────────────────────────┤  (端口 9090) │
│  (端口 7947)     │                          │              │
├──────────────────┤                          │              │
│  StoreFS 节点 N  │◄─────────────────────────┤              │
│  (端口 7948)     │                          └──────┬───────┘
└──────────────────┘                                 │
                                                      │
                        ┌─────────────────────────────┼──────────────────────────┐
                        │                             │                          │
                        ▼                             ▼                          ▼
                 ┌───────────┐              ┌─────────────────┐       ┌────────────────────┐
                 │  Grafana  │              │  Alertmanager   │       │  告警规则           │
                 │ (端口 3000)│              │  (端口 9093)    │       │  (storefs_alerts.yml)│
                 │           │              │                 │       └────────────────────┘
                 │  仪表盘   │              │ Slack / Email   │
                 └───────────┘              │ Webhook / 静默  │
                                            └─────────────────┘
```

### 工作流程

1. **指标采集**：每个 StoreFS 节点在管理端口（与集群 gossip 使用相同端口，通过 `node.port` 配置）上暴露 `/metrics` 端点。处理器以 Prometheus 纯文本格式返回指标（Content-Type: `text/plain; version=0.0.4`）。

2. **指标拉取**：Prometheus 按照 `scrape_interval`（默认 15s）周期性地从所有 StoreFS 节点的 `/metrics` 端点拉取指标。

3. **告警评估**：Prometheus 根据 `alerts/storefs_alerts.yml` 中定义的规则对拉取的指标进行评估。当规则条件持续满足指定时长（`for`）后，告警触发并转发给 Alertmanager。

4. **告警通知**：Alertmanager 接收触发的告警，根据路由规则进行分组，并通过配置的接收器（Slack、Email、Webhook 或默认空操作）发送通知。

5. **可视化**：Grafana 从 Prometheus 读取指标数据，通过预配置的仪表盘展示节点级和集群级的指标。

### 关键组件

| 组件 | 镜像 | 端口 | 用途 |
|-----------|-------|------|---------|
| Prometheus | prom/prometheus:v3.5.4 | 9090 | 指标存储和告警规则评估 |
| Grafana | grafana/grafana:13.1.0 | 3000 | 指标可视化和仪表盘 |
| Alertmanager | prom/alertmanager:v0.28.1 | 9093 | 告警通知路由和投递 |

### 指标端点

指标端点位于 `http://<节点IP>:<节点端口>/metrics`，**不需要**身份认证，以便 Prometheus 直接拉取。

每个指标携带以下通用标签：
- `node`：StoreFS 节点名称（来自配置 `cluster.node.name`）
- `ip`：StoreFS 节点 IP 地址（来自配置 `cluster.node.ip`）

---

## 2. 部署

### 前提条件

- 监控主机上已安装 Docker 和 Docker Compose
- StoreFS 集群已运行且可从监控主机访问

### 目录结构

```
monitor/
├── docker-compose.yml           # Docker Compose 文件
├── prometheus.yml               # Prometheus 配置文件
├── alertmanager.yml             # Alertmanager 配置文件
├── alertmanager.tmpl            # 通知模板
├── alerts/
│   └── storefs_alerts.yml       # 告警规则定义
└── grafana/
    └── provisioning/
        ├── dashboards/
        │   ├── dashboard.yml                    # Grafana 仪表盘自动配置
        │   ├── storefs.json                     # 节点指标仪表盘
        │   └── storefs-all-nodes-summary.json   # 全节点汇总仪表盘
        └── datasources/
            └── datasource.yml     # Prometheus 数据源配置
```

### 逐步部署

#### 1. 配置 Prometheus 拉取目标

编辑 `monitor/prometheus.yml`，设置正确的 StoreFS 节点地址：

```yaml
scrape_configs:
  - job_name: "storefs-metrics"
    static_configs:
      - targets:
          - "<主机1>:7946"   # 节点 1
          - "<主机2>:7947"   # 节点 2
          - "<主机3>:7948"   # 节点 3
        labels:
          app: "storefs"
```

> **注意**：当 StoreFS 运行在宿主机而监控运行在 Docker 中时，使用 `host.docker.internal` 替代 IP 地址实现跨容器通信（Prometheus 服务已预配置 `extra_hosts: ["host.docker.internal:host-gateway"]`）。

#### 2. 配置 Grafana 凭据（可选）

编辑 `monitor/docker-compose.yml` 更改默认 Grafana 管理员凭据：

```yaml
environment:
  - GF_SECURITY_ADMIN_USER=admin
  - GF_SECURITY_ADMIN_PASSWORD=admin123
```

#### 3. 启动监控栈

```bash
cd monitor
docker-compose up -d
```

这将启动 Prometheus、Grafana 和 Alertmanager。

#### 4. 验证部署

- **Prometheus**：打开 http://localhost:9090，进入 Status > Targets 确认所有 StoreFS 节点状态为 UP。
- **Grafana**：打开 http://localhost:3000，使用配置的凭据登录，导航到 Dashboards 即可找到预配置的 StoreFS 仪表盘。
- **Alertmanager**：打开 http://localhost:9093 查看已触发的告警。

#### 5. 停止监控栈

```bash
cd monitor
docker-compose down
```

如需同时删除持久数据卷：

```bash
docker-compose down -v
```

---

## 3. 指标数据列表

StoreFS 通过 `/metrics` 端点暴露以下类别的指标。除 Go 运行时指标外，所有指标均使用 `storefs_` 前缀。每个指标携带通用标签 `node` 和 `ip`。

### 3.1 CPU 指标

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_cpu_usage_percent` | Gauge | CPU 使用率（整体） |

### 3.2 运行时长指标

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_host_uptime_seconds` | Gauge | 主机运行时间（自系统启动起，秒） |
| `storefs_node_uptime_seconds` | Counter | 节点运行时间（自 StoreFS 进程启动起，秒） |

### 3.3 内存指标

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_memory_total_bytes` | Gauge | 总物理内存（字节） |
| `storefs_memory_used_bytes` | Gauge | 已用物理内存（字节） |
| `storefs_memory_usage_percent` | Gauge | 内存使用率 |

### 3.4 磁盘空间指标（按配置的磁盘路径）

标签：`path`（配置的磁盘路径）

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_disk_total_bytes` | Gauge | 磁盘总容量（字节） |
| `storefs_disk_used_bytes` | Gauge | 已用磁盘空间（字节） |
| `storefs_disk_usage_percent` | Gauge | 磁盘使用率 |

### 3.5 磁盘 I/O 指标（按设备）

标签：`device`（例如 `nvme0`、`sda1`）

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_disk_reads_completed_total` | Counter | 成功完成的读取次数 |
| `storefs_disk_writes_completed_total` | Counter | 成功完成的写入次数 |
| `storefs_disk_read_bytes_total` | Counter | 从磁盘读取的字节数 |
| `storefs_disk_write_bytes_total` | Counter | 写入磁盘的字节数 |
| `storefs_disk_read_time_ms_total` | Counter | 读取耗时（毫秒） |
| `storefs_disk_write_time_ms_total` | Counter | 写入耗时（毫秒） |

### 3.6 网络 I/O 指标（按接口）

标签：`interface`（例如 `eth0`、`lo`）

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_network_receive_bytes_total` | Counter | 接收的字节数 |
| `storefs_network_transmit_bytes_total` | Counter | 发送的字节数 |
| `storefs_network_receive_packets_total` | Counter | 接收的数据包数 |
| `storefs_network_transmit_packets_total` | Counter | 发送的数据包数 |

### 3.7 对象操作指标（累积计数器）

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_download_object_start_total` | Counter | 下载（ReadObject）请求启动总数 |
| `storefs_download_object_complete_total` | Counter | 下载（ReadObject）请求完成总数 |
| `storefs_upload_object_start_total` | Counter | 上传（WriteObject）请求启动总数 |
| `storefs_upload_object_complete_total` | Counter | 上传（WriteObject）请求完成总数 |

### 3.8 分片操作指标（累积计数器）

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_write_fragment_start_total` | Counter | 分片写入操作启动总数 |
| `storefs_write_fragment_complete_total` | Counter | 分片写入操作完成总数 |
| `storefs_read_fragment_start_total` | Counter | 分片读取操作启动总数 |
| `storefs_read_fragment_complete_total` | Counter | 分片读取操作完成总数 |

### 3.9 分块上传指标（累积计数器）

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_create_multipart_start_total` | Counter | CreateMultipartUpload 请求启动总数 |
| `storefs_create_multipart_complete_total` | Counter | CreateMultipartUpload 请求完成总数 |
| `storefs_upload_part_start_total` | Counter | UploadPart 请求启动总数 |
| `storefs_upload_part_complete_total` | Counter | UploadPart 请求完成总数 |
| `storefs_complete_parts_start_total` | Counter | CompleteParts 请求启动总数 |
| `storefs_complete_parts_complete_total` | Counter | CompleteParts 请求完成总数 |
| `storefs_abort_multipart_start_total` | Counter | AbortMultipartUpload 请求启动总数 |
| `storefs_abort_multipart_complete_total` | Counter | AbortMultipartUpload 请求完成总数 |

### 3.10 Go 运行时指标

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `go_goroutines` | Gauge | 当前存在的 goroutine 数量 |
| `go_memstats_gc_cpu_fraction` | Gauge | 自程序启动以来 GC 使用的 CPU 时间比例 |
| `go_memory_gc_cycles_count` | Counter | 已完成的 GC 周期数 |
| `go_memory_gc_pause_duration` | Gauge | GC 暂停总时长（秒） |
| `go_memory_used` | Gauge | 已分配且仍在使用的堆内存字节数 |

### 3.11 热点桶指标（按节点，近 2 分钟）

这些指标由每个节点本地计算，使用滑动窗口（2 分钟窗口，5 秒槽粒度）。导出 Top 100 的桶。

标签：`bucket`

| 指标名称 | 类型 | 描述 |
|-------------|------|-------------|
| `storefs_upload_hot_bucket_2m` | Gauge | 近 2 分钟各桶上传次数（Top 100） |
| `storefs_download_hot_bucket_2m` | Gauge | 近 2 分钟各桶下载次数（Top 100） |
| `storefs_upload_part_hot_bucket_2m` | Gauge | 近 2 分钟各桶 UploadPart 次数（Top 100） |
| `storefs_multipart_complete_hot_bucket_2m` | Gauge | 近 2 分钟各桶 CompleteMultipartUpload 次数（Top 100） |

---

## 4. Grafana 仪表盘

StoreFS 提供两个预配置的 Grafana 仪表盘。当 Grafana 容器启动时，它们会自动加载（通过 `grafana/provisioning/` 目录配置）。

### 4.1 仪表盘：StoreFS / Node Metrics（节点指标）

**用途**：单个 StoreFS 节点的详细视图。使用"Node"下拉菜单选择要查看的节点。

**刷新频率**：每 30s | **默认时间范围**：最近 1 小时 | **时区**：浏览器时区

**模板变量**：
- `$node`（下拉菜单）：选择节点，自动从 `label_values(storefs_cpu_usage_percent, node)` 获取选项
- `$path`、`$device`、`$interface`：根据所选节点自动填充（UI 中隐藏）

#### 概览行（默认展开）

| 面板 | 类型 | 查询语句 | 描述 |
|-------|------|-------|-------------|
| CPU Usage | Gauge | `storefs_cpu_usage_percent{node="$node"}` | CPU 使用率（阈值：绿<70，橙<90，红≥90） |
| Memory Usage | Gauge | `storefs_memory_usage_percent{node="$node"}` | 内存使用率（阈值：绿<70，橙<90，红≥90） |
| Disk Usage | Bar Gauge | `storefs_disk_usage_percent{node="$node"}` | 各路径磁盘使用率（阈值：绿<80，橙<95，红≥95） |
| Memory Total | Stat | `storefs_memory_total_bytes{node="$node"}` | 总物理内存（字节） |
| Memory Used | Stat | `storefs_memory_used_bytes{node="$node"}` | 已用物理内存（字节） |
| Disk Capacity | Stat | `storefs_disk_total_bytes{node="$node"}` | 磁盘总容量（字节） |
| Host Uptime | Stat | `storefs_host_uptime_seconds{node="$node"}` | 系统启动时长（持续时长格式） |
| Node Uptime | Stat | `storefs_node_uptime_seconds{node="$node"}` | StoreFS 进程运行时长（持续时长格式） |
| Upload Object Count | Stat | `storefs_upload_object_start_total{node="$node"}` | 累积上传请求数 |
| Download Object Count | Stat | `storefs_download_object_start_total{node="$node"}` | 累积下载请求数 |
| Multipart Create Count | Stat | `storefs_create_multipart_start_total{node="$node"}` | 累积分块创建请求数 |
| Multipart Complete Count | Stat | `storefs_complete_parts_start_total{node="$node"}` | 累积分块完成请求数 |

<!-- TODO: 插入概览行截图 -->

#### 对象任务行

| 面板 | 类型 | 查询语句 | 描述 |
|-------|------|-------|-------------|
| Upload Object Request Count | Stat | `increase(storefs_upload_object_start_total{node="$node"}[$__rate_interval])` | 上传请求速率 |
| Upload Object Complete Count | Stat | `increase(storefs_upload_object_complete_total{node="$node"}[$__rate_interval])` | 上传完成速率 |
| Upload Object Processing Count | Stat | `storefs_upload_object_start_total{node="$node"} - storefs_upload_object_complete_total{node="$node"}` | 当前处理中的上传数 |
| Download Object Request Count | Stat | `increase(storefs_download_object_start_total{node="$node"}[$__rate_interval])` | 下载请求速率 |
| Download Object Complete Count | Stat | `increase(storefs_download_object_complete_total{node="$node"}[$__rate_interval])` | 下载完成速率 |
| Download Object Processing Count | Stat | `storefs_download_object_start_total{node="$node"} - storefs_download_object_complete_total{node="$node"}` | 当前处理中的下载数 |
| Upload Object | Time Series | `increase(...start_total[$__rate_interval])`、`increase(...complete_total[$__rate_interval])`、`start_total - complete_total` | 上传趋势 |
| Download Object | Time Series | `increase(...start_total[$__rate_interval])`、`increase(...complete_total[$__rate_interval])`、`start_total - complete_total` | 下载趋势 |

<!-- TODO: 插入对象任务行截图 -->

#### 分块任务行

本行仅包含时间序列面板（无 Stat 计数器面板）。

| 面板 | 类型 | 描述 |
|-------|------|-------------|
| Create Multipart | Time Series | CreateMultipart 请求速率、完成速率、处理中的数量 |
| Upload Part | Time Series | UploadPart 请求速率、完成速率、处理中的数量 |
| Complete Multipart | Time Series | CompleteParts 请求速率、完成速率、处理中的数量 |
| Abort Multipart | Time Series | AbortMultipart 请求速率、完成速率、处理中的数量 |

每个时间序列面板显示 3 条线：
- `increase(storefs_<操作>_start_total{node="$node"}[$__rate_interval])`
- `increase(storefs_<操作>_complete_total{node="$node"}[$__rate_interval])`
- `storefs_<操作>_start_total{node="$node"} - storefs_<操作>_complete_total{node="$node"}`（处理中）

<!-- TODO: 插入分块任务行截图 -->

#### 分片任务行

| 面板 | 类型 | 描述 |
|-------|------|-------------|
| Write Fragment Request Count | Stat | 分片写入请求速率 |
| Write Fragment Complete Count | Stat | 分片写入完成速率 |
| Write Fragment Processing Count | Stat | 当前处理中的分片写入数 |
| Read Fragment Request Count | Stat | 分片读取请求速率 |
| Read Fragment Complete Count | Stat | 分片读取完成速率 |
| Read Fragment Processing Count | Stat | 当前处理中的分片读取数 |
| Write Fragment | Time Series | 分片写入请求、完成、处理中趋势 |
| Read Fragment | Time Series | 分片读取请求、完成、处理中趋势 |

<!-- TODO: 插入分片任务行截图 -->

#### 桶任务行

| 面板 | 类型 | 查询语句 | 描述 |
|-------|------|-------|-------------|
| Top20 Hot Buckets (Object Upload) | Table | `topk(20, storefs_upload_hot_bucket_2m{node="$node"})` | 近 2 分钟上传热点桶 Top 20 |
| Top20 Hot Buckets (Object Download) | Table | `topk(20, storefs_download_hot_bucket_2m{node="$node"})` | 近 2 分钟下载热点桶 Top 20 |
| Top20 Hot Buckets (Upload Part) | Table | `topk(20, storefs_upload_part_hot_bucket_2m{node="$node"})` | 近 2 分钟 UploadPart 热点桶 Top 20 |
| Top20 Hot Buckets (Multipart Complete) | Table | `topk(20, storefs_multipart_complete_hot_bucket_2m{node="$node"})` | 近 2 分钟 CompleteMultipart 热点桶 Top 20 |

<!-- TODO: 插入桶任务行截图 -->

#### GC 行（默认折叠）

Stat 面板（即时值）：

| 面板 | 查询语句 | 单位 |
|-------|-------|------|
| GC Cycles | `go_memory_gc_cycles_count{node="$node"}` | short（次数） |
| GC Pause Duration | `go_memory_gc_pause_duration{node="$node"}` | 秒 |
| GC CPU Fraction | `go_memstats_gc_cpu_fraction{node="$node"}` | percentunit（0-1） |
| Goroutines | `go_goroutines{node="$node"}` | short（数量） |
| Go Memory Used | `go_memory_used{node="$node"}` | 字节 |

时间序列面板（历史趋势）：

| 面板 | 查询语句 | 单位 |
|-------|-------|------|
| GC Cycles Rate | `rate(go_memory_gc_cycles_count{node="$node"}[1m])` | ops（每秒次数） |
| GC Pause Duration | `go_memory_gc_pause_duration{node="$node"}` | 秒 |
| GC CPU Fraction | `go_memstats_gc_cpu_fraction{node="$node"}` | percentunit |
| Goroutines | `go_goroutines{node="$node"}` | short（数量） |
| Go Memory Used | `go_memory_used{node="$node"}` | 字节 |

<!-- TODO: 插入 GC 行（展开后）截图 -->

#### CPU 和内存行

| 面板 | 类型 | 查询语句 | 单位 |
|-------|------|-------|------|
| CPU Usage | Time Series | `storefs_cpu_usage_percent{node="$node"}` | 百分比 |
| Memory | Time Series | `storefs_memory_total_bytes{node="$node"}`（总计）、`storefs_memory_used_bytes{node="$node"}`（已用） | 字节 |

<!-- TODO: 插入 CPU & Memory 行截图 -->

#### 磁盘行

| 面板 | 类型 | 查询语句 | 单位 |
|-------|------|---------|------|
| Disk Usage % | Time Series | `storefs_disk_usage_percent{node="$node"}` 图例=`{{path}}` | 百分比 |
| Disk Throughput | Time Series | `rate(storefs_disk_read_bytes_total{node="$node"}[$__rate_interval])` 读取、`rate(storefs_disk_write_bytes_total{node="$node"}[$__rate_interval])` 写入 | Bps |
| Disk IOPS | Time Series | `rate(storefs_disk_reads_completed_total{node="$node"}[$__rate_interval])` 读取、`rate(storefs_disk_writes_completed_total{node="$node"}[$__rate_interval])` 写入 | iops |

<!-- TODO: 插入磁盘行截图 -->

#### 网络行

| 面板 | 类型 | 查询语句 | 单位 |
|-------|------|---------|------|
| Network Traffic (bits) | Time Series | `rate(storefs_network_receive_bytes_total{node="$node"}[$__rate_interval]) * 8` 接收、`rate(storefs_network_transmit_bytes_total{node="$node"}[$__rate_interval]) * 8` 发送 | bps |
| Network Packets | Time Series | `rate(storefs_network_receive_packets_total{node="$node"}[$__rate_interval])` 接收、`rate(storefs_network_transmit_packets_total{node="$node"}[$__rate_interval])` 发送 | pps |

<!-- TODO: 插入网络行截图 -->

### 4.2 仪表盘：StoreFS / All Nodes Summary Metrics（全节点汇总）

**用途**：跨所有节点的集群级汇总视图。显示聚合的操作计数和资源使用情况。

**默认时间范围**：最近 30 分钟

#### 集群概览行

| 面板 | 类型 | 查询语句 | 描述 |
|-------|------|-------|-------------|
| Node Uptime | Stat | `storefs_node_uptime_seconds{}` 图例=`{{node}}` | 每个节点的运行时长（持续时长格式） |

<!-- TODO: 插入截图 -->

#### 对象任务行（集群级）

Stat 面板（6 个）：

| 面板 | 查询语句 | 描述 |
|-------|-------|-------------|
| Total Upload Request | `sum(increase(storefs_upload_object_start_total{}[$__rate_interval]))` | 上传请求速率汇总 |
| Total Upload Complete | `sum(increase(storefs_upload_object_complete_total{}[$__rate_interval]))` | 上传完成速率汇总 |
| Total Upload Processing | `sum(storefs_upload_object_start_total{}) - sum(storefs_upload_object_complete_total{})` | 当前处理中的上传数汇总 |
| Total Download Request | `sum(increase(storefs_download_object_start_total{}[$__rate_interval]))` | 下载请求速率汇总 |
| Total Download Complete | `sum(increase(storefs_download_object_complete_total{}[$__rate_interval]))` | 下载完成速率汇总 |
| Total Download Processing | `sum(storefs_download_object_start_total{}) - sum(storefs_download_object_complete_total{})` | 当前处理中的下载数汇总 |

时间序列面板（1 个）：

| 面板 | 查询语句 | 描述 |
|-------|-------|-------------|
| Total Object Operations | `sum(increase(...start_total{}[$__rate_interval]))` 上传/下载、`sum(...start_total{} - ...complete_total{})` 处理中 | 跨所有节点的上传、下载和处理中总数 |

<!-- TODO: 插入截图 -->

#### 分块任务行（集群级）

Stat 面板（12 个）：

| 面板 | 查询语句 |
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

时间序列面板（1 个）：

| 面板 | 查询语句 | 描述 |
|-------|-------|-------------|
| Total Multipart Operations | `sum(increase(...start_total{}[$__rate_interval]))`（CreateMultipart、UploadPart、CompleteParts、AbortMultipart）+ `sum(...start_total{} - ...complete_total{})`（处理中） | 跨所有节点的分块操作趋势 |

<!-- TODO: 插入截图 -->

#### 分片任务行（集群级）

Stat 面板（6 个）：

| 面板 | 查询语句 |
|-------|-------|
| Total Fragment Write Request | `sum(increase(storefs_write_fragment_start_total{}[$__rate_interval]))` |
| Total Fragment Write Complete | `sum(increase(storefs_write_fragment_complete_total{}[$__rate_interval]))` |
| Total Fragment Write Processing | `sum(storefs_write_fragment_start_total{}) - sum(storefs_write_fragment_complete_total{})` |
| Total Fragment Read Request | `sum(increase(storefs_read_fragment_start_total{}[$__rate_interval]))` |
| Total Fragment Read Complete | `sum(increase(storefs_read_fragment_complete_total{}[$__rate_interval]))` |
| Total Fragment Read Processing | `sum(storefs_read_fragment_start_total{}) - sum(storefs_read_fragment_complete_total{})` |

时间序列面板（1 个）：

| 面板 | 查询语句 | 描述 |
|-------|-------|-------------|
| Total Fragment Operations | `sum(increase(...start_total{}[$__rate_interval]))`（写入/读取）+ `sum(...start_total{} - ...complete_total{})`（处理中） | 跨所有节点的分片操作趋势 |

<!-- TODO: 插入截图 -->

#### 桶任务行（所有节点）

| 面板 | 类型 | 查询语句 |
|-------|------|-------|
| Top 10 Hot Buckets (Upload, All Nodes) | Table | `topk(10, sum by (bucket) (storefs_upload_hot_bucket_2m))` |
| Top 10 Hot Buckets (Download, All Nodes) | Table | `topk(10, sum by (bucket) (storefs_download_hot_bucket_2m))` |
| Top 10 Hot Buckets (Upload Part, All Nodes) | Table | `topk(10, sum by (bucket) (storefs_upload_part_hot_bucket_2m))` |
| Top 10 Hot Buckets (Multipart Complete, All Nodes) | Table | `topk(10, sum by (bucket) (storefs_multipart_complete_hot_bucket_2m))` |

<!-- TODO: 插入截图 -->

#### 集群资源汇总行

| 面板 | 类型 | 查询语句 | 单位 |
|-------|------|-------|------|
| Free Disk by Node | Time Series | `100 - (avg by (node) (storefs_disk_usage_percent{}))` 图例=`{{node}} free` | 百分比 |
| CPU by Node | Time Series | `avg by (node) (storefs_cpu_usage_percent{})` 图例=`{{node}}` | 百分比 |
| Memory by Node | Time Series | `avg by (node) (storefs_memory_usage_percent{})` 图例=`{{node}}` | 百分比 |

<!-- TODO: 插入截图 -->

---

## 5. 通知告警配置

### 5.1 告警规则

告警规则定义在 `monitor/alerts/storefs_alerts.yml`，由 Prometheus 每 15s 评估一次。

| 告警名称 | 条件 | 持续时间 | 严重级别 | 描述 |
|------------|-----------|-----|----------|-------------|
| `StoreFSNodeDown` | `up{job="storefs-metrics"} == 0` | 1分钟 | critical | Prometheus 无法访问节点 |
| `StoreFSNodeRestarted` | `storefs_node_uptime_seconds < 60` | 1分钟 | critical | 节点进程刚重启（< 60秒前） |
| `StoreFSDiskUsageWarning` | `storefs_disk_usage_percent > 75` | 2分钟 | warning | 磁盘使用率超过 75% |
| `StoreFSDiskUsageCritical` | `storefs_disk_usage_percent > 90` | 1分钟 | critical | 磁盘使用率超过 90% |
| `StoreFSHighCPUUsage` | `storefs_cpu_usage_percent > 80` | 5分钟 | warning | CPU 使用率超过 80% 持续 > 5分钟 |
| `StoreFSHighCPUUsageCritical` | `storefs_cpu_usage_percent > 90` | 3分钟 | critical | CPU 使用率超过 90% 持续 > 3分钟 |
| `StoreFSHighMemoryUsage` | `storefs_memory_usage_percent > 85` | 2分钟 | warning | 内存使用率超过 85% |
| `StoreFSHighMemoryUsageCritical` | `storefs_memory_usage_percent > 93` | 1分钟 | critical | 内存使用率超过 93% |
| `StoreFSGoroutineWarning` | `go_goroutines > 20000` | 2分钟 | warning | goroutine 数量超过 20000 |
| `StoreFSGoroutineCritical` | `go_goroutines > 50000` | 1分钟 | critical | goroutine 数量超过 50000 |

### 5.2 Alertmanager 配置

Alertmanager 配置位于 `monitor/alertmanager.yml`。默认情况下，所有告警被路由到 **空操作接收器**（告警被静默丢弃）。这意味着告警仍然可以在 Alertmanager UI（http://localhost:9093）中查看，但不会发送任何通知。

#### 默认配置

```yaml
route:
  group_by: ["alertname", "node"]
  group_wait: 10s
  group_interval: 5m
  repeat_interval: 4h
  receiver: "default-noop"    # 默认不发送通知
```

#### 启用 Slack 通知

1. 在 https://api.slack.com/messaging/webhooks 创建 Slack Webhook URL
2. 取消 `alertmanager.yml` 中 Slack 接收器的注释：

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

3. 添加路由规则：

```yaml
route:
  receiver: "default-noop"
  routes:
    - receiver: "slack-notifications"
      continue: false
```

4. 重启 Alertmanager：`docker-compose restart alertmanager`

#### 启用邮件通知

1. 更新 `alertmanager.yml` 中的 `global.smtp_*` 设置，填入你的 SMTP 服务器信息
2. 取消注释邮件接收器：

```yaml
receivers:
  - name: "email-notifications"
    email_configs:
      - to: "ops@example.com"
        send_resolved: true
        headers:
          subject: '{{ template "email.subject" . }}'
```

3. 添加路由规则并重启 Alertmanager。

#### 启用 Webhook 通知

```yaml
receivers:
  - name: "webhook-notifications"
    webhook_configs:
      - url: "http://your-webhook-host:8080/alert"
        send_resolved: true
```

### 5.3 通知模板

模板定义在 `monitor/alertmanager.tmpl`，覆盖三种通知渠道：

- **Slack**：模板 `slack.title` 和 `slack.text` 将告警格式化为带有严重级别、描述和值的 Slack 消息。
- **邮件**：模板 `email.subject` 和 `email.body.html` 将告警格式化为 HTML 邮件，包含触发的告警表格。
- **Webhook**：使用默认的 Alertmanager webhook JSON 负载格式。

### 5.4 告警分组和重复

- 告警按 `alertname` 和 `node` 分组
- **group_wait**：10秒（新组首次通知前的等待时间）
- **group_interval**：5分钟（同一组通知之间的最小间隔）
- **repeat_interval**：4小时（如果告警仍在触发，每 4小时 重复通知一次）

### 5.5 查看告警

- **Prometheus UI**：http://localhost:9090/alerts — 查看所有告警规则及其当前状态
- **Alertmanager UI**：http://localhost:9093 — 查看已触发和已静音的告警

---

## 附录：常见操作

### 检查指标是否正常工作

```bash
# 直接查询 StoreFS 节点的指标端点
curl http://<节点IP>:<节点端口>/metrics | head -20

# 查询 Prometheus 特定指标
curl 'http://localhost:9090/api/v1/query?query=storefs_node_uptime_seconds'
```

### 新增节点到监控

1. 编辑 `prometheus.yml`，在 `static_configs.targets` 中添加新节点地址
2. 重启 Prometheus：`docker-compose restart prometheus`

### 自定义告警阈值

编辑 `monitor/alerts/storefs_alerts.yml`，修改规则中的阈值和持续时间，然后重启 Prometheus：

```bash
docker-compose restart prometheus
```