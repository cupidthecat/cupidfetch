#include <sys/statvfs.h>
#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_available_storage() {
    FILE* mount_file = fopen("/proc/mounts", "r");
    if (mount_file == NULL) {
        cupid_log(LogType_ERROR, "couldn't open /proc/mounts");
        return;
    }

    bool first = true;
    char device[256], mnt_point[256], fs_type[256];
    while (fscanf(mount_file, "%255s %255s %255s %*s %*d %*d", device, mnt_point, fs_type) == 3) {
        if (cf_should_skip_storage_mount(device, mnt_point, fs_type)) {
            continue;
        }

        struct statvfs stat;

        if (statvfs(mnt_point, &stat) != 0) {
            cupid_log(LogType_INFO, "nothing for %s", mnt_point);
            continue;
        }

        unsigned long long total_bytes = (unsigned long long)stat.f_blocks * (unsigned long long)stat.f_frsize;
        unsigned long long available_bytes = (unsigned long long)stat.f_bavail * (unsigned long long)stat.f_frsize;

        unsigned long total = cf_convert_bytes_to_unit(total_bytes, g_userConfig.storage_unit_size);
        unsigned long available = cf_convert_bytes_to_unit(available_bytes, g_userConfig.storage_unit_size);
        unsigned long used = (total > available) ? (total - available) : 0;
        unsigned long usage_percent = total > 0 ? (used * 100UL) / total : 0;

        if (total == 0) continue;

        print_info(
            first ? "Storage" : "",
            "%s: %lu/%lu %s (%lu%%)",
            20, 30,
            mnt_point, used, total, g_userConfig.storage_unit, usage_percent
        );
        first = false;
    }
    fclose(mount_file);
}
