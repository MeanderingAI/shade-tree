#ifndef YAML_PARSER_H
#define YAML_PARSER_H

#include "distributed_lxc.h"

// YAML parsing structures
typedef struct yaml_node {
    char key[MAX_NAME_LEN];
    char value[MAX_COMMAND_LEN];
    struct yaml_node* next;
    struct yaml_node* child;
} yaml_node_t;

// Function prototypes for YAML parsing
int parse_yaml_file(const char* filename, yaml_node_t** root);
int extract_lxc_config(yaml_node_t* root, lxc_config_t* config);
void free_yaml_tree(yaml_node_t* root);
char* get_yaml_value(yaml_node_t* root, const char* key);
int parse_yaml_line(const char* line, char* key, char* value, int* indent);

#endif // YAML_PARSER_H