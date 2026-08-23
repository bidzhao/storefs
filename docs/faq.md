**[查看中文版](faq_cn.md)**

# FAQ Documentation

## Frequently Asked Questions

### 1. Installation and Configuration

#### 1.1 How to choose the database type?

StoreFS currently supports the following databases:
- **MySQL**: Traditional relational database, suitable for small deployments
- **Apache Doris**: High-performance analytical database, suitable for large-scale storage and complex queries

For production environments, Apache Doris is recommended because it provides better query performance and scalability.

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
3. **Database backup**: Back up metadata in Apache Doris/MySQL

#### 3.2 How to restore data?

1. Restore metadata: Restore from database backup
2. Restore data files: Copy backed up files to the corresponding location
3. Start the node: `./storefs -config config.yaml`

#### 3.3 How to replace a disk?

StoreFS supports online disk replacement through the task system. This allows data migration from the old disk to the new disk without service interruption.

Steps:

1. **Set the node to taint status** — mark the node as maintenance mode so no new data is written to it. You can do this via any of:
   - **Web Admin Console**: Node Management → set the target node's status to "taint"
   - **Admin API**: `curl -X PUT http://<node-ip>:7946/api/node-status/<node-name> -H "Authorization: Bearer <token>" -d '{"status":"taint"}'`
   - **MCP**: `storefs_update_node_status(nodeName="<node-name>", status="taint")`

2. **Run a ReplaceDisk task** on the taint-marked node. This migrates data from the old disk to the new disk:
   - **Web Admin Console**: Tasks → Create Task → select "Replace Disk" type, choose the target node, fill in the old and new disk paths
   - **Admin API**: `curl -X POST http://<node-ip>:7946/api/tasks -H "Authorization: Bearer <token>" -H "Content-Type: application/json" -d '{"type":"replacedisk","params":{"oldDiskPath":"/data/old-disk","newDiskPath":"/data/new-disk","nodeName":"<node-name>"}}'`
   - **MCP**: `storefs_create_task(type="replacedisk", params={oldDiskPath: "/data/old-disk", newDiskPath: "/data/new-disk", nodeName: "<node-name>"})`

   You can monitor the task progress via:
   - **Web Admin Console**: Tasks → Active Tasks (progress bar updates in real-time)
   - **Admin API**: `curl http://<node-ip>:7946/api/tasks/<task-id> -H "Authorization: Bearer <token>"`
   - **MCP**: `storefs_get_task(taskId=...)`

3. **Remove the taint status** from the node after the task completes successfully:
   - **Web Admin Console**: Node Management → set the node's status back to "active"
   - **Admin API**: `curl -X PUT http://<node-ip>:7946/api/node-status/<node-name> -H "Authorization: Bearer <token>" -d '{"status":"active"}'`
   - **MCP**: `storefs_update_node_status(nodeName="<node-name>", status="active")`

> **Note**: The `replacedisk` task can also be created and monitored via the S3 console's Task Management interface. Only users with `super_admin` role can perform these operations.

#### 3.4 When to run a Repair task (node/disk)?

The Repair task reconstructs missing or inconsistent fragment metadata. You should run a Repair in the following scenarios:

- **Node restart after an abnormal shutdown** — some fragments may not have been flushed properly, resulting in metadata inconsistency
- **Disk replacement** — after a new disk is added, fragment references on the old disk may need to be updated
- **During cluster recovery** — when metadata and actual data are out of sync, run Repair to scan and rebuild the fragment index
- **Periodic health check** — running Repair proactively during low-load periods helps detect and fix fragment-level issues early

Steps:

1. **Set the node to taint status** (if repairing a specific node):
   - **Web Admin Console**: Node Management → set the target node's status to "taint"
   - **Admin API**: `curl -X PUT http://<node-ip>:7946/api/node-status/<node-name> -H "Authorization: Bearer <token>" -d '{"status":"taint"}'`

2. **Run a Repair task** on the node:
   - **Web Admin Console**: Tasks → Create Task → select "Repair Fragments" type, choose the target node, optionally specify a disk path to repair only that disk
   - **Admin API**: `curl -X POST http://<node-ip>:7946/api/tasks -H "Authorization: Bearer <token>" -H "Content-Type: application/json" -d '{"type":"repair","params":{"node":"<node-name>","disk":"/data/disk1"}}'`
   - **MCP**: `storefs_create_task(type="repair", params={node: "<node-name>", disk: "/data/disk1"})`

   > The `disk` parameter is optional. If omitted, the task will repair all disks on the node.

   You can monitor progress via:
   - **Web Admin Console**: Tasks → Active Tasks
   - **Admin API**: `curl http://<node-ip>:7946/api/tasks/<task-id> -H "Authorization: Bearer <token>"`
   - **MCP**: `storefs_get_task(taskId=...)`

