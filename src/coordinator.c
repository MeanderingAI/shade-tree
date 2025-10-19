#include "../include/distributed_lxc.h"
#include "../include/yaml_parser.h"
#include "../include/lxc_manager.h"

// External declarations from network.c
extern node_t* find_node_by_id(const char* node_id);
extern int register_node(const char* node_id, const char* hostname, const char* ip_address, int port);

// Global coordinator state
static container_t deployed_containers[MAX_CONTAINERS];
static int deployed_container_count = 0;
static pthread_mutex_t containers_mutex = PTHREAD_MUTEX_INITIALIZER;

// Find best node for container deployment based on resources
node_t* find_best_node(const lxc_config_t* config) {
    if (!config) return NULL;
    
    node_t* best_node = NULL;
    double best_score = -1.0;
    time_t current_time = time(NULL);
    
    extern node_t nodes[];
    extern int node_count;
    extern pthread_mutex_t nodes_mutex;
    
    pthread_mutex_lock(&nodes_mutex);
    
    for (int i = 0; i < node_count; i++) {
        node_t* node = &nodes[i];
        
        // Skip disconnected or unresponsive nodes
        if (node->state != NODE_CONNECTED || 
            (current_time - node->last_heartbeat) > 30) {
            continue;
        }
        
        // Check if node has capacity
        if (node->container_count >= node->resources.max_containers) {
            continue;
        }
        
        // Calculate node score based on available resources
        double cpu_available = 100.0 - node->resources.cpu_usage;
        double memory_available = 100.0 - node->resources.memory_usage;
        double disk_available = 100.0 - node->resources.disk_usage;
        double container_load = (double)node->container_count / node->resources.max_containers;
        
        // Weighted scoring (CPU: 30%, Memory: 30%, Disk: 20%, Load: 20%)
        double score = (cpu_available * 0.3 + 
                       memory_available * 0.3 + 
                       disk_available * 0.2 + 
                       (1.0 - container_load) * 100.0 * 0.2);
        
        if (score > best_score) {
            best_score = score;
            best_node = node;
        }
    }
    
    pthread_mutex_unlock(&nodes_mutex);
    
    if (best_node) {
        printf("Selected node %s (score: %.2f) for container %s\n", 
               best_node->id, best_score, config->name);
    } else {
        printf("No suitable node found for container %s\n", config->name);
    }
    
    return best_node;
}

// Deploy container to a specific node
int deploy_container(const char* node_id, const lxc_config_t* config) {
    if (!node_id || !config) return -1;
    
    node_t* node = find_node_by_id(node_id);
    if (!node) {
        printf("Error: Node %s not found\n", node_id);
        return -1;
    }
    
    if (node->state != NODE_CONNECTED) {
        printf("Error: Node %s is not connected\n", node_id);
        return -1;
    }
    
    // Create deployment message
    message_t msg;
    create_message(&msg, MSG_DEPLOY_CONTAINER, "coordinator", node_id, 
                   config, sizeof(lxc_config_t));
    
    if (send_message(node->socket_fd, &msg) != 0) {
        printf("Error: Failed to send deployment message to node %s\n", node_id);
        return -1;
    }
    
    // Add container to deployed list
    pthread_mutex_lock(&containers_mutex);
    
    if (deployed_container_count < MAX_CONTAINERS) {
        container_t* container = &deployed_containers[deployed_container_count];
        
        snprintf(container->id, sizeof(container->id), "%s_%s", 
                node_id, config->name);
        strcpy(container->name, config->name);
        strcpy(container->node_id, node_id);
        container->state = CONTAINER_STARTING;
        container->config = *config;
        container->created_at = time(NULL);
        
        deployed_container_count++;
        
        // Add to node's container list
        if (node->container_count < MAX_CONTAINERS) {
            node->containers[node->container_count] = *container;
            node->container_count++;
        }
    }
    
    pthread_mutex_unlock(&containers_mutex);
    
    printf("Container %s deployed to node %s\n", config->name, node_id);
    return 0;
}

// Deploy container using automatic node selection
int deploy_container_auto(const lxc_config_t* config) {
    if (!config) return -1;
    
    node_t* best_node = find_best_node(config);
    if (!best_node) {
        printf("Error: No suitable node available for deployment\n");
        return -1;
    }
    
    return deploy_container(best_node->id, config);
}

