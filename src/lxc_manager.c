#include "../include/lxc_manager.h"
#include <sys/wait.h>

// Execute a system command and return the exit code
int execute_command(const char* command, char* output, size_t output_size) {
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        printf("Error: Failed to execute command: %s\n", command);
        return -1;
    }
    
    if (output && output_size > 0) {
        size_t bytes_read = fread(output, 1, output_size - 1, pipe);
        output[bytes_read] = '\0';
    }
    
    int exit_code = pclose(pipe);
    return WEXITSTATUS(exit_code);
}

// Check if LXC container exists
int lxc_container_exists(const char* name) {
    char command[MAX_COMMAND_LEN];
    snprintf(command, sizeof(command), "lxc info %s >/dev/null 2>&1", name);
    return (execute_command(command, NULL, 0) == 0) ? 1 : 0;
}

// Generate LXC configuration file
int generate_lxc_config_file(const lxc_config_t* config, const char* output_path) {
    if (!config || !output_path) return -1;
    
    FILE* file = fopen(output_path, "w");
    if (!file) {
        printf("Error: Cannot create config file %s\n", output_path);
        return -1;
    }
    
    fprintf(file, "# LXC Configuration for %s\n", config->name);
    fprintf(file, "lxc.uts.name = %s\n", config->name);
    
    if (config->cpu_limit > 0) {
        fprintf(file, "lxc.cgroup2.cpu.max = %d\n", config->cpu_limit);
    }
    
    if (config->memory_limit > 0) {
        fprintf(file, "lxc.cgroup2.memory.max = %dM\n", config->memory_limit);
    }
    
    if (config->privileged) {
        fprintf(file, "lxc.init.uid = 0\n");
        fprintf(file, "lxc.init.gid = 0\n");
    } else {
        fprintf(file, "lxc.idmap = u 0 100000 65536\n");
        fprintf(file, "lxc.idmap = g 0 100000 65536\n");
    }
    
    // Network configuration
    if (config->network_config) {
        fprintf(file, "lxc.net.0.type = veth\n");
        fprintf(file, "lxc.net.0.link = lxcbr0\n");
        fprintf(file, "lxc.net.0.flags = up\n");
        fprintf(file, "lxc.net.0.hwaddr = 00:16:3e:xx:xx:xx\n");
    }
    
    // Mount points
    if (config->mount_points) {
        char* mounts = strdup(config->mount_points);
        char* mount = strtok(mounts, ",");
        while (mount) {
            fprintf(file, "lxc.mount.entry = %s\n", mount);
            mount = strtok(NULL, ",");
        }
        free(mounts);
    }
    
    fclose(file);
    return 0;
}

// Create LXC container
int lxc_create_container(const lxc_config_t* config) {
    if (!config || strlen(config->name) == 0) {
        printf("Error: Invalid container configuration\n");
        return -1;
    }
    
    char command[MAX_COMMAND_LEN];
    char output[MAX_LOG_LEN];
    
    // Check if container already exists
    if (lxc_container_exists(config->name)) {
        printf("Container %s already exists\n", config->name);
        return 0;
    }
    
    // Create container with specified image
    if (strlen(config->image) > 0) {
        snprintf(command, sizeof(command), 
                "lxc launch %s %s", config->image, config->name);
    } else {
        snprintf(command, sizeof(command), 
                "lxc launch ubuntu:20.04 %s", config->name);
    }
    
    printf("Creating container: %s\n", command);
    int result = execute_command(command, output, sizeof(output));
    
    if (result != 0) {
        printf("Error creating container %s: %s\n", config->name, output);
        return -1;
    }
    
    // Stop the container (it starts automatically)
    snprintf(command, sizeof(command), "lxc stop %s", config->name);
    execute_command(command, NULL, 0);
    
    // Apply custom configuration if provided
    if (strlen(config->config_file) > 0) {
        char config_path[MAX_PATH_LEN];
        snprintf(config_path, sizeof(config_path), 
                "/var/lib/lxc/%s/config", config->name);
        
        if (generate_lxc_config_file(config, config_path) != 0) {
            printf("Warning: Failed to apply custom configuration\n");
        }
    }
    
    // Set environment variables
    if (config->environment_vars) {
        char* env_vars = strdup(config->environment_vars);
        char* env_var = strtok(env_vars, ",");
        while (env_var) {
            char* equals = strchr(env_var, '=');
            if (equals) {
                *equals = '\0';
                char* key = env_var;
                char* value = equals + 1;
                
                snprintf(command, sizeof(command), 
                        "lxc config set %s environment.%s %s", 
                        config->name, key, value);
                execute_command(command, NULL, 0);
            }
            env_var = strtok(NULL, ",");
        }
        free(env_vars);
    }
    
    printf("Container %s created successfully\n", config->name);
    return 0;
}

