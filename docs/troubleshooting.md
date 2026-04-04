**[查看中文版](troubleshooting_cn.md)**

# Troubleshooting Documentation

## System Startup Issues

### 1. Database Connection Failed

#### Symptoms
- "database connection failed" message at startup
- Cannot connect to MySQL or StarRocks

#### Possible Causes and Solutions

1. **Database Service Not Started**
   - Check MySQL service status: `systemctl status mysql`
   - Check StarRocks service status: `./fe/bin/start_fe.sh --status`
   - Ensure the database service is running

2. **Database Configuration Error**
   - Check database configuration in `config.yaml`:
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
   - Ensure the configuration matches the actual database settings

3. **Firewall or Network Issues**
   - Check firewall rules to ensure database port access is allowed
   - Test network connection: `telnet <db-host> <db-port>`

### 2. Node Cannot Join Cluster

#### Symptoms
- Node fails to communicate with other nodes after startup
- "failed to join cluster" message in logs

#### Possible Causes and Solutions

1. **Cluster Configuration Error**
   - Check cluster name in `config.yaml`:
     ```yaml
     cluster:
       name: mycluster
     ```
   - Ensure all nodes have the same cluster name

2. **Seed Node Configuration Error**
   - Check seed node configuration in `config.yaml`:
     ```yaml
     cluster:
       seeds:
         - 127.0.0.1:7946
         - 127.0.0.1:7947
     ```
   - Ensure at least one seed node is running and accessible

3. **Port or Firewall Issues**
   - Check if the node communication port is occupied: `lsof -i :7946`
   - Check firewall rules to ensure inter-node communication is allowed
   - Test network connection: `telnet <seed-ip> <seed-port>`

### 3. S3 API Inaccessible

#### Symptoms
- Cannot connect to StoreFS through S3 client
- API requests return "connection refused" or "timeout"

#### Possible Causes and Solutions

1. **S3 API Service Not Started**
   - Check if the node is running: `ps aux | grep storefs`
   - Check logs for S3 service startup errors

2. **Configuration Error**
   - Check S3 configuration in `config.yaml`:
     ```yaml
     node:
       s3:
         host: 127.0.0.1
         port: 8901
     ```
   - Ensure host and port configurations are correct

3. **Network or Firewall Issues**
   - Check firewall rules to ensure S3 API port access is allowed
   - Test network connection: `telnet <node-ip> <s3-port>`

## Data Operation Issues

### 1. Cannot Upload Objects

#### Symptoms
- Object upload fails
- Error messages may include "internal error", "timeout", or "permission denied"

#### Possible Causes and Solutions

1. **Permission Issues**
   - Check if the user's access keys are valid
   - Check if the user has permission to upload objects
   - Check bucket policy settings

2. **Insufficient Disk Space**
   - Check node disk space: `df -h`
   - Ensure there is enough space to store the object

3. **Disk Path Configuration Error**
   - Check disk configuration in `config.yaml`:
     ```yaml
     node:
       disks:
         - path: /data/disk1
           weight: 1
     ```
   - Ensure the disk path exists and has write permissions

### 2. Cannot Download Objects

#### Symptoms
- Object download fails
- Error messages may include "not found", "internal error", or "timeout"

#### Possible Causes and Solutions

1. **Object Does Not Exist**
   - Verify if the object was successfully uploaded
   - Check if the object name is correct (note case sensitivity)

2. **Permission Issues**
   - Check if the user has permission to read the object
   - Check bucket policy settings

3. **Disk or Network Issues**
   - Check if the node disk is functioning properly
   - Check if the network connection is stable

### 3. Data Consistency Issues

#### Symptoms
- Uploaded objects cannot be accessed immediately
- Object content does not match expectations

#### Possible Causes and Solutions

1. **Synchronization Delay**
   - StoreFS uses asynchronous replication, which may take time to sync data
   - Wait a few seconds and retry

2. **Replica Policy Configuration Error**
   - Check policy configuration:
     ```yaml
     # Policy created in policy management
     name: "my-policy"
     type: "replicas"
     replicas: 2
     ```
   - Ensure the number of replicas is reasonable

3. **Network Partition**
   - Check inter-node network connections
   - Ensure all nodes can communicate normally

## Performance Issues

### 1. High Latency

