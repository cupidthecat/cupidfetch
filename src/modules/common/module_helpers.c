#include "module_helpers.h"

static bool process_matches(const char *pid, const char *needle) {
    char path[64];
    char comm[128];

    snprintf(path, sizeof(path), "/proc/%s/comm", pid);
    FILE *comm_file = fopen(path, "r");
    if (comm_file) {
        if (fgets(comm, sizeof(comm), comm_file)) {
            cf_trim_newline(comm);
            fclose(comm_file);
            if (cf_contains_icase(comm, needle)) return true;
        } else {
            fclose(comm_file);
        }
    }

    snprintf(path, sizeof(path), "/proc/%s/cmdline", pid);
    FILE *cmdline_file = fopen(path, "r");
    if (!cmdline_file) return false;

    char cmdline[512];
    size_t nread = fread(cmdline, 1, sizeof(cmdline) - 1, cmdline_file);
    fclose(cmdline_file);
    cmdline[nread] = '\0';

    return cf_contains_icase(cmdline, needle);
}

void cf_trim_newline(char *str) {
    str[strcspn(str, "\r\n")] = '\0';
}

bool cf_contains_icase(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return false;

    size_t needle_len = strlen(needle);
    for (size_t i = 0; haystack[i]; i++) {
        size_t j = 0;
        while (j < needle_len && haystack[i + j] &&
               tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == needle_len) return true;
    }
    return false;
}

const char *cf_detect_process_label(const struct process_match *candidates, size_t num_candidates) {
    DIR *dir = opendir("/proc");
    if (!dir) return NULL;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;

        const char *pid = entry->d_name;
        if (!isdigit((unsigned char)pid[0])) continue;

        for (size_t i = 0; i < num_candidates; i++) {
            if (process_matches(pid, candidates[i].proc_name)) {
                closedir(dir);
                return candidates[i].label;
            }
        }
    }

    closedir(dir);
    return NULL;
}

const char *cf_basename_or_self(const char *path) {
    if (!path || !path[0]) return NULL;
    const char *base = strrchr(path, '/');
    return base ? (base + 1) : path;
}

bool cf_read_first_line(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "r");
    if (!file) return false;

    if (!fgets(buffer, size, file)) {
        fclose(file);
        return false;
    }
    fclose(file);
    cf_trim_newline(buffer);
    return true;
}

bool cf_read_ulong_file(const char *path, unsigned long *value) {
    char line[64];
    if (!cf_read_first_line(path, line, sizeof(line))) return false;

    char *endptr = NULL;
    errno = 0;
    unsigned long parsed = strtoul(line, &endptr, 10);
    if (errno != 0 || endptr == line) return false;

    *value = parsed;
    return true;
}

bool cf_starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return false;
    size_t prefix_len = strlen(prefix);
    return strncmp(str, prefix, prefix_len) == 0;
}

char *cf_trim_spaces(char *str) {
    if (!str) return str;

    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return str;
}

bool cf_executable_in_path(const char *name) {
    if (!name || !name[0]) return false;

    const char *path_env = getenv("PATH");
    if (!path_env || !path_env[0]) return false;

    char path_copy[4096];
    strncpy(path_copy, path_env, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token = strtok(path_copy, ":");
    while (token) {
        char full[4352];
        if (snprintf(full, sizeof(full), "%s/%s", token, name) > 0) {
            if (access(full, X_OK) == 0) return true;
        }
        token = strtok(NULL, ":");
    }

    return false;
}

bool cf_run_command_first_line(const char *command, char *out, size_t out_size) {
    if (!command || !command[0] || !out || out_size == 0) return false;

    FILE *fp = popen(command, "r");
    if (!fp) return false;

    bool ok = fgets(out, out_size, fp) != NULL;
    pclose(fp);
    if (!ok) return false;

    cf_trim_newline(out);
    char *trimmed = cf_trim_spaces(out);
    if (trimmed != out) {
        memmove(out, trimmed, strlen(trimmed) + 1);
    }
    return out[0] != '\0';
}

static bool read_cpu_times(unsigned long long *idle_all, unsigned long long *total_all) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return false;

    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return false;
    }
    fclose(fp);

    if (!cf_starts_with(line, "cpu ")) return false;

    unsigned long long user = 0, nice = 0, system = 0, idle = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    unsigned long long guest = 0, guest_nice = 0;

    int fields = sscanf(
        line,
        "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
        &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice
    );
    if (fields < 4) return false;

    *idle_all = idle + iowait;
    *total_all = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
    return true;
}

bool cf_detect_cpu_usage_percent(double *usage_out) {
    unsigned long long idle1 = 0, total1 = 0;
    unsigned long long idle2 = 0, total2 = 0;

    if (!read_cpu_times(&idle1, &total1)) return false;
    usleep(120000);
    if (!read_cpu_times(&idle2, &total2)) return false;

    if (total2 <= total1) return false;

    unsigned long long total_delta = total2 - total1;
    unsigned long long idle_delta = (idle2 >= idle1) ? (idle2 - idle1) : 0;
    if (total_delta == 0) return false;

    double usage = (double)(total_delta - idle_delta) * 100.0 / (double)total_delta;
    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;

    *usage_out = usage;
    return true;
}