// Start a deployed container
int start_container(const char* container_id) {
    if (!container_id) return -1;
    
    pthread_mutex_lock(&containers_mutex);
    
    container_t* container = NULL;
    for (int i = 0; i < deployed_container_count; i++) {
        if (strcmp(deployed_containers[i].id, container_id) == 0) {
            container = &deployed_containers[i];
            break;
        }
    }
    
    if (!container) {
        pthread_mutex_unlock(&containers_mutex);
        printf("Error: Container %s not found\n", container_id);
        return -1;
    }
    
    node_t* node = find_node_by_id(container->node_id);
    if (!node) {
        pthread_mutex_unlock(&containers_mutex);
        printf("Error: Node %s not found for container %s\n", 
               container->node_id, container_id);
        return -1;
    }
    
    // Send start message to node
    message_t msg;
    create_message(&msg, MSG_START_CONTAINER, "coordinator", node->id, 
                   container->name, strlen(container->name));
    
    if (send_message(node->socket_fd, &msg) != 0) {
        pthread_mutex_unlock(&containers_mutex);
        printf("Error: Failed to send start message to node %s\n", node->id);
        return -1;
    }
    
    container->state = CONTAINER_STARTING;
    container->started_at = time(NULL);
    
    pthread_mutex_unlock(&containers_mutex);
    
    printf("Start command sent for container %s\n", container_id);
    return 0;
}

// Stop a running container
int stop_container(const char* container_id) {
    if (!container_id) return -1;
    
    pthread_mutex_lock(&containers_mutex);
    
    container_t* container = NULL;
    for (int i = 0; i < deployed_container_count; i++) {
        if (strcmp(deployed_containers[i].id, container_id) == 0) {
            container = &deployed_containers[i];
            break;
        }
    }
    
    if (!container) {
        pthread_mutex_unlock(&containers_mutex);
        printf("Error: Container %s not found\n", container_id);
        return -1;
    }
    
    node_t* node = find_node_by_id(container->node_id);
    if (!node) {
        pthread_mutex_unlock(&containers_mutex);
        printf("Error: Node %s not found for container %s\n", 
               container->node_id, container_id);
        return -1;
    }
    
    // Send stop message to node
    message_t msg;
    create_message(&msg, MSG_STOP_CONTAINER, "coordinator", node->id, 
                   container->name, strlen(container->name));
    
    if (send_message(node->socket_fd, &msg) != 0) {
        pthread_mutex_unlock(&containers_mutex);
        printf("Error: Failed to send stop message to node %s\n", node->id);
        return -1;
    }
    
    container->state = CONTAINER_STOPPING;
    
    pthread_mutex_unlock(&containers_mutex);
    
    printf("Stop command sent for container %s\n", container_id);
    return 0;
}

// Delete a container
int delete_container(const char* container_id) {
    if (!container_id) return -1;
    
    pthread_mutex_lock(&containers_mutex);
    
    container_t* container = NULL;
    int container_index = -1;
    for (int i = 0; i < deployed_container_count; i++) {
        if (strcmp(deployed_containers[i].id, container_id) == 0) {
            container = &deployed_containers[i];
            container_index = i;
            break;
        }
    }
    
    if (!container) {
        pthread_mutex_unlock(&containers_mutex);
        printf("Error: Container %s not found\n", container_id);
        return -1;
    }
    
    node_t* node = find_node_by_id(container->node_id);
    if (node) {
        // Send delete message to node
        message_t msg;
        create_message(&msg, MSG_DELETE_CONTAINER, "coordinator", node->id, 
                       container->name, strlen(container->name));
        
        if (send_message(node->socket_fd, &msg) != 0) {
            printf("Warning: Failed to send delete message to node %s\n", node->id);
        }
        
        // Remove from node's container list
        for (int i = 0; i < node->container_count; i++) {
            if (strcmp(node->containers[i].id, container_id) == 0) {
                // Shift remaining containers
                for (int j = i; j < node->container_count - 1; j++) {
                    node->containers[j] = node->containers[j + 1];
                }
                node->container_count--;
                break;
            }
        }
    }
    
    // Remove from deployed containers list
    for (int i = container_index; i < deployed_container_count - 1; i++) {
        deployed_containers[i] = deployed_containers[i + 1];
    }
    deployed_container_count--;
    
    pthread_mutex_unlock(&containers_mutex);
    
    printf("Container %s deleted\n", container_id);
    return 0;
}

// Get container status
container_state_t get_container_status(const char* container_id) {
    if (!container_id) return CONTAINER_ERROR;
    
    pthread_mutex_lock(&containers_mutex);
    
    for (int i = 0; i < deployed_container_count; i++) {
        if (strcmp(deployed_containers[i].id, container_id) == 0) {
            container_state_t state = deployed_containers[i].state;
            pthread_mutex_unlock(&containers_mutex);
            return state;
        }
    }
    
    pthread_mutex_unlock(&containers_mutex);
    return CONTAINER_ERROR;
}

