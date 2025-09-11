// File: config.c
#include "cupidfetch.h"
#include "cupidconf.h"  // For configuration parsing
#include <stdlib.h>
#include <string.h>

// Global configuration variable.
struct CupidConfig g_userConfig;

// Mapping of module names to their functions.
struct module {
    char *s;
    void (*m)();
};
struct module string_to_module[] = {
    {"hostname", get_hostname},
    {"username", get_username},
    {"distro", get_distro},
    {"linux_kernel", get_linux_kernel},
    {"kernel", get_linux_kernel},
    {"uptime", get_uptime},
    {"pkg", get_package_count},
    {"term", get_terminal},
    {"shell", get_shell},
    {"ip", get_local_ip},
    {"memory", get_available_memory},
    {"storage", get_available_storage},
    {"cpu", get_cpu},
    {"wm", get_window_manager},
    {"session", get_session_type},
};

void init_g_config() {
    // Set up the default configuration.
    struct CupidConfig cfg_ = {
        .modules = { get_hostname, get_username, get_distro, get_linux_kernel,
                     get_uptime, get_package_count, get_terminal, get_shell,
                     get_available_storage, get_window_manager, get_session_type,
                     NULL },
        .memory_unit = "MB",
        .memory_unit_size = 1000000,
        .storage_unit = "GB",
        .storage_unit_size = 1000000000,
    };
    g_userConfig = cfg_;
}

void load_config_file(const char* config_path, struct CupidConfig *config) {
    cupidconf_t *conf = cupidconf_load(config_path);
    if (!conf) {
        cupid_log(LogType_WARNING, "Failed to load config file: %s", config_path);
        return;
    }

    /* --- Load the list of modules --- */
    const char* modules_str = cupidconf_get(conf, "modules");
    if (modules_str) {
        char buffer[1024];
        strncpy(buffer, modules_str, sizeof(buffer));
        buffer[sizeof(buffer)-1] = '\0';

        char *token = strtok(buffer, " ");
        size_t mi = 0;
        while (token) {
            for (size_t i = 0; i < sizeof(string_to_module)/sizeof(string_to_module[0]); i++) {
                if (strcmp(token, string_to_module[i].s) == 0) {
                    if (mi < MAX_NUM_MODULES) {
                        config->modules[mi] = string_to_module[i].m;
                        mi++;
                    }
                    break;
                }
            }
            token = strtok(NULL, " ");
        }
        config->modules[mi] = NULL;
    }

    /* --- Load memory settings --- */
    const char *mem_unit = cupidconf_get(conf, "memory.unit-str");
    if (mem_unit) {
        strncpy(config->memory_unit, mem_unit, MEMORY_UNIT_LEN);
        config->memory_unit[MEMORY_UNIT_LEN - 1] = '\0';
    }
    const char *mem_unit_size_str = cupidconf_get(conf, "memory.unit-size");
    if (mem_unit_size_str) {
        config->memory_unit_size = atol(mem_unit_size_str);
    }

    /* --- Load storage settings --- */
    const char *stor_unit = cupidconf_get(conf, "storage.unit-str");
    if (stor_unit) {
        strncpy(config->storage_unit, stor_unit, MEMORY_UNIT_LEN);
        config->storage_unit[MEMORY_UNIT_LEN - 1] = '\0';
    }
    const char *stor_unit_size_str = cupidconf_get(conf, "storage.unit-size");
    if (stor_unit_size_str) {
        config->storage_unit_size = atol(stor_unit_size_str);
    }

    cupidconf_free(conf);
}
