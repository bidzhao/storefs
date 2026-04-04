**[查看中文版](faq_cn.md)**

# FAQ Documentation

## Frequently Asked Questions

### 1. Installation and Configuration

#### 1.1 How to choose the database type?

StoreFS currently supports the following databases:
- **MySQL**: Traditional relational database, suitable for small deployments
- **StarRocks**: High-performance analytical database, suitable for large-scale storage and complex queries

For production environments, StarRocks is recommended because it provides better query performance and scalability.

#### 1.2 How to configure multiple disks?

Add multiple disk configurations in the `config.yaml` file:

```yaml
node:
  disks:
    - path: /data/disk1
      weight: 1
    - path: /data/disk2
      weight: 2
```

A higher weight value means more data will be allocated to that disk.

#### 1.3 How to modify node listening ports?

Modify the following configurations in the `config.yaml` file:

```yaml
node:
  port: 7946          # Node communication, API and console port
  internal_port: 17946 # Internal file operation port
  s3:
    port: 8901        # S3 API port
```

### 2. Cluster Management

#### 2.1 How to add a new node to the cluster?

1. Install StoreFS on the new node
2. Configure `config.yaml`, ensuring:
   - `cluster.name` is consistent with the existing cluster
   - `node.name` and `node.num` are unique
   - `seeds` includes at least one existing node
3. Start the new node: `./storefs -config config.yaml`

#### 2.2 How to safely remove a node?

1. Ensure all data on the node has been replicated to other nodes
2. Stop the node process: `pkill storefs`
3. Remove the node address from the `seeds` configuration of other nodes

#### 2.3 How to check cluster health status?

Use the health check API:

```bash
curl http://<node-ip>:7946/api/health
```

A return of `"status": "healthy"` indicates the node is healthy.

### 3. Data Management

#### 3.1 How to back up data?

StoreFS supports the following backup methods:
1. **Manual backup**: Directly copy files on disk
2. **S3 interface**: Use AWS CLI or other S3 tools to sync data to external storage
3. **Database backup**: Back up metadata in StarRocks/MySQL

#### 3.2 How to restore data?

1. Restore metadata: Restore from database backup
2. Restore data files: Copy backed up files to the corresponding location
3. Start the node: `./storefs -config config.yaml`

#### 3.3 How to handle disk failures?

1. Replace the failed disk
2. Create the same directory structure on the new disk
3. Reconfigure the node: Update the disk path in `config.yaml`
4. Restart the node

### 4. Performance Optimization

#### 4.1 How to improve write performance?

1. Increase the number of nodes to distribute write pressure
2. Use faster storage devices (such as SSD)
3. Adjust disk weights to evenly distribute data
4. Optimize database configurations (such as StarRocks memory and concurrency settings)

#### 4.2 How to improve read performance?

1. Use replica policies to increase data redundancy
2. Store hot data on frequently queried nodes
3. Optimize disk layout to avoid disk contention
4. Use CDN or caching mechanisms

#### 4.3 How to optimize memory usage?

1. Adjust database cache size
2. Optimize query statements to reduce memory consumption
3. Limit the number of concurrent queries
4. Use servers with larger memory

### 5. Security

#### 5.1 How to protect the StoreFS cluster?

1. Use firewalls to restrict access to ports
2. Configure HTTPS communication
3. Regularly replace access keys
4. Restrict database access permissions

#### 5.2 How to audit operations?

StoreFS does not currently provide direct audit logs, but you can implement it through the following methods:
1. Enable database audit functionality
2. Use network packet capture tools (such as tcpdump)
3. Configure system log recording

#### 5.3 How to handle access key leaks?

1. Immediately delete the leaked access key
2. Create new access keys
3. Update affected application configurations
4. Audit the system to find abnormal activities

### 6. Other Questions

#### 6.1 Why can't uploaded files be downloaded?

Possible reasons:
1. The file was not completely uploaded
2. File metadata is corrupted
3. Network connection issues
4. Permission configuration errors

#### 6.2 How to view system logs?

StoreFS logs are output to standard output by default and can be viewed through the following methods:
1. Redirect to a file when starting: `./storefs -config config.yaml > storefs.log 2>&1`
2. Use system log services (such as systemd)
3. Use Docker log functionality: `docker logs storefs-node1`

### 7. Technical Support

If you encounter problems not solved in this FAQ, please:

1. Check the [Troubleshooting](troubleshooting.md) documentation
2. Check system logs
3. Report issues in GitHub Issues
4. Contact the technical support team
