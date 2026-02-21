#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_cpu() {
#ifdef _WIN32
    char cpu_name[256] = "";
    unsigned int total_cores = 0;
    unsigned int total_threads = 0;

    FILE *fp = popen("wmic cpu get Name,NumberOfCores,NumberOfLogicalProcessors /format:list 2>nul", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            cf_trim_newline(line);
            char *trimmed = cf_trim_spaces(line);
            if (!trimmed || !trimmed[0]) continue;

            if (strncmp(trimmed, "Name=", 5) == 0) {
                if (cpu_name[0] == '\0') {
                    strncpy(cpu_name, trimmed + 5, sizeof(cpu_name) - 1);
                    cpu_name[sizeof(cpu_name) - 1] = '\0';
                }
                continue;
            }

            if (strncmp(trimmed, "NumberOfCores=", 14) == 0) {
                unsigned int c = 0;
                if (sscanf(trimmed + 14, "%u", &c) == 1 && c > 0) {
                    total_cores += c;
                }
                continue;
            }

            if (strncmp(trimmed, "NumberOfLogicalProcessors=", 26) == 0) {
                unsigned int t = 0;
                if (sscanf(trimmed + 26, "%u", &t) == 1 && t > 0) {
                    total_threads += t;
                }
                continue;
            }
        }
        pclose(fp);
    }

    if (cpu_name[0] == '\0') {
        const char *fallback_name = getenv("PROCESSOR_IDENTIFIER");
        if (fallback_name && fallback_name[0]) {
            strncpy(cpu_name, fallback_name, sizeof(cpu_name) - 1);
            cpu_name[sizeof(cpu_name) - 1] = '\0';
        } else {
            strncpy(cpu_name, "Windows CPU", sizeof(cpu_name) - 1);
            cpu_name[sizeof(cpu_name) - 1] = '\0';
        }
    }

    if (cf_contains_icase(cpu_name, "Family") || cf_contains_icase(cpu_name, "GenuineIntel")) {
        FILE *reg_fp = popen("reg query \"HKLM\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0\" /v ProcessorNameString 2>nul", "r");
        if (reg_fp) {
            char reg_line[512];
            while (fgets(reg_line, sizeof(reg_line), reg_fp)) {
                cf_trim_newline(reg_line);
                char *trimmed = cf_trim_spaces(reg_line);
                if (!trimmed || !trimmed[0]) continue;
                if (!cf_contains_icase(trimmed, "ProcessorNameString")) continue;

                char *reg_sz = strstr(trimmed, "REG_SZ");
                if (!reg_sz) continue;
                reg_sz += 6;

                char *name = cf_trim_spaces(reg_sz);
                if (name && name[0]) {
                    strncpy(cpu_name, name, sizeof(cpu_name) - 1);
                    cpu_name[sizeof(cpu_name) - 1] = '\0';
                    break;
                }
            }
            pclose(reg_fp);
        }
    }

    if (total_threads == 0 || total_cores == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        if (total_threads == 0) total_threads = (unsigned int)si.dwNumberOfProcessors;
        if (total_cores == 0) total_cores = total_threads;
    }

    print_info("CPU", "%s (%uC/%uT)", 20, 30, cpu_name, total_cores, total_threads);
    return;
#else
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
#endif
}
