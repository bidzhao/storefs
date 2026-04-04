**[English](troubleshooting.md)**

# 故障排除文档

## 系统启动问题

### 1. 数据库连接失败

#### 症状
- 启动时提示 "database connection failed"
- 无法连接到 MySQL 或 StarRocks

#### 可能的原因和解决方案

1. **数据库服务未启动**
   - 检查 MySQL 服务状态：`systemctl status mysql`
   - 检查 StarRocks 服务状态：`./fe/bin/start_fe.sh --status`
   - 确保数据库服务正在运行

2. **数据库配置错误**
   - 检查 `config.yaml` 中的数据库配置：
     ```yaml
     cluster:
       db:
         host: "127.0.0.1"
         port: 9030
         user: "root"
         password: ""
         database: "mydb"
         timeout: 10s
     ```
   - 确保配置与数据库实际设置一致

3. **防火墙或网络问题**
   - 检查防火墙规则，确保允许访问数据库端口
   - 测试网络连接：`telnet <db-host> <db-port>`

### 2. 节点无法加入集群

#### 症状
- 节点启动后无法与其他节点通信
- 日志中显示 "failed to join cluster"

#### 可能的原因和解决方案

1. **集群配置错误**
   - 检查 `config.yaml` 中的集群名称：
     ```yaml
     cluster:
       name: mycluster
     ```
   - 确保所有节点的集群名称一致

2. **种子节点配置错误**
   - 检查 `config.yaml` 中的种子节点配置：
     ```yaml
     cluster:
       seeds:
         - 127.0.0.1:7946
         - 127.0.0.1:7947
     ```
   - 确保至少有一个种子节点正在运行且可访问

3. **端口或防火墙问题**
   - 检查节点通信端口是否被占用：`lsof -i :7946`
   - 检查防火墙规则，确保允许节点间通信
   - 测试网络连接：`telnet <seed-ip> <seed-port>`

### 3. S3 API 无法访问

#### 症状
- 无法通过 S3 客户端连接到 StoreFS
- API 请求返回 "connection refused" 或 "timeout"

#### 可能的原因和解决方案

1. **S3 API 服务未启动**
   - 检查节点是否正在运行：`ps aux | grep storefs`
   - 检查日志中是否有 S3 服务启动错误

2. **配置错误**
   - 检查 `config.yaml` 中的 S3 配置：
     ```yaml
     node:
       s3:
         host: 127.0.0.1
         port: 8901
     ```
   - 确保主机和端口配置正确

3. **网络或防火墙问题**
   - 检查防火墙规则，确保允许访问 S3 API 端口
   - 测试网络连接：`telnet <node-ip> <s3-port>`

## 数据操作问题

### 1. 无法上传对象

#### 症状
- 上传对象时失败
- 错误信息可能包括 "internal error"、"timeout" 或 "permission denied"

#### 可能的原因和解决方案

1. **权限问题**
   - 检查用户的访问密钥是否有效
   - 检查用户是否有上传对象的权限
   - 检查桶的策略设置

2. **磁盘空间不足**
   - 检查节点磁盘空间：`df -h`
   - 确保有足够的空间存储对象

3. **磁盘路径配置错误**
   - 检查 `config.yaml` 中的磁盘配置：
     ```yaml
     node:
       disks:
         - path: /data/disk1
           weight: 1
     ```
   - 确保磁盘路径存在且有写入权限

### 2. 无法下载对象

#### 症状
- 下载对象时失败
- 错误信息可能包括 "not found"、"internal error" 或 "timeout"

#### 可能的原因和解决方案

1. **对象不存在**
   - 确认对象是否已成功上传
   - 检查对象名称是否正确（注意大小写敏感）

2. **权限问题**
   - 检查用户是否有读取对象的权限
   - 检查桶的策略设置

3. **磁盘或网络问题**
   - 检查节点磁盘是否正常
   - 检查网络连接是否稳定

### 3. 数据一致性问题

#### 症状
- 上传的对象无法立即访问
- 对象内容与预期不符

#### 可能的原因和解决方案

1. **同步延迟**
   - StoreFS 使用异步复制，可能需要时间来同步数据
   - 等待几秒钟后重试

2. **副本策略配置错误**
   - 检查策略配置：
     ```yaml
     # 在策略管理中创建的策略
     name: "my-policy"
     type: "replicas"
     replicas: 2
     ```
   - 确保副本数量合理

3. **网络分区**
   - 检查节点间网络连接
   - 确保所有节点都能正常通信

