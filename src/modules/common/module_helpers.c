#include "subprocess.h"
#include "module_helpers.h"
#include <limits.h>
#ifndef _WIN32
#include <mntent.h>
#endif

#define CF_EXEC_CACHE_CAP 64

struct cf_exec_cache_entry {
    char name[64];
    bool exists;
};

static struct cf_exec_cache_entry g_exec_cache[CF_EXEC_CACHE_CAP];
static size_t g_exec_cache_count = 0;
static char g_exec_cache_path[4096] = "";
static bool g_exec_cache_path_set = false;

static void cf_exec_cache_reset(void) {
    g_exec_cache_count = 0;
    g_exec_cache_path[0] = '\0';
    g_exec_cache_path_set = false;
}

#ifdef _WIN32
static char g_exec_cache_pathext[4096] = "";

static const char *cf_path_extensions(void) {
    const char *extensions = getenv("PATHEXT");
    return extensions && extensions[0] ? extensions : ".COM;.EXE;.BAT;.CMD";
}

static bool cf_windows_file_exists(const char *path) {
    /* _access accepts 0, 2, 4 or 6; Windows has no X_OK mode. */
    struct _stat info;
    return _access(path, 0) == 0 && _stat(path, &info) == 0 &&
           (info.st_mode & _S_IFMT) == _S_IFREG;
}

static bool cf_windows_command_exists(char *full, size_t capacity) {
    size_t base_len = strlen(full);
    const char *extension = cf_path_extensions();
    while (*extension) {
        size_t len = strcspn(extension, ";");
        /* Ignore empty and malformed entries, including directory separators. */
        if (len > 1 && extension[0] == '.' &&
            !memchr(extension, '/', len) && !memchr(extension, '\\', len)) {
            if (base_len >= len &&
                strncasecmp(full + base_len - len, extension, len) == 0 &&
                cf_windows_file_exists(full)) return true;
            if (len < capacity - base_len) {
                memcpy(full + base_len, extension, len);
                full[base_len + len] = '\0';
                if (cf_windows_file_exists(full)) return true;
                full[base_len] = '\0';
            }
        }
        extension += len;
        if (*extension == ';') extension++;
    }
    return false;
}
#endif

static bool cf_scan_executable_in_path(const char *path_env, const char *name) {
    if (!path_env || !path_env[0] || !name || !name[0]) return false;

#ifdef _WIN32
    const char *separator = ";";
    const char slash = '\\';
#else
    const char *separator = ":";
    const char slash = '/';
#endif
    /* Walk the original PATH so entries after 4096 bytes are still searched. */
    const char *entry = path_env;
    while (*entry) {
        size_t entry_len = strcspn(entry, separator);
        const char *directory = entry;
        size_t directory_len = entry_len;
#ifdef _WIN32
        if (directory_len >= 2 && directory[0] == '"' &&
            directory[directory_len - 1] == '"') {
            directory++;
            directory_len -= 2;
        }
#endif
        char full[4352];
        size_t name_len = strlen(name);
        if (directory_len && directory_len < sizeof(full) - 1 &&
            name_len < sizeof(full) - directory_len - 1) {
            memcpy(full, directory, directory_len);
            full[directory_len] = slash;
            memcpy(full + directory_len + 1, name, name_len + 1);
#ifdef _WIN32
            if (cf_windows_command_exists(full, sizeof(full))) return true;
#else
            struct stat info;
            if (access(full, X_OK) == 0 && stat(full, &info) == 0 &&
                S_ISREG(info.st_mode)) return true;
#endif
        }
        entry += entry_len;
        if (*entry) entry++;
    }
    return false;
}

#ifndef _WIN32
static bool process_matches(const char *pid, const char *needle) {
    if (!needle || !needle[0]) return false;
    char path[64];
    char comm[128];
    snprintf(path, sizeof(path), "/proc/%s/comm", pid);
    if (cf_read_first_line(path, comm, sizeof(comm)) &&
        strcasecmp(comm, needle) == 0) return true;

    snprintf(path, sizeof(path), "/proc/%s/cmdline", pid);
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    char argv0[4096];
    size_t nread = fread(argv0, 1, sizeof(argv0), file);
    fclose(file);
    /* Only a complete argv[0] counts. Arguments and path prefixes do not. */
    if (!memchr(argv0, '\0', nread) || !argv0[0]) return false;
    return strcasecmp(cf_basename_or_self(argv0), needle) == 0;
}