bool cf_build_power_supply_path(
    char *dest,
    size_t dest_size,
    const char *entry_name,
    const char *suffix
) {
    const char prefix[] = "/sys/class/power_supply/";
    size_t prefix_len = sizeof(prefix) - 1;
    size_t entry_len = strlen(entry_name);
    size_t suffix_len = strlen(suffix);

    if (prefix_len + entry_len + suffix_len + 1 > dest_size) {
        return false;
    }

    memcpy(dest, prefix, prefix_len);
    memcpy(dest + prefix_len, entry_name, entry_len);
    memcpy(dest + prefix_len + entry_len, suffix, suffix_len);
    dest[prefix_len + entry_len + suffix_len] = '\0';
    return true;
}

void cf_format_duration_compact(unsigned long seconds, char *buffer, size_t size) {
    unsigned long rounded_minutes = (seconds + 59UL) / 60UL;
    if (rounded_minutes == 0) {
        snprintf(buffer, size, "<1m");
        return;
    }

    unsigned long hours = rounded_minutes / 60UL;
    unsigned long minutes = rounded_minutes % 60UL;

    if (hours > 0) {
        snprintf(buffer, size, "%luh %lum", hours, minutes);
    } else {
        snprintf(buffer, size, "%lum", rounded_minutes);
    }
}

bool cf_is_drm_card_device(const char *name) {
    if (strncmp(name, "card", 4) != 0) return false;
    if (strchr(name, '-') != NULL) return false;

    const char *suffix = name + 4;
    if (*suffix == '\0') return false;

    for (const char *p = suffix; *p; p++) {
        if (!isdigit((unsigned char)*p)) return false;
    }
    return true;
}

bool cf_build_path3(
    char *dest,
    size_t dest_size,
    const char *prefix,
    const char *middle,
    const char *suffix
) {
    size_t prefix_len = strlen(prefix);
    size_t middle_len = strlen(middle);
    size_t suffix_len = strlen(suffix);

    if (prefix_len + middle_len + suffix_len + 1 > dest_size) {
        return false;
    }

    memcpy(dest, prefix, prefix_len);
    memcpy(dest + prefix_len, middle, middle_len);
    memcpy(dest + prefix_len + middle_len, suffix, suffix_len);
    dest[prefix_len + middle_len + suffix_len] = '\0';
    return true;
}

const char *cf_gpu_vendor_name(const char *vendor_id) {
    if (strcmp(vendor_id, "0x10de") == 0) return "NVIDIA";
    if (strcmp(vendor_id, "0x8086") == 0) return "Intel";
    if (strcmp(vendor_id, "0x1002") == 0) return "AMD";
    if (strcmp(vendor_id, "0x1022") == 0) return "AMD";
    if (strcmp(vendor_id, "0x13b5") == 0) return "ARM";
    if (strcmp(vendor_id, "0x5143") == 0) return "Qualcomm";
    if (strcmp(vendor_id, "0x1414") == 0) return "Microsoft";
    return NULL;
}

void cf_append_csv_item(char *dest, size_t dest_size, const char *item) {
    if (!item || !item[0]) return;

    size_t dest_len = strlen(dest);
    size_t item_len = strlen(item);

    if (dest_len >= dest_size - 1) return;

    if (dest_len > 0) {
        if (dest_len + 2 >= dest_size) return;
        dest[dest_len++] = ',';
        dest[dest_len++] = ' ';
        dest[dest_len] = '\0';
    }

    size_t max_copy = dest_size - 1 - dest_len;
    if (item_len > max_copy) item_len = max_copy;
    memcpy(dest + dest_len, item, item_len);
    dest[dest_len + item_len] = '\0';
}

bool cf_detect_gpu_from_lspci(char *gpu_out, size_t gpu_out_size) {
    FILE *fp = popen("lspci 2>/dev/null", "r");
    if (!fp) return false;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (!cf_contains_icase(line, "vga compatible controller") &&
            !cf_contains_icase(line, "3d controller") &&
            !cf_contains_icase(line, "display controller") &&
            !cf_contains_icase(line, "render driver")) {
            continue;
        }

        cf_trim_newline(line);
        char *desc = strstr(line, ": ");
        desc = desc ? (desc + 2) : line;

        strncpy(gpu_out, desc, gpu_out_size);
        gpu_out[gpu_out_size - 1] = '\0';
        pclose(fp);
        return true;
    }

    pclose(fp);
    return false;
}

bool cf_read_pci_slot_from_uevent(const char *drm_name, char *slot_out, size_t slot_out_size) {
    char path[512];
    if (!cf_build_path3(path, sizeof(path), "/sys/class/drm/", drm_name, "/device/uevent")) {
        return false;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "PCI_SLOT_NAME=", 14) == 0) {
            char *value = line + 14;
            cf_trim_newline(value);
            strncpy(slot_out, value, slot_out_size);
            slot_out[slot_out_size - 1] = '\0';
            found = slot_out[0] != '\0';
            break;
        }
    }

    fclose(fp);
    return found;
}