#### Symptoms
- Excessive request response time
- Throughput degradation

#### Possible Causes and Solutions

1. **Disk Performance Issues**
   - Check disk I/O utilization: `iostat -x`
   - Consider using SSD or faster storage devices

2. **Network Congestion**
   - Check network traffic: `iftop` or `nload`
   - Optimize network configuration or upgrade bandwidth

3. **Database Pressure**
   - Check database performance metrics
   - Optimize queries or add indexes

### 2. High Memory Usage

#### Symptoms
- Insufficient system memory
- Processes are killed by the system

#### Possible Causes and Solutions

1. **Large Data Cache**
   - Check node cache configuration
   - Limit process memory usage

2. **Too Many Concurrent Connections**
   - Check number of connections: `netstat -an | grep ESTABLISHED | wc -l`
   - Optimize connection pool configuration

3. **Memory Leaks**
   - Monitor memory usage trends
   - Use memory analysis tools (such as pprof) to locate issues

### 3. High CPU Usage

#### Symptoms
- CPU utilization remains high
- System response slows down

#### Possible Causes and Solutions

1. **CPU-Intensive Operations**
   - Check system load: `top`
   - Optimize queries or processing logic

2. **Too Many Processes**
   - Check running processes: `ps aux`
   - Close unnecessary services

3. **Software Defects**
   - Check if there are known performance issues
   - Upgrade to the latest version

## System Crash Issues

### 1. Node Unexpectedly Crashes

#### Symptoms
- StoreFS process terminates suddenly
- Crash information in system logs

#### Possible Causes and Solutions

1. **Memory Overflow**
   - Check system logs for OOM (Out of Memory) errors
   - Optimize memory usage or increase system memory

2. **Disk Failure**
   - Check disk error logs: `dmesg | grep disk`
   - Replace faulty disks

3. **Software Defects**
   - Check crash logs: `/var/log/messages` or system core dumps
   - Report the issue and wait for a fixed version

### 2. Database Connection Interrupt

#### Symptoms
- Node cannot communicate with the database
- Operations fail

#### Possible Causes and Solutions

1. **Database Service Crash**
   - Check database service status
   - Restart the database service

2. **Network Connection Issues**
   - Check network connections
   - Fix network failures

3. **Firewall Configuration Changes**
   - Check if firewall rules have changed
   - Ensure database connections are allowed

## Log Analysis

### How to Use Logs to Locate Problems

#### 1. Startup Errors

Search for logs containing "error" or "failed" keywords:

```bash
grep -i "error\|failed" /path/to/storefs.log
```

#### 2. Runtime Problems

Monitor logs in real time:

```bash
tail -f /path/to/storefs.log
```

#### 3. Common Error Patterns

- "database connection failed": Database connection issues
- "disk not accessible": Disk access issues
- "node communication error": Node communication issues
- "invalid signature": Authentication failure

## Backup and Recovery

### 1. How to Backup Metadata

#### MySQL Backup

```bash
# Backup entire database
mysqldump -h <host> -P <port> -u <user> -p <database> > backup.sql

# Restore database
mysql -h <host> -P <port> -u <user> -p <database> < backup.sql
```

#### StarRocks Backup

```bash
# Backup metadata
./fe/bin/backup_fe.sh

# Restore metadata
./fe/bin/restore_fe.sh
```

### 2. How to Backup Data Files

#### Manual Backup

```bash
# Backup disk data
rsync -avz /path/to/disk/ /path/to/backup/
```

#### Backup Using S3 Tools

```bash
# Use AWS CLI to sync to external storage
aws s3 sync s3://<bucket>/ s3://<backup-bucket>/ --endpoint-url http://<storefs-ip>:<s3-port>
```

## Contact Support

If you encounter problems that cannot be resolved, you can contact the support team through the following methods:

1. **GitHub Issues**: Submit an issue in the project repository
2. **Email Support**: Send an email to davidz.bb@gmail.com
3. **Community Forum**: Visit the StoreFS community forum
4. **Technical Support**: Purchase commercial support services

When reporting an issue, please provide the following information:
- StoreFS version
- System architecture (CPU, memory, disk)
- Error logs
- Operation steps
- Expected behavior and actual behavior

This will help us locate and resolve the issue faster.