static bool process_in_session(const char *pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%s/status", pid);
    FILE *file = fopen(path, "r");
    if (!file) return false;
    char line[256];
    bool owned = false;
    while (fgets(line, sizeof(line), file)) {
        unsigned long real, effective, saved, fs;
        if (sscanf(line, "Uid: %lu %lu %lu %lu", &real, &effective, &saved, &fs) == 4) {
            owned = effective == (unsigned long)geteuid();
            break;
        }
    }
    fclose(file);
    /* Proc directory ownership can become root for non-dumpable processes. */
    if (!owned) return false;

    static const char *keys[] = {"XDG_SESSION_ID", "DISPLAY", "WAYLAND_DISPLAY"};
    bool matched = false, conflict = false;
    snprintf(path, sizeof(path), "/proc/%s/environ", pid);
    file = fopen(path, "rb");
    if (file) {
        char entry[4096];
        size_t used = 0, total = 0;
        bool truncated = false;
        int ch;
        /* Read NUL-delimited entries, including variables after long entries.
         * Bound work for processes with unusually large environments. */
        while (total++ < 1024 * 1024 && (ch = fgetc(file)) != EOF) {
            if (ch != '\0') {
                if (used < sizeof(entry) - 1) entry[used++] = (char)ch;
                else truncated = true;
                continue;
            }
            entry[used] = '\0';
            for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
                size_t len = strlen(keys[i]);
                const char *current = getenv(keys[i]);
                if (current && current[0] && strncmp(entry, keys[i], len) == 0 &&
                    entry[len] == '=' && entry[len + 1]) {
                    if (truncated || strcmp(current, entry + len + 1) != 0) conflict = true;
                    else matched = true;
                }
            }
            used = 0;
            truncated = false;
        }
        /* Incomplete input cannot establish session membership. */
        if (!feof(file) || ferror(file) || used || truncated) conflict = true;
        fclose(file);
    }
    if (conflict) return false;
    if (matched) return true;

    /* Terminal shells often have their own POSIX session. Shared graphical or
     * login metadata above therefore takes precedence over this fallback. */
    pid_t current_sid = getsid(0);
    return current_sid >= 0 && getsid((pid_t)strtol(pid, NULL, 10)) == current_sid;
}
#endif

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
#ifdef _WIN32
    (void)candidates;
    (void)num_candidates;
    return NULL;
#else
    DIR *dir = opendir("/proc");
    if (!dir) return NULL;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;

        const char *pid = entry->d_name;
        if (!pid[0] || strspn(pid, "0123456789") != strlen(pid)) continue;

        for (size_t i = 0; i < num_candidates; i++) {
            if (process_matches(pid, candidates[i].proc_name) && process_in_session(pid)) {
                closedir(dir);
                return candidates[i].label;
            }
        }
    }

    closedir(dir);
    return NULL;
#endif
}

const char *cf_basename_or_self(const char *path) {
    if (!path || !path[0]) return NULL;
    const char *base = strrchr(path, '/');
#ifdef _WIN32
    const char *base_win = strrchr(path, '\\');
    if (!base || (base_win && base_win > base)) base = base_win;
#endif
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

    /* Oversized environment values are searched in full without caching. */
    bool cacheable = strlen(path_env) < sizeof(g_exec_cache_path);
    bool environment_changed = !g_exec_cache_path_set ||
                               strcmp(g_exec_cache_path, path_env) != 0;
#ifdef _WIN32
    const char *extensions = cf_path_extensions();
    cacheable = cacheable && strlen(extensions) < sizeof(g_exec_cache_pathext);
    environment_changed = environment_changed ||
                          strcmp(g_exec_cache_pathext, extensions) != 0;
#endif
    if (!cacheable) {
        cf_exec_cache_reset();
        return cf_scan_executable_in_path(path_env, name);
    }
    if (environment_changed) {
        cf_exec_cache_reset();
        strcpy(g_exec_cache_path, path_env);
#ifdef _WIN32
        strcpy(g_exec_cache_pathext, extensions);
#endif
        g_exec_cache_path_set = true;
    }

    for (size_t i = 0; i < g_exec_cache_count; i++) {
        if (strcmp(g_exec_cache[i].name, name) == 0) {
            return g_exec_cache[i].exists;
        }
    }

    bool exists = cf_scan_executable_in_path(path_env, name);

    if (strlen(name) < sizeof(g_exec_cache[0].name) && g_exec_cache_count < CF_EXEC_CACHE_CAP) {
        strncpy(g_exec_cache[g_exec_cache_count].name, name, sizeof(g_exec_cache[g_exec_cache_count].name) - 1);
        g_exec_cache[g_exec_cache_count].name[sizeof(g_exec_cache[g_exec_cache_count].name) - 1] = '\0';
        g_exec_cache[g_exec_cache_count].exists = exists;
        g_exec_cache_count++;
    }

    return exists;
}