bool cf_detect_gpu_from_pci_slot(const char *pci_slot, char *gpu_out, size_t gpu_out_size) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "lspci -s %s 2>/dev/null", pci_slot);

    FILE *fp = popen(cmd, "r");
    if (!fp) return false;

    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        pclose(fp);
        return false;
    }
    pclose(fp);

    cf_trim_newline(line);
    char *desc = strstr(line, ": ");
    desc = desc ? (desc + 2) : line;

    strncpy(gpu_out, desc, gpu_out_size);
    gpu_out[gpu_out_size - 1] = '\0';
    return gpu_out[0] != '\0';
}

bool cf_detect_primary_ip(char *iface_out, size_t iface_out_size, char *ip_out, size_t ip_out_size, bool *is_up) {
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        return false;
    }

    bool found = false;
    bool found_up = false;
    char iface_buf[64] = "";
    char ip_buf[INET6_ADDRSTRLEN] = "";
    int found_family = 0;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;

        int family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;
        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) continue;

        char ip[INET6_ADDRSTRLEN] = "";
        if (family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            if (!inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip))) continue;
            if (strncmp(ip, "127.", 4) == 0) continue;
        } else {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            if (!inet_ntop(AF_INET6, &addr6->sin6_addr, ip, sizeof(ip))) continue;
            if (strcmp(ip, "::1") == 0) continue;
        }

        bool current_up = (ifa->ifa_flags & IFF_UP) != 0;
        bool better_candidate = false;
        if (!found) {
            better_candidate = true;
        } else if (!found_up && current_up) {
            better_candidate = true;
        } else if (found_family == AF_INET6 && family == AF_INET) {
            better_candidate = true;
        }

        if (better_candidate) {
            strncpy(iface_buf, ifa->ifa_name, sizeof(iface_buf));
            iface_buf[sizeof(iface_buf) - 1] = '\0';
            strncpy(ip_buf, ip, sizeof(ip_buf));
            ip_buf[sizeof(ip_buf) - 1] = '\0';
            found = true;
            found_up = current_up;
            found_family = family;

            if (found_up && found_family == AF_INET) break;
        }
    }

    freeifaddrs(ifaddr);

    if (!found) return false;

    strncpy(iface_out, iface_buf, iface_out_size);
    iface_out[iface_out_size - 1] = '\0';
    strncpy(ip_out, ip_buf, ip_out_size);
    ip_out[ip_out_size - 1] = '\0';
    *is_up = found_up;
    return true;
}

bool cf_get_public_ip(char *ip_out, size_t ip_out_size) {
    FILE *fp = popen(
        "sh -c \"if command -v curl >/dev/null 2>&1; then curl -fsS --max-time 2 https://api.ipify.org; "
        "elif command -v wget >/dev/null 2>&1; then wget -qO- --timeout=2 https://api.ipify.org; fi\" 2>/dev/null",
        "r"
    );
    if (!fp) return false;

    char buffer[128] = "";
    bool ok = fgets(buffer, sizeof(buffer), fp) != NULL;
    pclose(fp);

    if (!ok) return false;

    cf_trim_newline(buffer);
    if (buffer[0] == '\0') return false;

    strncpy(ip_out, buffer, ip_out_size);
    ip_out[ip_out_size - 1] = '\0';
    return true;
}

void cf_mask_public_ip(const char *ip_in, char *masked_out, size_t masked_out_size) {
    if (!ip_in || !ip_in[0]) {
        masked_out[0] = '\0';
        return;
    }

    if (strchr(ip_in, '.')) {
        unsigned int a, b, c, d;
        if (sscanf(ip_in, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 &&
            a <= 255 && b <= 255 && c <= 255 && d <= 255) {
            snprintf(masked_out, masked_out_size, "x.x.%u.%u", c, d);
            return;
        }
    }

    if (strchr(ip_in, ':')) {
        size_t len = strlen(ip_in);
        if (len > 8) {
            snprintf(masked_out, masked_out_size, "...%s", ip_in + (len - 8));
            return;
        }
    }

    snprintf(masked_out, masked_out_size, "hidden");
}

bool cf_should_skip_storage_mount(const char *device, const char *mnt_point, const char *fs_type) {
    static const char *ignored_fs[] = {
        "proc", "sysfs", "tmpfs", "devtmpfs", "devpts", "cgroup", "cgroup2",
        "pstore", "securityfs", "debugfs", "tracefs", "configfs", "overlay",
        "squashfs", "nsfs", "fusectl", "mqueue", "autofs", "ramfs"
    };

    for (size_t i = 0; i < sizeof(ignored_fs) / sizeof(ignored_fs[0]); i++) {
        if (strcmp(fs_type, ignored_fs[i]) == 0) return true;
    }

    if (strncmp(mnt_point, "/snap", 5) == 0 ||
        strncmp(mnt_point, "/var/lib/snapd/snap", 19) == 0) {
        return true;
    }

    if (strncmp(device, "/dev/loop", 9) == 0) {
        return true;
    }

    return false;
}
