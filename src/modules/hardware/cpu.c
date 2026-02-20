#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_cpu() {
    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");

    if (cpuinfo == NULL) {
        cupid_log(LogType_ERROR, "Failed to open /proc/cpuinfo");
        return;
    }

    char line[256];
    char model_name[160] = "";
    int num_cores = 0;
    int siblings = 0;
    int logical_threads = 0;

    while (fgets(line, sizeof(line), cpuinfo)) {
        if (cf_starts_with(line, "processor")) {
            logical_threads++;
            continue;
        }

        char *sep = strchr(line, ':');
        if (!sep) continue;
        *sep = '\0';

        char *key = cf_trim_spaces(line);
        char *value = cf_trim_spaces(sep + 1);
        if (!key || !value || value[0] == '\0') continue;

        if (strcmp(key, "model name") == 0) {
            if (model_name[0] == '\0') {
                strncpy(model_name, value, sizeof(model_name));
                model_name[sizeof(model_name) - 1] = '\0';
            }
        } else if (strcmp(key, "cpu cores") == 0) {
            int cores = atoi(value);
            if (cores > num_cores) num_cores = cores;
        } else if (strcmp(key, "siblings") == 0) {
            int threads = atoi(value);
            if (threads > siblings) siblings = threads;
        }
    }

    fclose(cpuinfo);

    if (logical_threads <= 0 && siblings > 0) {
        logical_threads = siblings;
    }
    if (num_cores <= 0 && logical_threads > 0) {
        num_cores = logical_threads;
    }

    double cpu_usage = 0.0;
    bool has_usage = cf_detect_cpu_usage_percent(&cpu_usage);

    if (model_name[0] != '\0' && num_cores > 0 && logical_threads > 0) {
        if (has_usage) {
            print_info("CPU", "%s (%dC/%dT, %.1f%%)", 20, 30, model_name, num_cores, logical_threads, cpu_usage);
        } else {
            print_info("CPU", "%s (%dC/%dT)", 20, 30, model_name, num_cores, logical_threads);
        }
    } else if (model_name[0] != '\0') {
        if (has_usage) {
            print_info("CPU", "%s (%.1f%%)", 20, 30, model_name, cpu_usage);
        } else {
            print_info("CPU", "%s", 20, 30, model_name);
        }
    } else {
        cupid_log(LogType_ERROR, "Failed to retrieve CPU information");
    }
}