bool cf_run_command_first_line(const char *command, char *out, size_t out_size) {
    if (!command || !command[0] || !out || out_size == 0) return false;

    FILE *fp = cf_capture_command(command, CF_COMMAND_TIMEOUT_MS, CF_COMMAND_OUTPUT_LIMIT);
    if (!fp) return false;

    bool ok = fgets(out, out_size, fp) != NULL;
    fclose(fp);
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

    return cf_parse_proc_stat_cpu_line(line, idle_all, total_all);
}

bool cf_parse_proc_stat_cpu_line(const char *line, unsigned long long *idle_all, unsigned long long *total_all) {
    if (!line || !idle_all || !total_all || !cf_starts_with(line, "cpu ")) return false;

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
    *total_all = user + nice + system + idle + iowait + irq + softirq + steal; /* guest times are already included in user/nice. */
    return true;
}

bool cf_detect_cpu_usage_percent(double *usage_out) {
#ifdef _WIN32
    (void)usage_out;
    return false;
#else
    if (!usage_out) return false;

    static bool have_prev = false;
    static unsigned long long prev_idle = 0;
    static unsigned long long prev_total = 0;

    unsigned long long idle_now = 0;
    unsigned long long total_now = 0;
    if (!read_cpu_times(&idle_now, &total_now)) return false;
    if (total_now == 0 || idle_now > total_now) return false;

    double usage = 0.0;

    if (have_prev && total_now > prev_total) {
        unsigned long long total_delta = total_now - prev_total;
        unsigned long long idle_delta = (idle_now >= prev_idle) ? (idle_now - prev_idle) : 0;

        if (total_delta == 0) return false;
        usage = (double)(total_delta - idle_delta) * 100.0 / (double)total_delta;
    } else {
        usage = (double)(total_now - idle_now) * 100.0 / (double)total_now;
    }

    prev_idle = idle_now;
    prev_total = total_now;
    have_prev = true;

    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;

    *usage_out = usage;
    return true;
#endif
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
#ifdef _WIN32
    (void)gpu_out;
    (void)gpu_out_size;
    return false;
#else
    FILE *fp = cf_capture_command("lspci 2>/dev/null", CF_COMMAND_TIMEOUT_MS, CF_COMMAND_OUTPUT_LIMIT);
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
        fclose(fp);
        return true;
    }

    fclose(fp);
    return false;
#endif
}

bool cf_read_pci_slot_from_uevent(const char *drm_name, char *slot_out, size_t slot_out_size) {
#ifdef _WIN32
    (void)drm_name;
    (void)slot_out;
    (void)slot_out_size;
    return false;
#else
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
#endif
}

bool cf_detect_gpu_from_pci_slot(const char *pci_slot, char *gpu_out, size_t gpu_out_size) {
#ifdef _WIN32
    (void)pci_slot;
    (void)gpu_out;
    (void)gpu_out_size;
    return false;
#else
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "lspci -s %s 2>/dev/null", pci_slot);

    FILE *fp = cf_capture_command(cmd, CF_COMMAND_TIMEOUT_MS, CF_COMMAND_OUTPUT_LIMIT);
    if (!fp) return false;

    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return false;
    }
    fclose(fp);

    cf_trim_newline(line);
    char *desc = strstr(line, ": ");
    desc = desc ? (desc + 2) : line;

    strncpy(gpu_out, desc, gpu_out_size);
    gpu_out[gpu_out_size - 1] = '\0';
    return gpu_out[0] != '\0';
#endif
}

