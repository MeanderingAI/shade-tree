#ifndef DISTRIBUTED_LXC_H
#define DISTRIBUTED_LXC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_NODES 256
#define MAX_CONTAINERS 1024
#define MAX_NAME_LEN 256
#define MAX_PATH_LEN 1024
#define MAX_COMMAND_LEN 2048
#define MAX_LOG_LEN 4096
#define BUFFER_SIZE 8192
#define DEFAULT_PORT 8888

// Message types for node communication
typedef enum {
    MSG_REGISTER_NODE,
    MSG_NODE_HEARTBEAT,
    MSG_DEPLOY_CONTAINER,
    MSG_START_CONTAINER,
    MSG_STOP_CONTAINER,
    MSG_DELETE_CONTAINER,
    MSG_CONTAINER_STATUS,
    MSG_NODE_STATUS,
    MSG_ERROR,
    MSG_ACK
} message_type_t;

// Container states
typedef enum {
    CONTAINER_STOPPED,
    CONTAINER_STARTING,
    CONTAINER_RUNNING,
    CONTAINER_STOPPING,
    CONTAINER_ERROR
} container_state_t;

// Node states
typedef enum {
    NODE_DISCONNECTED,
    NODE_CONNECTING,
    NODE_CONNECTED,
    NODE_BUSY,
    NODE_ERROR
} node_state_t;

// Resource information
typedef struct {
    double cpu_usage;
    double memory_usage;
    double disk_usage;
    int container_count;
    int max_containers;
} resource_info_t;

// LXC Container configuration
typedef struct {
    char name[MAX_NAME_LEN];
    char image[MAX_NAME_LEN];
    char config_file[MAX_PATH_LEN];
    char* environment_vars;
    char* mount_points;
    char* network_config;
    int cpu_limit;
    int memory_limit;
    int privileged;
} lxc_config_t;

// Container instance
typedef struct {
    char id[MAX_NAME_LEN];
    char name[MAX_NAME_LEN];
    char node_id[MAX_NAME_LEN];
    container_state_t state;
    lxc_config_t config;
    time_t created_at;
    time_t started_at;
    char log_file[MAX_PATH_LEN];
} container_t;

// Network node information
typedef struct {
    char id[MAX_NAME_LEN];
    char hostname[MAX_NAME_LEN];
    char ip_address[INET_ADDRSTRLEN];
    int port;
    node_state_t state;
    resource_info_t resources;
    time_t last_heartbeat;
    int socket_fd;
    container_t containers[MAX_CONTAINERS];
    int container_count;
} node_t;

// Message structure for network communication
typedef struct {
    message_type_t type;
    char sender_id[MAX_NAME_LEN];
    char recipient_id[MAX_NAME_LEN];
    int data_length;
    char data[BUFFER_SIZE - sizeof(message_type_t) - 2*MAX_NAME_LEN - sizeof(int)];
} message_t;

// Function prototypes
int init_coordinator(int port);
int init_worker_node(const char* coordinator_ip, int coordinator_port);
int parse_lxc_yaml(const char* yaml_file, lxc_config_t* config);
int deploy_container(const char* node_id, const lxc_config_t* config);
int start_container(const char* container_id);
int stop_container(const char* container_id);
int delete_container(const char* container_id);
container_state_t get_container_status(const char* container_id);
node_t* find_best_node(const lxc_config_t* config);
int send_message(int socket_fd, const message_t* msg);
int receive_message(int socket_fd, message_t* msg);
void cleanup_resources(void);

#endif // DISTRIBUTED_LXC_H