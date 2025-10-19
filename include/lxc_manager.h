#ifndef LXC_MANAGER_H
#define LXC_MANAGER_H

#include "distributed_lxc.h"

// LXC management functions
int lxc_create_container(const lxc_config_t* config);
int lxc_start_container(const char* name);
int lxc_stop_container(const char* name);
int lxc_destroy_container(const char* name);
container_state_t lxc_get_container_state(const char* name);
int lxc_container_exists(const char* name);
int generate_lxc_config_file(const lxc_config_t* config, const char* output_path);
int get_system_resources(resource_info_t* resources);
int monitor_container(const char* name, char* log_buffer, size_t buffer_size);

#endif // LXC_MANAGER_H