bool cf_detect_primary_ip(char *iface_out, size_t iface_out_size, char *ip_out, size_t ip_out_size, bool *is_up) {
#ifdef _WIN32
    if (!iface_out || iface_out_size == 0 || !ip_out || ip_out_size == 0 || !is_up) return false;

    FILE *fp = cf_capture_command("ipconfig", CF_COMMAND_TIMEOUT_MS, CF_COMMAND_OUTPUT_LIMIT);
    if (!fp) return false;

    char line[512];
    char current_iface[128] = "";
    bool in_adapter = false;
    bool disconnected = false;

    while (fgets(line, sizeof(line), fp)) {
        cf_trim_newline(line);
        char *trimmed = cf_trim_spaces(line);
        if (!trimmed || !trimmed[0]) continue;

        if (cf_contains_icase(trimmed, "adapter") && trimmed[strlen(trimmed) - 1] == ':') {
            const char *start = strstr(trimmed, "adapter ");
            if (start) {
                start += strlen("adapter ");
                size_t len = strlen(start);
                if (len > 0 && start[len - 1] == ':') len--;
                if (len >= sizeof(current_iface)) len = sizeof(current_iface) - 1;
                memcpy(current_iface, start, len);
                current_iface[len] = '\0';
                in_adapter = true;
                disconnected = false;
            } else {
                in_adapter = false;
            }
            continue;
        }

        if (!in_adapter) continue;

        if (cf_contains_icase(trimmed, "Media State") && cf_contains_icase(trimmed, "disconnected")) {
            disconnected = true;
            continue;
        }

        if (cf_contains_icase(trimmed, "IPv4 Address")) {
            char *colon = strrchr(trimmed, ':');
            if (!colon) continue;

            char *ip = cf_trim_spaces(colon + 1);
            if (!ip || !ip[0]) continue;

            char *preferred = strchr(ip, '(');
            if (preferred) {
                *preferred = '\0';
                ip = cf_trim_spaces(ip);
            }

            if (strncmp(ip, "169.254.", 8) == 0) continue;

            strncpy(iface_out, current_iface[0] ? current_iface : "Ethernet", iface_out_size - 1);
            iface_out[iface_out_size - 1] = '\0';
            strncpy(ip_out, ip, ip_out_size - 1);
            ip_out[ip_out_size - 1] = '\0';
            *is_up = !disconnected;
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
#else
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
#endif
}

bool cf_get_public_ip(char *ip_out, size_t ip_out_size) {
#ifdef _WIN32
    FILE *fp = cf_capture_command(
        "powershell -NoProfile -Command \"try { (Invoke-RestMethod -Uri 'https://api.ipify.org' -TimeoutSec 2) } catch { '' }\" 2>nul", CF_COMMAND_TIMEOUT_MS, CF_COMMAND_OUTPUT_LIMIT);
#else
    FILE *fp = cf_capture_command(
        "sh -c \"if command -v curl >/dev/null 2>&1; then curl -fsS --max-time 2 https://api.ipify.org; "
        "elif command -v wget >/dev/null 2>&1; then wget -qO- --timeout=2 https://api.ipify.org; fi\" 2>/dev/null", CF_COMMAND_TIMEOUT_MS, CF_COMMAND_OUTPUT_LIMIT);
#endif
    if (!fp) return false;

    char buffer[128] = "";
    bool ok = fgets(buffer, sizeof(buffer), fp) != NULL;
    fclose(fp);

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

bool cf_parse_distro_def_line(
    const char *line,
    char *shortname_out,
    size_t shortname_out_size,
    char *longname_out,
    size_t longname_out_size,
    char *pkgcmd_out,
    size_t pkgcmd_out_size
) {
    if (!line || !shortname_out || !longname_out || !pkgcmd_out) return false;
    if (shortname_out_size == 0 || longname_out_size == 0 || pkgcmd_out_size == 0) return false;

    shortname_out[0] = '\0';
    longname_out[0] = '\0';
    pkgcmd_out[0] = '\0';

    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "DISTRO(", 7) != 0) return false;

    const char *cursor = strchr(p, '"');
    if (!cursor) return false;
    cursor++;

    const char *end = strchr(cursor, '"');
    if (!end) return false;
    size_t len = (size_t)(end - cursor);
    if (len >= shortname_out_size) len = shortname_out_size - 1;
    memcpy(shortname_out, cursor, len);
    shortname_out[len] = '\0';

    cursor = strchr(end + 1, '"');
    if (!cursor) return false;
    cursor++;
    end = strchr(cursor, '"');
    if (!end) return false;
    len = (size_t)(end - cursor);
    if (len >= longname_out_size) len = longname_out_size - 1;
    memcpy(longname_out, cursor, len);
    longname_out[len] = '\0';

    cursor = strchr(end + 1, '"');
    if (!cursor) return false;
    cursor++;
    end = strchr(cursor, '"');
    if (!end) return false;
    len = (size_t)(end - cursor);
    if (len >= pkgcmd_out_size) len = pkgcmd_out_size - 1;
    memcpy(pkgcmd_out, cursor, len);
    pkgcmd_out[len] = '\0';

    return true;
}

bool cf_parse_os_release_id_line(const char *line, char *id_out, size_t id_out_size) {
    if (!line || !id_out || id_out_size == 0) return false;

    id_out[0] = '\0';
    if (strncmp(line, "ID=", 3) != 0) return false;

    const char *value = line + 3;
    while (*value == ' ' || *value == '\t') value++;
    if (*value == '\0') return false;

    char temp[128];
    strncpy(temp, value, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    temp[strcspn(temp, "\r\n")] = '\0';

    char *trimmed = cf_trim_spaces(temp);
    size_t len = strlen(trimmed);
    if (len >= 2 && trimmed[0] == '"' && trimmed[len - 1] == '"') {
        trimmed[len - 1] = '\0';
        trimmed++;
    }

    if (!trimmed[0]) return false;

    strncpy(id_out, trimmed, id_out_size - 1);
    id_out[id_out_size - 1] = '\0';
    for (char *c = id_out; *c; c++) {
        *c = (char)tolower((unsigned char)*c);
    }
    return true;
}

unsigned long cf_convert_bytes_to_unit(unsigned long long bytes, unsigned long unit_size) {
    if (unit_size == 0) return 0;
    return (unsigned long)(bytes / unit_size);
}

#ifndef _WIN32
/* getmntent decodes procfs octal escapes and consumes complete mount records. */
bool cf_read_storage_mount(FILE *stream, const char **device, const char **mountpoint, const char **type) {
    if (!stream || !device || !mountpoint || !type) return false;
    struct mntent *entry = getmntent(stream);
    if (!entry) return false;
    *device = entry->mnt_fsname;
    *mountpoint = entry->mnt_dir;
    *type = entry->mnt_type;
    return true;
}
#endif

struct cf_cpu_core_id {
    int package;
    int core;
};

/* Count distinct (package, core) pairs, not the per-package "cpu cores" field.
 * Without complete topology, leave the physical core count unknown. */
bool cf_parse_cpuinfo(FILE *stream, char *model, size_t model_size, int *cores, int *threads) {
    if (!stream || !model || !model_size || !cores || !threads) return false;
    model[0] = '\0';
    *cores = *threads = 0;
    struct cf_cpu_core_id *ids = NULL;
    size_t count = 0, capacity = 0;
    int package = -1, core = -1;
    bool processor = false, complete = true;
    char line[512];
    for (;;) {
        bool have_line = fgets(line, sizeof(line), stream) != NULL;
        char *key = have_line ? cf_trim_spaces(line) : NULL;
        if (!have_line || !key[0]) {
            if (processor) {
                if (*threads == INT_MAX) { free(ids); return false; }
                ++*threads;
                if (package < 0 || core < 0) {
                    complete = false;
                } else {
                    size_t i;
                    for (i = 0; i < count; ++i) {
                        if (ids[i].package == package && ids[i].core == core) break;
                    }
                    if (i == count) {
                        if (count == capacity) {
                            size_t next = capacity ? capacity * 2 : 16;
                            if (next < capacity || next > (size_t)-1 / sizeof(*ids)) {
                                free(ids); return false;
                            }
                            struct cf_cpu_core_id *grown = realloc(ids, next * sizeof(*ids));
                            if (!grown) { free(ids); return false; }
                            ids = grown;
                            capacity = next;
                        }
                        ids[count].package = package;
                        ids[count++].core = core;
                    }
                }
            }
            processor = false;
            package = core = -1;
            if (!have_line) break;
            continue;
        }
        char *sep = strchr(key, ':');
        if (!sep) continue;
        *sep = '\0';
        key = cf_trim_spaces(key);
        char *value = cf_trim_spaces(sep + 1);
        if (strcmp(key, "model name") == 0 && !model[0]) {
            snprintf(model, model_size, "%s", value);
        } else if (strcmp(key, "processor") == 0) {
            processor = true;
        } else if (strcmp(key, "physical id") == 0 || strcmp(key, "core id") == 0) {
            char *end;
            errno = 0;
            long id = strtol(value, &end, 10);
            if (errno == 0 && end != value && *end == '\0' && id >= 0 && id <= INT_MAX) {
                if (strcmp(key, "physical id") == 0) package = (int)id;
                else core = (int)id;
            }
        }
    }
    *cores = complete ? (int)count : 0;
    free(ids);
    return !ferror(stream);
}
