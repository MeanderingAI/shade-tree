#include "../include/yaml_parser.h"
#include <ctype.h>

// Parse a YAML line and extract key-value pair with indentation level
int parse_yaml_line(const char* line, char* key, char* value, int* indent) {
    if (!line || !key || !value || !indent) return -1;
    
    *indent = 0;
    key[0] = '\0';
    value[0] = '\0';
    
    // Skip empty lines and comments
    if (line[0] == '\0' || line[0] == '#') return 0;
    
    // Count indentation
    while (line[*indent] == ' ' || line[*indent] == '\t') {
        (*indent)++;
    }
    
    const char* start = line + *indent;
    const char* colon = strchr(start, ':');
    
    if (!colon) return 0; // Not a key-value pair
    
    // Extract key
    int key_len = colon - start;
    strncpy(key, start, key_len);
    key[key_len] = '\0';
    
    // Remove trailing whitespace from key
    while (key_len > 0 && isspace(key[key_len - 1])) {
        key[--key_len] = '\0';
    }
    
    // Extract value
    const char* value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t') {
        value_start++;
    }
    
    if (*value_start != '\0') {
        strcpy(value, value_start);
        // Remove trailing newline
        int value_len = strlen(value);
        if (value_len > 0 && value[value_len - 1] == '\n') {
            value[value_len - 1] = '\0';
        }
    }
    
    return 1;
}

// Create a new YAML node
yaml_node_t* create_yaml_node(const char* key, const char* value) {
    yaml_node_t* node = malloc(sizeof(yaml_node_t));
    if (!node) return NULL;
    
    strcpy(node->key, key);
    strcpy(node->value, value);
    node->next = NULL;
    node->child = NULL;
    
    return node;
}

// Parse YAML file into a tree structure
int parse_yaml_file(const char* filename, yaml_node_t** root) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Cannot open file %s\n", filename);
        return -1;
    }
    
    char line[MAX_COMMAND_LEN];
    char key[MAX_NAME_LEN];
    char value[MAX_COMMAND_LEN];
    int indent;
    
    yaml_node_t* current_parent = NULL;
    yaml_node_t* last_node = NULL;
    int last_indent = -1;
    
    *root = NULL;
    
    while (fgets(line, sizeof(line), file)) {
        if (parse_yaml_line(line, key, value, &indent) <= 0) {
            continue;
        }
        
        yaml_node_t* new_node = create_yaml_node(key, value);
        if (!new_node) {
            fclose(file);
            return -1;
        }
        
        if (*root == NULL) {
            // First node becomes root
            *root = new_node;
            current_parent = new_node;
            last_node = new_node;
            last_indent = indent;
        } else if (indent > last_indent) {
            // Child node
            if (last_node) {
                last_node->child = new_node;
                current_parent = last_node;
            }
        } else if (indent == last_indent) {
            // Sibling node
            if (last_node) {
                last_node->next = new_node;
            }
        } else {
            // Parent level or higher
            // Find appropriate parent
            yaml_node_t* parent = *root;
            if (indent == 0) {
                // Top level - add as sibling to root
                while (parent->next) {
                    parent = parent->next;
                }
                parent->next = new_node;
            } else {
                // Find parent at correct level
                // For simplicity, add as sibling to last node
                if (last_node) {
                    last_node->next = new_node;
                }
            }
        }
        
        last_node = new_node;
        last_indent = indent;
    }
    
    fclose(file);
    return 0;
}

// Get value for a specific key from YAML tree
char* get_yaml_value(yaml_node_t* root, const char* key) {
    if (!root || !key) return NULL;
    
    yaml_node_t* current = root;
    
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return current->value;
        }
        
        // Search in children
        char* result = get_yaml_value(current->child, key);
        if (result) return result;
        
        current = current->next;
    }
    
    return NULL;
}

// Extract LXC configuration from YAML tree
int extract_lxc_config(yaml_node_t* root, lxc_config_t* config) {
    if (!root || !config) return -1;
    
    // Initialize config
    memset(config, 0, sizeof(lxc_config_t));
    
    char* name = get_yaml_value(root, "name");
    if (name) {
        strncpy(config->name, name, MAX_NAME_LEN - 1);
    }
    
    char* image = get_yaml_value(root, "image");
    if (image) {
        strncpy(config->image, image, MAX_NAME_LEN - 1);
    }
    
    char* config_file = get_yaml_value(root, "config");
    if (config_file) {
        strncpy(config->config_file, config_file, MAX_PATH_LEN - 1);
    }
    
    char* cpu_limit_str = get_yaml_value(root, "cpu_limit");
    if (cpu_limit_str) {
        config->cpu_limit = atoi(cpu_limit_str);
    }
    
    char* memory_limit_str = get_yaml_value(root, "memory_limit");
    if (memory_limit_str) {
        config->memory_limit = atoi(memory_limit_str);
    }
    
    char* privileged_str = get_yaml_value(root, "privileged");
    if (privileged_str) {
        config->privileged = (strcmp(privileged_str, "true") == 0) ? 1 : 0;
    }
    
    // Allocate and copy environment variables
    char* env_vars = get_yaml_value(root, "environment");
    if (env_vars) {
        config->environment_vars = malloc(strlen(env_vars) + 1);
        if (config->environment_vars) {
            strcpy(config->environment_vars, env_vars);
        }
    }
    
    // Allocate and copy mount points
    char* mounts = get_yaml_value(root, "mounts");
    if (mounts) {
        config->mount_points = malloc(strlen(mounts) + 1);
        if (config->mount_points) {
            strcpy(config->mount_points, mounts);
        }
    }
    
    // Allocate and copy network configuration
    char* network = get_yaml_value(root, "network");
    if (network) {
        config->network_config = malloc(strlen(network) + 1);
        if (config->network_config) {
            strcpy(config->network_config, network);
        }
    }
    
    return 0;
}

// Free YAML tree memory
void free_yaml_tree(yaml_node_t* root) {
    if (!root) return;
    
    free_yaml_tree(root->child);
    free_yaml_tree(root->next);
    
    free(root);
}

// Main YAML parsing function
int parse_lxc_yaml(const char* yaml_file, lxc_config_t* config) {
    yaml_node_t* root = NULL;
    
    if (parse_yaml_file(yaml_file, &root) != 0) {
        printf("Error: Failed to parse YAML file %s\n", yaml_file);
        return -1;
    }
    
    if (extract_lxc_config(root, config) != 0) {
        printf("Error: Failed to extract LXC configuration from YAML\n");
        free_yaml_tree(root);
        return -1;
    }
    
    free_yaml_tree(root);
    return 0;
}