# Distributed LXC Management System

A distributed container management system that allows you to deploy and manage LXC containers across multiple computers in a network. The system consists of a coordinator node that manages the cluster and worker nodes that execute container operations.

## Features

- **Distributed Architecture**: Deploy containers across multiple machines
- **YAML Configuration**: Define containers using YAML files
- **Automatic Load Balancing**: Intelligent container placement based on resource usage
- **Real-time Monitoring**: Monitor container and node status
- **Fault Tolerance**: Handle node failures gracefully
- **Interactive CLI**: Easy-to-use command-line interface

## Architecture

The system consists of two main components:

1. **Coordinator**: The master node that receives deployment requests and distributes containers across worker nodes
2. **Worker**: Nodes that execute container operations and report status back to the coordinator

## Prerequisites

- Linux operating system (Ubuntu 18.04+ recommended)
- LXC/LXD installed on all nodes
- GCC compiler with pthread support
- Network connectivity between nodes

### Installing LXC/LXD

On Ubuntu/Debian:
```bash
sudo apt update
sudo apt install lxc lxd
sudo lxd init
```

## Building the System

1. Clone or download the source code
2. Navigate to the project directory
3. Build the system:

```bash
make check-deps  # Check dependencies
make all         # Build all components
```

For a release build with optimizations:
```bash
make release
```

For debugging:
```bash
make debug
```

## Installation

To install system-wide:
```bash
sudo make install
```

This will install the binaries to `/usr/local/bin/` and configuration files to `/etc/distributed-lxc/`.

## Usage

### Starting the Coordinator

```bash
# Using installed binary
dlxc-coordinator [port]

# Or from build directory
./bin/coordinator [port]
```

Default port is 8888 if not specified.

### Starting Worker Nodes

On each worker machine:
```bash
# Using installed binary
dlxc-worker <coordinator_ip> <coordinator_port>

# Or from build directory
./bin/worker <coordinator_ip> <coordinator_port>
```

Example:
```bash
dlxc-worker 192.168.1.100 8888
```

### Container Management

Once the coordinator is running and workers are connected, you can manage containers using the interactive CLI:

```
coordinator> list nodes              # List connected nodes
coordinator> deploy container.yaml   # Deploy a container
coordinator> list containers         # List all containers
coordinator> start container_id      # Start a container
coordinator> stop container_id       # Stop a container
coordinator> delete container_id     # Delete a container
coordinator> quit                    # Exit coordinator
```

## Container Configuration

Containers are defined using YAML files. Here's an example:

```yaml
name: my-web-server
image: ubuntu:20.04
cpu_limit: 2
memory_limit: 512
privileged: false
environment:
  - PORT=8080
  - ENV=production
mounts:
  - /host/path:/container/path
network:
  type: bridge
  bridge: lxcbr0
```

## Configuration Files

### Coordinator Configuration (`/etc/distributed-lxc/coordinator.conf`)

```ini
[coordinator]
port = 8888
max_nodes = 256
max_containers = 1024

[logging]
log_level = INFO
log_file = /var/log/distributed-lxc/coordinator.log
```

### Worker Configuration (`/etc/distributed-lxc/worker.conf`)

```ini
[worker]
coordinator_ip = 127.0.0.1
coordinator_port = 8888
max_containers = 50

[containers]
default_image = ubuntu:20.04
storage_path = /var/lib/lxc
```

## Network Protocol

The system uses a custom TCP-based protocol for communication between coordinator and worker nodes:

- **MSG_REGISTER_NODE**: Worker registration
- **MSG_NODE_HEARTBEAT**: Periodic status updates
- **MSG_DEPLOY_CONTAINER**: Container deployment
- **MSG_START_CONTAINER**: Start container command
- **MSG_STOP_CONTAINER**: Stop container command
- **MSG_DELETE_CONTAINER**: Delete container command
- **MSG_CONTAINER_STATUS**: Container status updates
- **MSG_ACK**: Acknowledgment messages
- **MSG_ERROR**: Error notifications

## Load Balancing Algorithm

The coordinator uses a weighted scoring system to select the best node for container deployment:

- **CPU Usage** (30%): Available CPU capacity
- **Memory Usage** (30%): Available memory
- **Disk Usage** (20%): Available disk space
- **Container Load** (20%): Number of containers vs. capacity

The node with the highest score is selected for deployment.

## Monitoring

### Node Status
- Connection state
- Resource utilization (CPU, memory, disk)
- Container count
- Last heartbeat timestamp

### Container Status
- Running state (STOPPED, STARTING, RUNNING, STOPPING, ERROR)
- Resource usage
- Deployment timestamp
- Associated node

## Troubleshooting

### Common Issues

1. **Connection Refused**
   - Check if coordinator is running
   - Verify network connectivity
   - Check firewall settings

2. **Container Creation Failed**
   - Verify LXC is installed and configured
   - Check permissions
   - Ensure sufficient resources

3. **Node Not Registering**
   - Check coordinator IP and port
   - Verify network connectivity
   - Check logs for error messages

### Logs

- Coordinator logs: `/var/log/distributed-lxc/coordinator.log`
- Worker logs: `/var/log/distributed-lxc/worker.log`
- System logs: `journalctl -u distributed-lxc`

## Development

### Project Structure

```
distributed-lxc/
├── src/                 # Source code
│   ├── coordinator.c    # Coordinator implementation
│   ├── worker.c         # Worker implementation
│   ├── network.c        # Network communication
│   ├── yaml_parser.c    # YAML parsing
│   └── lxc_manager.c    # LXC management
├── include/             # Header files
├── config/              # Configuration files
├── examples/            # Example YAML files
├── Makefile            # Build system
└── README.md           # This file
```

### Building from Source

```bash
git clone <repository_url>
cd distributed-lxc
make check-deps
make all
```

### Running Tests

```bash
make test
```

### Creating Packages

```bash
make package
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## License

This project is licensed under the MIT License. See the LICENSE file for details.

## Support

For issues and questions:
- Check the troubleshooting section
- Review log files
- Create an issue in the repository

## Roadmap

- [ ] SSL/TLS support for secure communication
- [ ] Authentication and authorization
- [ ] Web-based management interface
- [ ] Container migration between nodes
- [ ] Docker container support
- [ ] High availability coordinator
- [ ] Metrics and alerting integration
- [ ] Auto-scaling capabilities