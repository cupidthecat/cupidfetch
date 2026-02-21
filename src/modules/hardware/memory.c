#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_available_memory() {
#ifdef _WIN32
    MEMORYSTATUSEX memory_status;
    memset(&memory_status, 0, sizeof(memory_status));
    memory_status.dwLength = sizeof(memory_status);
    if (!GlobalMemoryStatusEx(&memory_status)) {
        return;
    }

    unsigned long long mem_total_bytes = memory_status.ullTotalPhys;
    unsigned long long mem_avail_bytes = memory_status.ullAvailPhys;
    unsigned long long mem_used_bytes = (mem_total_bytes > mem_avail_bytes) ? (mem_total_bytes - mem_avail_bytes) : 0;

    print_info(
        "Memory", "%ld %s / %ld %s", 20, 30,
        (long)cf_convert_bytes_to_unit(mem_used_bytes, g_userConfig.memory_unit_size),
        g_userConfig.memory_unit,
        (long)cf_convert_bytes_to_unit(mem_total_bytes, g_userConfig.memory_unit_size),
        g_userConfig.memory_unit
    );
    return;
#else
    ssize_t mem_avail = -1, mem_total = -1;
    long mem_used = 0;
    FILE* meminfo;
    char line[LINUX_PROC_LINE_SZ];

    meminfo = fopen("/proc/meminfo", "r");

    if (meminfo == NULL) return;

    while (fgets(line, sizeof line, meminfo)) {
        char *value = NULL;
        size_t vnum;
        size_t i, len = 0, vlen = 0;

        for (i = 0; line[i]; i++) {
            if (!len && line[i] == ':')
                len = i;
            if (len && !value && isdigit(line[i]))
                value = &line[i];
            if (len && value && isdigit(line[i]))
                vlen = 1 + &line[i] - value;
        }
        if (!len || !vlen || !value)
            continue;

        line[len] = '\0';
        value[vlen] = '\0';

        if (1 != sscanf(value, "%zu", &vnum))
            continue;

        if (0 == strcmp("MemTotal", line)) {
            mem_total = vnum;
            mem_used += vnum;
        } else if (0 == strcmp("MemAvailable", line))
            mem_avail = vnum;
        else if (0 == strcmp("Shmem", line))
            mem_used += vnum;
        else if (0 == strcmp("MemFree", line))
            mem_used -= vnum;
        else if (0 == strcmp("Buffers", line))
            mem_used -= vnum;
        else if (0 == strcmp("Cached", line))
            mem_used -= vnum;
        else if (0 == strcmp("SReclaimable", line))
            mem_used -= vnum;

        if (mem_total != -1 && mem_avail != -1)
            break;
    }

    fclose(meminfo);

    if (meminfo == NULL) {
        cupid_log(LogType_ERROR, "Failed to open /proc/meminfo");
        return;
    }

    if (mem_avail != -1) {
        mem_used = mem_total - mem_avail;
    }

    print_info(
        "Memory", "%ld %s / %ld %s", 20, 30,
        (long)cf_convert_bytes_to_unit((unsigned long long)mem_used * 1024ULL, g_userConfig.memory_unit_size),
        g_userConfig.memory_unit,
        (long)cf_convert_bytes_to_unit((unsigned long long)mem_total * 1024ULL, g_userConfig.memory_unit_size),
        g_userConfig.memory_unit
    );
#endif
}
