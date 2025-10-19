#include "../include/distributed_lxc.h"
#include "../include/yaml_parser.h"
#include "../include/lxc_manager.h"
#include <sys/utsname.h>

// Worker node state
static char node_id[MAX_NAME_LEN];
static char coordinator_ip[INET_ADDRSTRLEN];
static int coordinator_port;
static int coordinator_socket = -1;
static container_t local_containers[MAX_CONTAINERS];
static int local_container_count = 0;
static pthread_mutex_t local_containers_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int running = 1;

// Generate unique node ID
void generate_node_id(char* buffer, size_t buffer_size) {
    struct utsname system_info;
    if (uname(&system_info) == 0) {
        snprintf(buffer, buffer_size, "%s_%ld", 
                system_info.nodename, (long)getpid());
    } else {
        snprintf(buffer, buffer_size, "node_%ld", (long)getpid());
    }
}

// Send heartbeat to coordinator
void* heartbeat_thread(void* arg) {
    while (running && coordinator_socket >= 0) {
        message_t msg;
        resource_info_t resources;
        
        // Get current system resources
        if (get_system_resources(&resources) == 0) {
            create_message(&msg, MSG_NODE_HEARTBEAT, node_id, "coordinator", 
                          &resources, sizeof(resource_info_t));
            
            if (send_message(coordinator_socket, &msg) != 0) {
                printf("Warning: Failed to send heartbeat\n");
            }
        }
        
        sleep(10); // Send heartbeat every 10 seconds
    }
    
    return NULL;
}

// Handle container deployment
int handle_deploy_container(const lxc_config_t* config) {
    if (!config) return -1;
    
    printf("Deploying container: %s\n", config->name);
    
    // Create the container
    if (lxc_create_container(config) != 0) {
        printf("Error: Failed to create container %s\n", config->name);
        return -1;
    }
    
    // Add to local container list
    pthread_mutex_lock(&local_containers_mutex);
    
    if (local_container_count < MAX_CONTAINERS) {
        container_t* container = &local_containers[local_container_count];
        
        snprintf(container->id, sizeof(container->id), "%s_%s", 
                node_id, config->name);
        strcpy(container->name, config->name);
        strcpy(container->node_id, node_id);
        container->state = CONTAINER_STOPPED;
        container->config = *config;
        container->created_at = time(NULL);
        
        local_container_count++;
        printf("Container %s deployed successfully\n", config->name);
    } else {
        pthread_mutex_unlock(&local_containers_mutex);
        printf("Error: Maximum container limit reached\n");
        return -1;
    }
    
    pthread_mutex_unlock(&local_containers_mutex);
    return 0;
}

// Handle container start
int handle_start_container(const char* container_name) {
    if (!container_name) return -1;
    
    printf("Starting container: %s\n", container_name);
    
    // Find container in local list
    pthread_mutex_lock(&local_containers_mutex);
    
    container_t* container = NULL;
    for (int i = 0; i < local_container_count; i++) {
        if (strcmp(local_containers[i].name, container_name) == 0) {
            container = &local_containers[i];
            break;
        }
    }
    
    if (!container) {
        pthread_mutex_unlock(&local_containers_mutex);
        printf("Error: Container %s not found locally\n", container_name);
        return -1;
    }
    
    container->state = CONTAINER_STARTING;
    pthread_mutex_unlock(&local_containers_mutex);
    
    // Start the container
    if (lxc_start_container(container_name) == 0) {
        pthread_mutex_lock(&local_containers_mutex);
        container->state = CONTAINER_RUNNING;
        container->started_at = time(NULL);
        pthread_mutex_unlock(&local_containers_mutex);
        
        // Notify coordinator of status change
        message_t msg;
        create_message(&msg, MSG_CONTAINER_STATUS, node_id, "coordinator", 
                      container, sizeof(container_t));
        send_message(coordinator_socket, &msg);
        
        printf("Container %s started successfully\n", container_name);
        return 0;
    } else {
        pthread_mutex_lock(&local_containers_mutex);
        container->state = CONTAINER_ERROR;
        pthread_mutex_unlock(&local_containers_mutex);
        
        printf("Error: Failed to start container %s\n", container_name);
        return -1;
    }
}

