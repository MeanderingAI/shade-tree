#include "../include/distributed_lxc.h"

// Global variables for network communication
static int server_socket = -1;
static node_t nodes[MAX_NODES];
static int node_count = 0;
static pthread_mutex_t nodes_mutex = PTHREAD_MUTEX_INITIALIZER;

// Send a message over a socket
int send_message(int socket_fd, const message_t* msg) {
    if (socket_fd < 0 || !msg) return -1;
    
    ssize_t bytes_sent = send(socket_fd, msg, sizeof(message_t), 0);
    if (bytes_sent != sizeof(message_t)) {
        printf("Error: Failed to send complete message (%zd bytes sent)\n", bytes_sent);
        return -1;
    }
    
    return 0;
}

// Receive a message from a socket
int receive_message(int socket_fd, message_t* msg) {
    if (socket_fd < 0 || !msg) return -1;
    
    ssize_t bytes_received = recv(socket_fd, msg, sizeof(message_t), 0);
    if (bytes_received != sizeof(message_t)) {
        if (bytes_received == 0) {
            printf("Connection closed by peer\n");
        } else if (bytes_received < 0) {
            printf("Error receiving message: %s\n", strerror(errno));
        } else {
            printf("Error: Incomplete message received (%zd bytes)\n", bytes_received);
        }
        return -1;
    }
    
    return 0;
}

// Create a message
void create_message(message_t* msg, message_type_t type, const char* sender_id, 
                   const char* recipient_id, const void* data, int data_len) {
    if (!msg) return;
    
    memset(msg, 0, sizeof(message_t));
    msg->type = type;
    
    if (sender_id) {
        strncpy(msg->sender_id, sender_id, MAX_NAME_LEN - 1);
    }
    
    if (recipient_id) {
        strncpy(msg->recipient_id, recipient_id, MAX_NAME_LEN - 1);
    }
    
    if (data && data_len > 0) {
        int copy_len = (data_len < sizeof(msg->data)) ? data_len : sizeof(msg->data);
        memcpy(msg->data, data, copy_len);
        msg->data_length = copy_len;
    }
}

// Find node by ID
node_t* find_node_by_id(const char* node_id) {
    if (!node_id) return NULL;
    
    pthread_mutex_lock(&nodes_mutex);
    for (int i = 0; i < node_count; i++) {
        if (strcmp(nodes[i].id, node_id) == 0) {
            pthread_mutex_unlock(&nodes_mutex);
            return &nodes[i];
        }
    }
    pthread_mutex_unlock(&nodes_mutex);
    return NULL;
}

// Add a new node to the cluster
int register_node(const char* node_id, const char* hostname, const char* ip_address, int port) {
    if (!node_id || !hostname || !ip_address) return -1;
    
    pthread_mutex_lock(&nodes_mutex);
    
    if (node_count >= MAX_NODES) {
        pthread_mutex_unlock(&nodes_mutex);
        printf("Error: Maximum number of nodes reached\n");
        return -1;
    }
    
    // Check if node already exists
    for (int i = 0; i < node_count; i++) {
        if (strcmp(nodes[i].id, node_id) == 0) {
            // Update existing node
            strcpy(nodes[i].hostname, hostname);
            strcpy(nodes[i].ip_address, ip_address);
            nodes[i].port = port;
            nodes[i].state = NODE_CONNECTED;
            nodes[i].last_heartbeat = time(NULL);
            pthread_mutex_unlock(&nodes_mutex);
            return 0;
        }
    }
    
    // Add new node
    strcpy(nodes[node_count].id, node_id);
    strcpy(nodes[node_count].hostname, hostname);
    strcpy(nodes[node_count].ip_address, ip_address);
    nodes[node_count].port = port;
    nodes[node_count].state = NODE_CONNECTED;
    nodes[node_count].last_heartbeat = time(NULL);
    nodes[node_count].container_count = 0;
    memset(&nodes[node_count].resources, 0, sizeof(resource_info_t));
    
    node_count++;
    pthread_mutex_unlock(&nodes_mutex);
    
    printf("Node %s registered successfully\n", node_id);
    return 0;
}