3. **Remove the taint status** from the node after the task completes successfully:
   - **Web Admin Console**: Node Management → set the node's status back to "active"
   - **Admin API**: `curl -X PUT http://<node-ip>:7946/api/node-status/<node-name> -H "Authorization: Bearer <token>" -d '{"status":"active"}'`
   - **MCP**: `storefs_update_node_status(nodeName="<node-name>", status="active")`

> **Note**: A Repair task is read-only — it scans and fixes fragment metadata without moving data. It is safe to run on a live node, but setting the node to taint first is recommended to avoid new writes during the repair process.

#### 3.5 When to run a ReplaceDisk task?

The ReplaceDisk task migrates data from one disk to another on the same node. Unlike Repair, which only fixes metadata, ReplaceDisk physically moves data. You should run a ReplaceDisk in the following scenarios:

- **Disk failure or degradation** — when a disk reports I/O errors, bad sectors, or is about to fail, migrate its data to a healthy disk
- **Disk capacity upgrade** — when replacing a small-capacity disk with a larger one, migrate data to the new disk
- **Disk performance upgrade** — when replacing an HDD with an SSD, migrate data to the faster disk
- **Disk path change** — when the disk mount point or directory path changes, migrate data to the new path
- **Disk decommissioning** — when a disk needs to be removed from the node, migrate its data to other disks first

The operation steps are the same as described in [3.3 How to replace a disk?](#3-3-how-to-replace-a-disk).

> **Note**: ReplaceDisk is a data-moving operation, not read-only. Ensure the target disk has sufficient capacity before starting the task. The node should be set to taint status before running ReplaceDisk to prevent new writes during migration.

### 4. Performance Optimization

#### 4.1 How to improve write performance?

1. Increase the number of nodes to distribute write pressure
2. Use faster storage devices (such as SSD)
3. Adjust disk weights to evenly distribute data
4. Optimize database configurations (such as Apache Doris memory and concurrency settings)

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

### 6. Resource Naming Rules

StoreFS enforces strict validation on resource names throughout the system (both backend API and management console).

#### 6.1 Bucket Name Rules
- Length: 3–63 characters
- Must be lowercase letters, numbers, and hyphens (`-`) only
- Must start and end with a letter or number
- Cannot be formatted as an IP address (e.g., `192.168.1.1`)
- Cannot have consecutive hyphens
- Reserved prefixes `xn--` and `sthree-` are not allowed

#### 6.2 Object Key Rules
- Length: 1–1024 bytes (not characters)
- Must be valid UTF-8
- No control characters (C0/C1 control codes) allowed
- Null characters (`0x00`) are not allowed

#### 6.3 Policy Name Rules
- Length: 1–128 characters
- Allowed characters: letters (`a-zA-Z`), numbers (`0-9`), and `+=,.@_-`
- Case-sensitive (keeps original case)

#### 6.4 Username Rules
- Length: 1–63 characters
- Allowed characters: lowercase letters, numbers, dots (`.`), underscores (`_`), and hyphens (`-`)
- Must start with a letter or number
- Cannot have consecutive dots (`..`)
- Cannot end with a hyphen or dot
- Reserved names not allowed: `admin`, `root`, `system`, `guest`

#### 6.5 Group Name Rules
- Same rules as username (see section 6.4)

### 7. Other Questions

#### 7.1 Why can't uploaded files be downloaded?

Possible reasons:
1. The file was not completely uploaded
2. File metadata is corrupted
3. Network connection issues
4. Permission configuration errors

#### 7.2 How to view system logs?

StoreFS logs are output to standard output by default and can be viewed through the following methods:
1. Redirect to a file when starting: `./storefs -config config.yaml > storefs.log 2>&1`
2. Use system log services (such as systemd)
3. Use Docker log functionality: `docker logs storefs-node1`

### 8. Technical Support

If you encounter problems not solved in this FAQ, please:

1. Check the [Troubleshooting](troubleshooting.md) documentation
2. Check system logs
3. Report issues in GitHub Issues
4. Contact the technical support team
