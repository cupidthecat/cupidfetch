#include <limits.h>
#ifndef _WIN32
#include <sys/statvfs.h>
#endif
#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_available_storage() {
#ifdef _WIN32
    char drives[512];
    DWORD len = GetLogicalDriveStringsA((DWORD)sizeof(drives), drives);
    if (len == 0 || len >= sizeof(drives)) {
        cupid_log(LogType_ERROR, "couldn't enumerate logical drives");
        return;
    }

    bool first = true;
    for (char *drive = drives; *drive; drive += strlen(drive) + 1) {
        ULARGE_INTEGER free_bytes_available;
        ULARGE_INTEGER total_bytes;
        ULARGE_INTEGER total_free_bytes;
        if (!GetDiskFreeSpaceExA(drive, &free_bytes_available, &total_bytes, &total_free_bytes)) {
            continue;
        }

        char usage[256];
        if (!cf_format_storage_usage(total_bytes.QuadPart, total_free_bytes.QuadPart,
                                     g_userConfig.storage_unit_size, g_userConfig.storage_unit,
                                     usage, sizeof(usage))) continue;
        print_info(first ? "Storage" : "", "%s: %s", 20, 30, drive, usage);
        first = false;
    }
    return;
#else
    FILE* mount_file = fopen("/proc/mounts", "r");
    if (mount_file == NULL) {
        cupid_log(LogType_ERROR, "couldn't open /proc/mounts");
        return;
    }

    bool first = true;
    const char *device, *mnt_point, *fs_type;
    while (cf_read_storage_mount(mount_file, &device, &mnt_point, &fs_type)) {
        if (cf_should_skip_storage_mount(device, mnt_point, fs_type)) {
            continue;
        }

        struct statvfs stat;

        if (statvfs(mnt_point, &stat) != 0) {
            cupid_log(LogType_INFO, "nothing for %s", mnt_point);
            continue;
        }

        if (stat.f_frsize == 0 ||
            (unsigned long long)stat.f_blocks > ULLONG_MAX / stat.f_frsize ||
            (unsigned long long)stat.f_bavail > ULLONG_MAX / stat.f_frsize) continue;
        unsigned long long total_bytes = (unsigned long long)stat.f_blocks * stat.f_frsize;
        unsigned long long available_bytes = (unsigned long long)stat.f_bavail * stat.f_frsize;
        char usage[256];
        if (!cf_format_storage_usage(total_bytes, available_bytes,
                                     g_userConfig.storage_unit_size, g_userConfig.storage_unit,
                                     usage, sizeof(usage))) continue;
        print_info(first ? "Storage" : "", "%s: %s", 20, 30, mnt_point, usage);
        first = false;
    }
    fclose(mount_file);
#endif
}