## 性能问题

### 1. 高延迟

#### 症状
- 请求响应时间过长
- 吞吐量下降

#### 可能的原因和解决方案

1. **磁盘性能问题**
   - 检查磁盘 I/O 利用率：`iostat -x`
   - 考虑使用 SSD 或更快的存储设备

2. **网络拥堵**
   - 检查网络流量：`iftop` 或 `nload`
   - 优化网络配置或升级带宽

3. **数据库压力**
   - 检查数据库性能指标
   - 优化查询或添加索引

### 2. 高内存使用率

#### 症状
- 系统内存不足
- 进程被系统强制终止

#### 可能的原因和解决方案

1. **数据缓存过大**
   - 检查节点缓存配置
   - 限制进程内存使用

2. **并发连接过多**
   - 检查连接数：`netstat -an | grep ESTABLISHED | wc -l`
   - 优化连接池配置

3. **内存泄漏**
   - 监控内存使用趋势
   - 使用内存分析工具（如 pprof）定位问题

### 3. 高 CPU 使用率

#### 症状
- CPU 利用率持续高位
- 系统响应变慢

#### 可能的原因和解决方案

1. **计算密集型操作**
   - 检查系统负载：`top`
   - 优化查询或处理逻辑

2. **进程数过多**
   - 检查运行中的进程：`ps aux`
   - 关闭不必要的服务

3. **软件缺陷**
   - 检查是否有已知的性能问题
   - 升级到最新版本

## 系统崩溃问题

### 1. 节点意外崩溃

#### 症状
- StoreFS 进程突然终止
- 系统日志中有崩溃信息

#### 可能的原因和解决方案

1. **内存溢出**
   - 检查系统日志中的 OOM（Out of Memory）错误
   - 优化内存使用或增加系统内存

2. **磁盘故障**
   - 检查磁盘错误日志：`dmesg | grep disk`
   - 替换有故障的磁盘

3. **软件缺陷**
   - 检查崩溃日志：`/var/log/messages` 或系统核心转储
   - 报告问题并等待修复版本

### 2. 数据库连接中断

#### 症状
- 节点无法与数据库通信
- 操作失败

#### 可能的原因和解决方案

1. **数据库服务崩溃**
   - 检查数据库服务状态
   - 重启数据库服务

2. **网络连接问题**
   - 检查网络连接
   - 修复网络故障

3. **防火墙配置变更**
   - 检查防火墙规则是否有变更
   - 确保允许数据库连接

## 日志分析

### 如何使用日志定位问题

#### 1. 启动时的错误

查找包含 "error" 或 "failed" 关键词的日志：

```bash
grep -i "error\|failed" /path/to/storefs.log
```

#### 2. 运行中的问题

实时监控日志：

```bash
tail -f /path/to/storefs.log
```

#### 3. 常见错误模式

- "database connection failed"：数据库连接问题
- "disk not accessible"：磁盘访问问题
- "node communication error"：节点通信问题
- "invalid signature"：认证失败

## 备份和恢复

### 1. 如何备份元数据

#### MySQL 备份

```bash
# 备份整个数据库
mysqldump -h <host> -P <port> -u <user> -p <database> > backup.sql

# 恢复数据库
mysql -h <host> -P <port> -u <user> -p <database> < backup.sql
```

#### StarRocks 备份

```bash
# 备份元数据
./fe/bin/backup_fe.sh

# 恢复元数据
./fe/bin/restore_fe.sh
```

### 2. 如何备份数据文件

#### 手动备份

```bash
# 备份磁盘数据
rsync -avz /path/to/disk/ /path/to/backup/
```

#### 使用 S3 工具备份

```bash
# 使用 AWS CLI 同步到外部存储
aws s3 sync s3://<bucket>/ s3://<backup-bucket>/ --endpoint-url http://<storefs-ip>:<s3-port>
```

## 联系支持

如果您遇到无法解决的问题，可以通过以下方式联系支持团队：

1. **GitHub Issues**：在项目仓库中提交问题
2. **邮件支持**：发送邮件到 davidz.bb@gmail.com
3. **社区论坛**：访问 StoreFS 社区论坛
4. **技术支持**：购买商业支持服务

在报告问题时，请注意提供以下信息：
- StoreFS 版本
- 系统架构（CPU、内存、磁盘）
- 错误日志
- 操作步骤
- 预期行为和实际行为

这将帮助我们更快地定位和解决问题。