// List all containers
void list_containers(void) {
    pthread_mutex_lock(&containers_mutex);
    
    printf("\n=== Deployed Containers ===\n");
    printf("%-20s %-20s %-15s %-10s\n", "ID", "Name", "Node", "State");
    printf("------------------------------------------------------------\n");
    
    for (int i = 0; i < deployed_container_count; i++) {
        container_t* container = &deployed_containers[i];
        const char* state_str;
        
        switch (container->state) {
            case CONTAINER_STOPPED:  state_str = "STOPPED"; break;
            case CONTAINER_STARTING: state_str = "STARTING"; break;
            case CONTAINER_RUNNING:  state_str = "RUNNING"; break;
            case CONTAINER_STOPPING: state_str = "STOPPING"; break;
            case CONTAINER_ERROR:    state_str = "ERROR"; break;
            default:                 state_str = "UNKNOWN"; break;
        }
        
        printf("%-20s %-20s %-15s %-10s\n", 
               container->id, container->name, container->node_id, state_str);
    }
    
    pthread_mutex_unlock(&containers_mutex);
}

// List all nodes
void list_nodes(void) {
    extern node_t nodes[];
    extern int node_count;
    extern pthread_mutex_t nodes_mutex;
    
    pthread_mutex_lock(&nodes_mutex);
    
    printf("\n=== Connected Nodes ===\n");
    printf("%-15s %-20s %-15s %-10s %-10s %-10s\n", 
           "ID", "Hostname", "IP", "State", "CPU%", "Mem%");
    printf("------------------------------------------------------------------------\n");
    
    for (int i = 0; i < node_count; i++) {
        node_t* node = &nodes[i];
        const char* state_str;
        
        switch (node->state) {
            case NODE_DISCONNECTED: state_str = "DISC"; break;
            case NODE_CONNECTING:   state_str = "CONN"; break;
            case NODE_CONNECTED:    state_str = "UP"; break;
            case NODE_BUSY:         state_str = "BUSY"; break;
            case NODE_ERROR:        state_str = "ERROR"; break;
            default:                state_str = "UNK"; break;
        }
        
        printf("%-15s %-20s %-15s %-10s %-10.1f %-10.1f\n", 
               node->id, node->hostname, node->ip_address, state_str,
               node->resources.cpu_usage, node->resources.memory_usage);
    }
    
    pthread_mutex_unlock(&nodes_mutex);
}

// Interactive coordinator command interface
void coordinator_command_loop(void) {
    char command[MAX_COMMAND_LEN];
    char yaml_file[MAX_PATH_LEN];
    char container_id[MAX_NAME_LEN];
    
    printf("\n=== Distributed LXC Coordinator ===\n");
    printf("Commands:\n");
    printf("  deploy <yaml_file>  - Deploy container from YAML\n");
    printf("  start <container_id> - Start container\n");
    printf("  stop <container_id>  - Stop container\n");
    printf("  delete <container_id> - Delete container\n");
    printf("  list containers      - List all containers\n");
    printf("  list nodes          - List all nodes\n");
    printf("  quit                - Exit coordinator\n\n");
    
    while (1) {
        printf("coordinator> ");
        fflush(stdout);
        
        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = '\0';
        
        if (strncmp(command, "deploy ", 7) == 0) {
            sscanf(command + 7, "%s", yaml_file);
            
            lxc_config_t config;
            if (parse_lxc_yaml(yaml_file, &config) == 0) {
                deploy_container_auto(&config);
            } else {
                printf("Error: Failed to parse YAML file %s\n", yaml_file);
            }
            
        } else if (strncmp(command, "start ", 6) == 0) {
            sscanf(command + 6, "%s", container_id);
            start_container(container_id);
            
        } else if (strncmp(command, "stop ", 5) == 0) {
            sscanf(command + 5, "%s", container_id);
            stop_container(container_id);
            
        } else if (strncmp(command, "delete ", 7) == 0) {
            sscanf(command + 7, "%s", container_id);
            delete_container(container_id);
            
        } else if (strcmp(command, "list containers") == 0) {
            list_containers();
            
        } else if (strcmp(command, "list nodes") == 0) {
            list_nodes();
            
        } else if (strcmp(command, "quit") == 0) {
            break;
            
        } else if (strlen(command) > 0) {
            printf("Unknown command: %s\n", command);
        }
    }
}

// Main coordinator function
int main(int argc, char* argv[]) {
    int port = DEFAULT_PORT;
    
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            printf("Error: Invalid port number %s\n", argv[1]);
            return 1;
        }
    }
    
    printf("Starting Distributed LXC Coordinator on port %d\n", port);
    
    // Set up signal handling for cleanup
    signal(SIGINT, cleanup_resources);
    signal(SIGTERM, cleanup_resources);
    
    // Start coordinator in background thread
    pthread_t coordinator_thread;
    int* port_ptr = malloc(sizeof(int));
    *port_ptr = port;
    
    if (pthread_create(&coordinator_thread, NULL, 
                      (void*(*)(void*))init_coordinator, port_ptr) != 0) {
        printf("Error: Failed to start coordinator thread\n");
        return 1;
    }
    
    // Give the server time to start
    sleep(1);
    
    // Start interactive command loop
    coordinator_command_loop();
    
    // Cleanup
    cleanup_resources();
    
    return 0;
}