// Remove a node from the cluster
int unregister_node(const char* node_id) {
    if (!node_id) return -1;
    
    pthread_mutex_lock(&nodes_mutex);
    
    for (int i = 0; i < node_count; i++) {
        if (strcmp(nodes[i].id, node_id) == 0) {
            // Close socket if open
            if (nodes[i].socket_fd > 0) {
                close(nodes[i].socket_fd);
            }
            
            // Shift remaining nodes
            for (int j = i; j < node_count - 1; j++) {
                nodes[j] = nodes[j + 1];
            }
            node_count--;
            pthread_mutex_unlock(&nodes_mutex);
            
            printf("Node %s unregistered\n", node_id);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&nodes_mutex);
    return -1;
}

// Handle client connection
void* handle_client_connection(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);
    
    message_t msg;
    char node_id[MAX_NAME_LEN] = {0};
    
    printf("New client connected (socket %d)\n", client_socket);
    
    while (1) {
        if (receive_message(client_socket, &msg) != 0) {
            break;
        }
        
        switch (msg.type) {
            case MSG_REGISTER_NODE: {
                // Extract node information from message data
                char* data_ptr = msg.data;
                char hostname[MAX_NAME_LEN];
                char ip_address[INET_ADDRSTRLEN];
                int port;
                
                sscanf(data_ptr, "%s %s %d", hostname, ip_address, &port);
                
                strcpy(node_id, msg.sender_id);
                if (register_node(node_id, hostname, ip_address, port) == 0) {
                    // Update socket in node structure
                    node_t* node = find_node_by_id(node_id);
                    if (node) {
                        node->socket_fd = client_socket;
                    }
                    
                    // Send acknowledgment
                    message_t ack_msg;
                    create_message(&ack_msg, MSG_ACK, "coordinator", node_id, "registered", 10);
                    send_message(client_socket, &ack_msg);
                }
                break;
            }
            
            case MSG_NODE_HEARTBEAT: {
                node_t* node = find_node_by_id(msg.sender_id);
                if (node) {
                    node->last_heartbeat = time(NULL);
                    node->state = NODE_CONNECTED;
                    
                    // Update resource information if provided
                    if (msg.data_length >= sizeof(resource_info_t)) {
                        memcpy(&node->resources, msg.data, sizeof(resource_info_t));
                    }
                }
                break;
            }
            
            case MSG_CONTAINER_STATUS: {
                node_t* node = find_node_by_id(msg.sender_id);
                if (node && msg.data_length >= sizeof(container_t)) {
                    // Update container status
                    container_t* container_update = (container_t*)msg.data;
                    for (int i = 0; i < node->container_count; i++) {
                        if (strcmp(node->containers[i].id, container_update->id) == 0) {
                            node->containers[i].state = container_update->state;
                            break;
                        }
                    }
                }
                break;
            }
            
            case MSG_ERROR: {
                printf("Error from node %s: %s\n", msg.sender_id, msg.data);
                break;
            }
            
            default:
                printf("Unknown message type received: %d\n", msg.type);
                break;
        }
    }
    
    // Clean up when client disconnects
    if (strlen(node_id) > 0) {
        node_t* node = find_node_by_id(node_id);
        if (node) {
            node->state = NODE_DISCONNECTED;
            node->socket_fd = -1;
        }
        printf("Node %s disconnected\n", node_id);
    }
    
    close(client_socket);
    return NULL;
}

// Initialize coordinator server
int init_coordinator(int port) {
    struct sockaddr_in server_addr;
    int opt = 1;
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        printf("Error creating socket: %s\n", strerror(errno));
        return -1;
    }
    
    // Set socket options
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("Error setting socket options: %s\n", strerror(errno));
        close(server_socket);
        return -1;
    }
    
    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error binding socket: %s\n", strerror(errno));
        close(server_socket);
        return -1;
    }
    
    // Listen for connections
    if (listen(server_socket, 10) < 0) {
        printf("Error listening on socket: %s\n", strerror(errno));
        close(server_socket);
        return -1;
    }
    
    printf("Coordinator server started on port %d\n", port);
    
    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            printf("Error accepting connection: %s\n", strerror(errno));
            continue;
        }
        
        // Create thread to handle client
        pthread_t client_thread;
        int* socket_ptr = malloc(sizeof(int));
        *socket_ptr = client_socket;
        
        if (pthread_create(&client_thread, NULL, handle_client_connection, socket_ptr) != 0) {
            printf("Error creating client thread: %s\n", strerror(errno));
            close(client_socket);
            free(socket_ptr);
        } else {
            pthread_detach(client_thread);
        }
    }
    
    return 0;
}

// Connect to coordinator as worker node
int init_worker_node(const char* coordinator_ip, int coordinator_port) {
    struct sockaddr_in server_addr;
    int socket_fd;
    
    // Create socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        printf("Error creating socket: %s\n", strerror(errno));
        return -1;
    }
    
    // Connect to coordinator
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(coordinator_port);
    
    if (inet_pton(AF_INET, coordinator_ip, &server_addr.sin_addr) <= 0) {
        printf("Invalid coordinator IP address: %s\n", coordinator_ip);
        close(socket_fd);
        return -1;
    }
    
    if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error connecting to coordinator: %s\n", strerror(errno));
        close(socket_fd);
        return -1;
    }
    
    printf("Connected to coordinator at %s:%d\n", coordinator_ip, coordinator_port);
    return socket_fd;
}

// Cleanup network resources
void cleanup_network_resources(void) {
    if (server_socket >= 0) {
        close(server_socket);
        server_socket = -1;
    }
    
    pthread_mutex_lock(&nodes_mutex);
    for (int i = 0; i < node_count; i++) {
        if (nodes[i].socket_fd >= 0) {
            close(nodes[i].socket_fd);
        }
    }
    node_count = 0;
    pthread_mutex_unlock(&nodes_mutex);
}