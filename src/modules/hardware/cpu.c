#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#include <stddef.h>
#include <limits.h>
#include "../common/subprocess.h"
#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

#ifdef _WIN32
static unsigned int windows_core_count(void) {
    DWORD bytes = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bytes) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) return 0;
    unsigned char *buffer = malloc(bytes);
    if (!buffer) return 0;
    DWORD capacity = bytes;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer, &bytes) || bytes > capacity) {
        free(buffer);
        return 0;
    }
    DWORD offset = 0;
    unsigned int cores = 0;
    while (offset < bytes) {
        size_t header_size = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor);
        if (bytes - offset < header_size) { cores = 0; break; }
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX record;
        memcpy(&record, buffer + offset, header_size);
        if (record.Size <= header_size || record.Size > bytes - offset ||
            record.Relationship != RelationProcessorCore || cores == UINT_MAX) {
            cores = 0;
            break;
        }
        cores++;
        offset += record.Size;
    }
    free(buffer);
    return cores;
}
#endif

void get_cpu() {
#ifdef _WIN32
    char cpu_name[256] = "";
    unsigned int total_cores = 0;
    unsigned int total_threads = 0;

    FILE *fp = cf_capture_command("wmic cpu get Name /format:list 2>nul", CF_COMMAND_TIMEOUT_MS, CF_COMMAND_OUTPUT_LIMIT);
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

        }
        fclose(fp);
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
        FILE *reg_fp = cf_capture_command("reg query \"HKLM\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0\" /v ProcessorNameString 2>nul", CF_COMMAND_TIMEOUT_MS, CF_COMMAND_OUTPUT_LIMIT);
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
            fclose(reg_fp);
        }
    }

    total_cores = windows_core_count();
    total_threads = (unsigned int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (total_cores && total_threads && total_cores <= total_threads) {
        print_info("CPU", "%s (%uC/%uT)", 20, 30, cpu_name, total_cores, total_threads);
    } else if (total_threads) {
        print_info("CPU", "%s (%uT)", 20, 30, cpu_name, total_threads);
    } else if (total_cores) {
        print_info("CPU", "%s (%uC)", 20, 30, cpu_name, total_cores);
    } else {
        print_info("CPU", "%s", 20, 30, cpu_name);
    }
    return;
#else
    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");

    if (cpuinfo == NULL) {
        cupid_log(LogType_ERROR, "Failed to open /proc/cpuinfo");
        return;
    }

    char model_name[160] = "";
    int num_cores = 0;
    int logical_threads = 0;
    bool parsed = cf_parse_cpuinfo(cpuinfo, model_name, sizeof(model_name), &num_cores, &logical_threads);
    fclose(cpuinfo);
    if (!parsed) {
        cupid_log(LogType_ERROR, "Failed to read /proc/cpuinfo");
        return;
    }

    double cpu_usage = 0.0;
    bool has_usage = cf_detect_cpu_usage_percent(&cpu_usage);

    if (model_name[0] != '\0' && num_cores > 0 && logical_threads > 0) {
        if (has_usage) {
            print_info("CPU", "%s (%dC/%dT, %.1f%%)", 20, 30, model_name, num_cores, logical_threads, cpu_usage);
        } else {
            print_info("CPU", "%s (%dC/%dT)", 20, 30, model_name, num_cores, logical_threads);
        }
    } else if (model_name[0] != '\0' && logical_threads > 0) {
        if (has_usage) {
            print_info("CPU", "%s (%dT, %.1f%%)", 20, 30, model_name, logical_threads, cpu_usage);
        } else {
            print_info("CPU", "%s (%dT)", 20, 30, model_name, logical_threads);
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