// Start LXC container
int lxc_start_container(const char* name) {
    if (!name) return -1;
    
    char command[MAX_COMMAND_LEN];
    char output[MAX_LOG_LEN];
    
    if (!lxc_container_exists(name)) {
        printf("Error: Container %s does not exist\n", name);
        return -1;
    }
    
    snprintf(command, sizeof(command), "lxc start %s", name);
    printf("Starting container: %s\n", name);
    
    int result = execute_command(command, output, sizeof(output));
    if (result != 0) {
        printf("Error starting container %s: %s\n", name, output);
        return -1;
    }
    
    printf("Container %s started successfully\n", name);
    return 0;
}

// Stop LXC container
int lxc_stop_container(const char* name) {
    if (!name) return -1;
    
    char command[MAX_COMMAND_LEN];
    char output[MAX_LOG_LEN];
    
    if (!lxc_container_exists(name)) {
        printf("Error: Container %s does not exist\n", name);
        return -1;
    }
    
    snprintf(command, sizeof(command), "lxc stop %s", name);
    printf("Stopping container: %s\n", name);
    
    int result = execute_command(command, output, sizeof(output));
    if (result != 0) {
        printf("Error stopping container %s: %s\n", name, output);
        return -1;
    }
    
    printf("Container %s stopped successfully\n", name);
    return 0;
}

// Destroy LXC container
int lxc_destroy_container(const char* name) {
    if (!name) return -1;
    
    char command[MAX_COMMAND_LEN];
    char output[MAX_LOG_LEN];
    
    if (!lxc_container_exists(name)) {
        printf("Container %s does not exist\n", name);
        return 0;
    }
    
    // Stop container first if running
    lxc_stop_container(name);
    
    snprintf(command, sizeof(command), "lxc delete %s", name);
    printf("Destroying container: %s\n", name);
    
    int result = execute_command(command, output, sizeof(output));
    if (result != 0) {
        printf("Error destroying container %s: %s\n", name, output);
        return -1;
    }
    
    printf("Container %s destroyed successfully\n", name);
    return 0;
}

// Get container state
container_state_t lxc_get_container_state(const char* name) {
    if (!name) return CONTAINER_ERROR;
    
    char command[MAX_COMMAND_LEN];
    char output[MAX_LOG_LEN];
    
    if (!lxc_container_exists(name)) {
        return CONTAINER_ERROR;
    }
    
    snprintf(command, sizeof(command), "lxc list %s --format csv -c s", name);
    
    if (execute_command(command, output, sizeof(output)) != 0) {
        return CONTAINER_ERROR;
    }
    
    // Parse output to determine state
    if (strstr(output, "RUNNING")) {
        return CONTAINER_RUNNING;
    } else if (strstr(output, "STOPPED")) {
        return CONTAINER_STOPPED;
    } else if (strstr(output, "STARTING")) {
        return CONTAINER_STARTING;
    } else if (strstr(output, "STOPPING")) {
        return CONTAINER_STOPPING;
    }
    
    return CONTAINER_ERROR;
}

// Get system resource information
int get_system_resources(resource_info_t* resources) {
    if (!resources) return -1;
    
    char command[MAX_COMMAND_LEN];
    char output[MAX_LOG_LEN];
    
    // Get CPU usage
    snprintf(command, sizeof(command), 
            "top -bn1 | grep 'Cpu(s)' | awk '{print $2}' | cut -d'%%' -f1");
    if (execute_command(command, output, sizeof(output)) == 0) {
        resources->cpu_usage = atof(output);
    }
    
    // Get memory usage
    snprintf(command, sizeof(command), 
            "free | grep Mem | awk '{printf \"%.1f\", $3/$2 * 100.0}'");
    if (execute_command(command, output, sizeof(output)) == 0) {
        resources->memory_usage = atof(output);
    }
    
    // Get disk usage
    snprintf(command, sizeof(command), 
            "df / | tail -1 | awk '{print $5}' | cut -d'%%' -f1");
    if (execute_command(command, output, sizeof(output)) == 0) {
        resources->disk_usage = atof(output);
    }
    
    // Get container count
    snprintf(command, sizeof(command), "lxc list --format csv | wc -l");
    if (execute_command(command, output, sizeof(output)) == 0) {
        resources->container_count = atoi(output);
    }
    
    // Set max containers (configurable)
    resources->max_containers = 50;
    
    return 0;
}

// Monitor container and get logs
int monitor_container(const char* name, char* log_buffer, size_t buffer_size) {
    if (!name || !log_buffer) return -1;
    
    char command[MAX_COMMAND_LEN];
    
    if (!lxc_container_exists(name)) {
        snprintf(log_buffer, buffer_size, "Container %s does not exist", name);
        return -1;
    }
    
    snprintf(command, sizeof(command), "lxc info %s", name);
    
    if (execute_command(command, log_buffer, buffer_size) != 0) {
        snprintf(log_buffer, buffer_size, "Failed to get container info");
        return -1;
    }
    
    return 0;
}