// Handle container stop
int handle_stop_container(const char* container_name) {
    if (!container_name) return -1;
    
    printf("Stopping container: %s\n", container_name);
    
    // Find container in local list
    pthread_mutex_lock(&local_containers_mutex);
    
    container_t* container = NULL;
    for (int i = 0; i < local_container_count; i++) {
        if (strcmp(local_containers[i].name, container_name) == 0) {
            container = &local_containers[i];
            break;
        }
    }
    
    if (!container) {
        pthread_mutex_unlock(&local_containers_mutex);
        printf("Error: Container %s not found locally\n", container_name);
        return -1;
    }
    
    container->state = CONTAINER_STOPPING;
    pthread_mutex_unlock(&local_containers_mutex);
    
    // Stop the container
    if (lxc_stop_container(container_name) == 0) {
        pthread_mutex_lock(&local_containers_mutex);
        container->state = CONTAINER_STOPPED;
        pthread_mutex_unlock(&local_containers_mutex);
        
        // Notify coordinator of status change
        message_t msg;
        create_message(&msg, MSG_CONTAINER_STATUS, node_id, "coordinator", 
                      container, sizeof(container_t));
        send_message(coordinator_socket, &msg);
        
        printf("Container %s stopped successfully\n", container_name);
        return 0;
    } else {
        pthread_mutex_lock(&local_containers_mutex);
        container->state = CONTAINER_ERROR;
        pthread_mutex_unlock(&local_containers_mutex);
        
        printf("Error: Failed to stop container %s\n", container_name);
        return -1;
    }
}

// Handle container deletion
int handle_delete_container(const char* container_name) {
    if (!container_name) return -1;
    
    printf("Deleting container: %s\n", container_name);
    
    // Find and remove container from local list
    pthread_mutex_lock(&local_containers_mutex);
    
    int container_index = -1;
    for (int i = 0; i < local_container_count; i++) {
        if (strcmp(local_containers[i].name, container_name) == 0) {
            container_index = i;
            break;
        }
    }
    
    if (container_index == -1) {
        pthread_mutex_unlock(&local_containers_mutex);
        printf("Error: Container %s not found locally\n", container_name);
        return -1;
    }
    
    // Remove from local list
    for (int i = container_index; i < local_container_count - 1; i++) {
        local_containers[i] = local_containers[i + 1];
    }
    local_container_count--;
    
    pthread_mutex_unlock(&local_containers_mutex);
    
    // Delete the container
    if (lxc_destroy_container(container_name) == 0) {
        printf("Container %s deleted successfully\n", container_name);
        return 0;
    } else {
        printf("Error: Failed to delete container %s\n", container_name);
        return -1;
    }
}

// Message handling loop
void* message_handler_thread(void* arg) {
    message_t msg;
    
    while (running && coordinator_socket >= 0) {
        if (receive_message(coordinator_socket, &msg) != 0) {
            printf("Connection to coordinator lost\n");
            break;
        }
        
        switch (msg.type) {
            case MSG_DEPLOY_CONTAINER: {
                if (msg.data_length >= sizeof(lxc_config_t)) {
                    lxc_config_t* config = (lxc_config_t*)msg.data;
                    
                    if (handle_deploy_container(config) == 0) {
                        // Send acknowledgment
                        message_t ack_msg;
                        create_message(&ack_msg, MSG_ACK, node_id, "coordinator", 
                                      "deployed", 8);
                        send_message(coordinator_socket, &ack_msg);
                    } else {
                        // Send error
                        message_t error_msg;
                        create_message(&error_msg, MSG_ERROR, node_id, "coordinator", 
                                      "deployment failed", 17);
                        send_message(coordinator_socket, &error_msg);
                    }
                }
                break;
            }
            
            case MSG_START_CONTAINER: {
                char container_name[MAX_NAME_LEN];
                int name_len = (msg.data_length < MAX_NAME_LEN) ? 
                              msg.data_length : MAX_NAME_LEN - 1;
                strncpy(container_name, msg.data, name_len);
                container_name[name_len] = '\0';
                
                if (handle_start_container(container_name) == 0) {
                    message_t ack_msg;
                    create_message(&ack_msg, MSG_ACK, node_id, "coordinator", 
                                  "started", 7);
                    send_message(coordinator_socket, &ack_msg);
                } else {
                    message_t error_msg;
                    create_message(&error_msg, MSG_ERROR, node_id, "coordinator", 
                                  "start failed", 12);
                    send_message(coordinator_socket, &error_msg);
                }
                break;
            }
            
            case MSG_STOP_CONTAINER: {
                char container_name[MAX_NAME_LEN];
                int name_len = (msg.data_length < MAX_NAME_LEN) ? 
                              msg.data_length : MAX_NAME_LEN - 1;
                strncpy(container_name, msg.data, name_len);
                container_name[name_len] = '\0';
                
                if (handle_stop_container(container_name) == 0) {
                    message_t ack_msg;
                    create_message(&ack_msg, MSG_ACK, node_id, "coordinator", 
                                  "stopped", 7);
                    send_message(coordinator_socket, &ack_msg);
                } else {
                    message_t error_msg;
                    create_message(&error_msg, MSG_ERROR, node_id, "coordinator", 
                                  "stop failed", 11);
                    send_message(coordinator_socket, &error_msg);
                }
                break;
            }
            
            case MSG_DELETE_CONTAINER: {
                char container_name[MAX_NAME_LEN];
                int name_len = (msg.data_length < MAX_NAME_LEN) ? 
                              msg.data_length : MAX_NAME_LEN - 1;
                strncpy(container_name, msg.data, name_len);
                container_name[name_len] = '\0';
                
                if (handle_delete_container(container_name) == 0) {
                    message_t ack_msg;
                    create_message(&ack_msg, MSG_ACK, node_id, "coordinator", 
                                  "deleted", 7);
                    send_message(coordinator_socket, &ack_msg);
                } else {
                    message_t error_msg;
                    create_message(&error_msg, MSG_ERROR, node_id, "coordinator", 
                                  "delete failed", 13);
                    send_message(coordinator_socket, &error_msg);
                }
                break;
            }
            
            default:
                printf("Unknown message type received: %d\n", msg.type);
                break;
        }
    }
    
    return NULL;
}

// Register with coordinator
int register_with_coordinator(void) {
    struct utsname system_info;
    char hostname[MAX_NAME_LEN];
    char registration_data[MAX_COMMAND_LEN];
    
    if (uname(&system_info) != 0) {
        strcpy(hostname, "unknown");
    } else {
        strcpy(hostname, system_info.nodename);
    }
    
    // Get local IP address (simplified - gets first non-loopback interface)
    char local_ip[INET_ADDRSTRLEN] = "127.0.0.1";
    FILE* ip_cmd = popen("hostname -I | awk '{print $1}'", "r");
    if (ip_cmd) {
        if (fgets(local_ip, sizeof(local_ip), ip_cmd)) {
            // Remove newline
            local_ip[strcspn(local_ip, "\n")] = '\0';
        }
        pclose(ip_cmd);
    }
    
    // Create registration data
    snprintf(registration_data, sizeof(registration_data), "%s %s %d", 
             hostname, local_ip, 0); // Port 0 for worker nodes
    
    // Send registration message
    message_t msg;
    create_message(&msg, MSG_REGISTER_NODE, node_id, "coordinator", 
                   registration_data, strlen(registration_data));
    
    if (send_message(coordinator_socket, &msg) != 0) {
        printf("Error: Failed to send registration message\n");
        return -1;
    }
    
    // Wait for acknowledgment
    message_t ack_msg;
    if (receive_message(coordinator_socket, &ack_msg) != 0) {
        printf("Error: Failed to receive registration acknowledgment\n");
        return -1;
    }
    
    if (ack_msg.type != MSG_ACK) {
        printf("Error: Registration failed\n");
        return -1;
    }
    
    printf("Successfully registered with coordinator as %s\n", node_id);
    return 0;
}

// Signal handler for cleanup
void worker_cleanup(int sig) {
    printf("\nShutting down worker node...\n");
    running = 0;
    
    if (coordinator_socket >= 0) {
        close(coordinator_socket);
        coordinator_socket = -1;
    }
    
    exit(0);
}

// Main worker function
int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <coordinator_ip> <coordinator_port>\n", argv[0]);
        return 1;
    }
    
    strcpy(coordinator_ip, argv[1]);
    coordinator_port = atoi(argv[2]);
    
    if (coordinator_port <= 0 || coordinator_port > 65535) {
        printf("Error: Invalid port number %s\n", argv[2]);
        return 1;
    }
    
    // Generate unique node ID
    generate_node_id(node_id, sizeof(node_id));
    
    printf("Starting LXC Worker Node: %s\n", node_id);
    printf("Connecting to coordinator at %s:%d\n", coordinator_ip, coordinator_port);
    
    // Set up signal handling
    signal(SIGINT, worker_cleanup);
    signal(SIGTERM, worker_cleanup);
    
    // Connect to coordinator
    coordinator_socket = init_worker_node(coordinator_ip, coordinator_port);
    if (coordinator_socket < 0) {
        printf("Error: Failed to connect to coordinator\n");
        return 1;
    }
    
    // Register with coordinator
    if (register_with_coordinator() != 0) {
        close(coordinator_socket);
        return 1;
    }
    
    // Start heartbeat thread
    pthread_t heartbeat_tid;
    if (pthread_create(&heartbeat_tid, NULL, heartbeat_thread, NULL) != 0) {
        printf("Error: Failed to start heartbeat thread\n");
        close(coordinator_socket);
        return 1;
    }
    
    // Start message handler thread
    pthread_t message_tid;
    if (pthread_create(&message_tid, NULL, message_handler_thread, NULL) != 0) {
        printf("Error: Failed to start message handler thread\n");
        close(coordinator_socket);
        return 1;
    }
    
    printf("Worker node %s is ready and waiting for tasks...\n", node_id);
    
    // Wait for threads to complete
    pthread_join(message_tid, NULL);
    pthread_join(heartbeat_tid, NULL);
    
    // Cleanup
    close(coordinator_socket);
    
    return 0;
}