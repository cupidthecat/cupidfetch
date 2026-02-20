// File: modules.c
// -----------------------
#include <sys/statvfs.h>
#include "cupidfetch.h"

struct process_match {
    const char *proc_name;
    const char *label;
};

static void trim_newline(char *str) {
    str[strcspn(str, "\r\n")] = '\0';
}

static bool contains_icase(const char *haystack, const char *needle) {
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

static bool process_matches(const char *pid, const char *needle) {
    char path[64];
    char comm[128];

    snprintf(path, sizeof(path), "/proc/%s/comm", pid);
    FILE *comm_file = fopen(path, "r");
    if (comm_file) {
        if (fgets(comm, sizeof(comm), comm_file)) {
            trim_newline(comm);
            fclose(comm_file);
            if (contains_icase(comm, needle)) return true;
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

    return contains_icase(cmdline, needle);
}

static const char *detect_process_label(const struct process_match *candidates, size_t num_candidates) {
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

static const char *basename_or_self(const char *path) {
    if (!path || !path[0]) return NULL;
    const char *base = strrchr(path, '/');
    return base ? (base + 1) : path;
}

static bool read_first_line(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "r");
    if (!file) return false;

    if (!fgets(buffer, size, file)) {
        fclose(file);
        return false;
    }
    fclose(file);
    trim_newline(buffer);
    return true;
}

static bool read_ulong_file(const char *path, unsigned long *value) {
    char line[64];
    if (!read_first_line(path, line, sizeof(line))) return false;

    char *endptr = NULL;
    errno = 0;
    unsigned long parsed = strtoul(line, &endptr, 10);
    if (errno != 0 || endptr == line) return false;

    *value = parsed;
    return true;
}

static bool starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return false;
    size_t prefix_len = strlen(prefix);
    return strncmp(str, prefix, prefix_len) == 0;
}

static char *trim_spaces(char *str) {
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

static bool executable_in_path(const char *name) {
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

static bool run_command_first_line(const char *command, char *out, size_t out_size) {
    if (!command || !command[0] || !out || out_size == 0) return false;

    FILE *fp = popen(command, "r");
    if (!fp) return false;

    bool ok = fgets(out, out_size, fp) != NULL;
    pclose(fp);
    if (!ok) return false;

    trim_newline(out);
    char *trimmed = trim_spaces(out);
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

    if (!starts_with(line, "cpu ")) return false;

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

static bool detect_cpu_usage_percent(double *usage_out) {
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

static bool build_power_supply_path(
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

static void format_duration_compact(unsigned long seconds, char *buffer, size_t size) {
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

static bool is_drm_card_device(const char *name) {
    if (strncmp(name, "card", 4) != 0) return false;
    if (strchr(name, '-') != NULL) return false;

    const char *suffix = name + 4;
    if (*suffix == '\0') return false;

    for (const char *p = suffix; *p; p++) {
        if (!isdigit((unsigned char)*p)) return false;
    }
    return true;
}

static bool build_path3(
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

static const char *gpu_vendor_name(const char *vendor_id) {
    if (strcmp(vendor_id, "0x10de") == 0) return "NVIDIA";
    if (strcmp(vendor_id, "0x8086") == 0) return "Intel";
    if (strcmp(vendor_id, "0x1002") == 0) return "AMD";
    if (strcmp(vendor_id, "0x1022") == 0) return "AMD";
    if (strcmp(vendor_id, "0x13b5") == 0) return "ARM";
    if (strcmp(vendor_id, "0x5143") == 0) return "Qualcomm";
    if (strcmp(vendor_id, "0x1414") == 0) return "Microsoft";
    return NULL;
}

static void append_csv_item(char *dest, size_t dest_size, const char *item) {
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

static bool detect_gpu_from_lspci(char *gpu_out, size_t gpu_out_size) {
    FILE *fp = popen("lspci 2>/dev/null", "r");
    if (!fp) return false;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (!contains_icase(line, "vga compatible controller") &&
            !contains_icase(line, "3d controller") &&
            !contains_icase(line, "display controller") &&
            !contains_icase(line, "render driver")) {
            continue;
        }

        trim_newline(line);
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

static bool read_pci_slot_from_uevent(const char *drm_name, char *slot_out, size_t slot_out_size) {
    char path[512];
    if (!build_path3(path, sizeof(path), "/sys/class/drm/", drm_name, "/device/uevent")) {
        return false;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "PCI_SLOT_NAME=", 14) == 0) {
            char *value = line + 14;
            trim_newline(value);
            strncpy(slot_out, value, slot_out_size);
            slot_out[slot_out_size - 1] = '\0';
            found = slot_out[0] != '\0';
            break;
        }
    }

    fclose(fp);
    return found;
}

static bool detect_gpu_from_pci_slot(const char *pci_slot, char *gpu_out, size_t gpu_out_size) {
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

    trim_newline(line);
    char *desc = strstr(line, ": ");
    desc = desc ? (desc + 2) : line;

    strncpy(gpu_out, desc, gpu_out_size);
    gpu_out[gpu_out_size - 1] = '\0';
    return gpu_out[0] != '\0';
}

static bool detect_primary_ip(char *iface_out, size_t iface_out_size, char *ip_out, size_t ip_out_size, bool *is_up) {
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

static bool get_public_ip(char *ip_out, size_t ip_out_size) {
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

    trim_newline(buffer);
    if (buffer[0] == '\0') return false;

    strncpy(ip_out, buffer, ip_out_size);
    ip_out[ip_out_size - 1] = '\0';
    return true;
}

static void mask_public_ip(const char *ip_in, char *masked_out, size_t masked_out_size) {
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

void get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    	cupid_log(LogType_ERROR, "couldn't get hostname");
    else
	print_info("Hostname", hostname, 20, 30);
}

void get_username() {
    char* username = getlogin();
    if (username == NULL) {
        // Fallback: use getpwuid with effective UID
        struct passwd *pw = getpwuid(geteuid());
        if (pw != NULL)
            username = pw->pw_name;
    }
    if (username != NULL)
        print_info("Username", username, 20, 30);
    else
        cupid_log(LogType_ERROR, "couldn't get username");
}

void get_linux_kernel() {
    struct utsname uname_data;

    if (uname(&uname_data) != 0)
    	cupid_log(LogType_ERROR, "couldn't get uname data");
    else
	print_info("Linux Kernel", uname_data.release, 20, 30);
}

void get_uptime() {
    FILE* uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file == NULL) {
        cupid_log(LogType_ERROR, "couldn't open /proc/uptime");
        return;
    }

    double uptime;
    if (fscanf(uptime_file, "%lf", &uptime) != 1) {
        cupid_log(LogType_ERROR, "couldn't read uptime from /proc/uptime");
        fclose(uptime_file);
        return;
    }
    fclose(uptime_file);

    int days = (int)uptime / (60 * 60 * 24);
    int hours = ((int)uptime % (60 * 60 * 24)) / (60 * 60);
    int minutes = ((int)uptime % (60 * 60)) / 60;

    // Corrected usage of print_info
    print_info("Uptime", "%d days, %02d:%02d", 20, 30, days, hours, minutes);
}

void get_distro() {
    const char *distro = detect_linux_distro();
    print_info("Distro", distro, 20, 30);
}

void get_package_count() {
    const char* package_command = NULL;
    const char* distro = detect_linux_distro();


    #define DISTRO(shortname, longname, pkgcmd) else if(strcmp(distro, longname) == 0) {\
    	package_command = pkgcmd;}

    if (0) {}
    #include "../data/distros.def"

    static const struct {
        const char *binary;
        const char *count_command;
    } fallback_pkg_managers[] = {
        {"pacman", "pacman -Qq 2>/dev/null | wc -l"},
        {"dpkg-query", "dpkg-query -f '.' -W 2>/dev/null | wc -c"},
        {"rpm", "rpm -qa 2>/dev/null | wc -l"},
        {"xbps-query", "xbps-query -l 2>/dev/null | wc -l"},
        {"apk", "apk info 2>/dev/null | wc -l"},
        {"equery", "equery list '*' 2>/dev/null | wc -l"},
        {"nix-store", "nix-store --query --requisites /run/current-system/sw 2>/dev/null | wc -l"},
        {"flatpak", "flatpak list --app 2>/dev/null | wc -l"},
    };

    char output[128] = "";
    bool got_output = false;

    if (package_command != NULL) {
        got_output = run_command_first_line(package_command, output, sizeof(output));
    }

    if (!got_output) {
        for (size_t i = 0; i < sizeof(fallback_pkg_managers) / sizeof(fallback_pkg_managers[0]); i++) {
            if (!executable_in_path(fallback_pkg_managers[i].binary)) continue;
            if (run_command_first_line(fallback_pkg_managers[i].count_command, output, sizeof(output))) {
                got_output = true;
                break;
            }
        }
    }

    if (got_output) {
        print_info("Package Count", "%s", 20, 30, output);
    }
}

void get_shell() {
    uid_t uid = geteuid();

    struct passwd *pw = getpwuid(uid);
    if (pw == NULL) {
        cupid_log(LogType_ERROR, "getpwuid failed to retrieve user password entry");
        return;
    }

    // Extract the shell from the password file entry
    const char *shell = pw->pw_shell;
    if (shell == NULL) {
        cupid_log(LogType_ERROR, "getpwuid failed to retrieve user shell information");
        return;
    }

    // Extract the base name of the shell
    const char *baseName = strrchr(shell, '/');
    baseName = (baseName != NULL) ? baseName + 1 : shell;

    print_info("Shell", baseName, 20, 30);
}

void get_terminal() {
    if (!isatty(STDOUT_FILENO)) {
        cupid_log(LogType_ERROR, "Not running in a terminal");
        return;
    }

    const char *term_program = getenv("TERM");
    if (term_program == NULL) {
        cupid_log(LogType_ERROR, "Failed to retrieve terminal program information");
        return;
    }

    print_info("Terminal", "%s", 20, 30, term_program);
}

void get_desktop_environment() {
    const char* xdgDesktop = getenv("XDG_CURRENT_DESKTOP");
    if (xdgDesktop != NULL && strlen(xdgDesktop) > 0) {
        print_info("DE", xdgDesktop, 20, 30);
        return;
    }

    const char* desktopSession = getenv("DESKTOP_SESSION");
    if (desktopSession != NULL && strlen(desktopSession) > 0) {
        print_info("DE", desktopSession, 20, 30);
        return;
    }

    static const struct process_match de_candidates[] = {
        {"gnome-shell", "GNOME"},
        {"plasmashell", "KDE Plasma"},
        {"xfce4-session", "XFCE"},
        {"mate-session", "MATE"},
        {"lxqt-session", "LXQt"},
        {"lxsession", "LXDE"},
        {"cinnamon-session", "Cinnamon"},
        {"budgie-desktop", "Budgie"},
    };

    const char *detected = detect_process_label(de_candidates, sizeof(de_candidates) / sizeof(de_candidates[0]));
    if (detected) {
        print_info("DE", detected, 20, 30);
    }
}

void get_window_manager() {
    const char *wm_env = getenv("WINDOWMANAGER");
    const char *wm_name = basename_or_self(wm_env);
    if (wm_name && wm_name[0]) {
        print_info("WM", wm_name, 20, 30);
        return;
    }

    static const struct process_match wm_candidates[] = {
        {"hyprland", "Hyprland"},
        {"sway", "Sway"},
        {"river", "River"},
        {"labwc", "labwc"},
        {"i3", "i3"},
        {"bspwm", "bspwm"},
        {"dwm", "dwm"},
        {"awesome", "Awesome"},
        {"openbox", "Openbox"},
        {"fluxbox", "Fluxbox"},
        {"kwin_wayland", "KWin (Wayland)"},
        {"kwin_x11", "KWin (X11)"},
        {"mutter", "Mutter"},
        {"xfwm4", "Xfwm4"},
        {"marco", "Marco"},
    };

    const char *detected = detect_process_label(wm_candidates, sizeof(wm_candidates) / sizeof(wm_candidates[0]));
    if (detected) {
        print_info("WM", detected, 20, 30);
    }
}

void get_display_server() {
    const char *session_type = getenv("XDG_SESSION_TYPE");
    if (session_type && session_type[0]) {
        if (contains_icase(session_type, "wayland")) {
            print_info("Display Server", "Wayland", 20, 30);
            return;
        }
        if (contains_icase(session_type, "x11")) {
            print_info("Display Server", "X11", 20, 30);
            return;
        }
        print_info("Display Server", session_type, 20, 30);
        return;
    }

    if (getenv("WAYLAND_DISPLAY")) {
        print_info("Display Server", "Wayland", 20, 30);
        return;
    }

    if (getenv("DISPLAY")) {
        print_info("Display Server", "X11", 20, 30);
        return;
    }

    static const struct process_match ds_candidates[] = {
        {"xwayland", "XWayland"},
        {"Xorg", "X11"},
        {"wayland", "Wayland"},
    };

    const char *detected = detect_process_label(ds_candidates, sizeof(ds_candidates) / sizeof(ds_candidates[0]));
    if (detected) {
        print_info("Display Server", detected, 20, 30);
    }
}

// haha got ur ip
void get_local_ip() {
    char iface[64] = "";
    char ip_addr[INET6_ADDRSTRLEN] = "";
    bool up = false;

    if (!detect_primary_ip(iface, sizeof(iface), ip_addr, sizeof(ip_addr), &up)) {
        return;
    }

    print_info("Local IP", "%s", 20, 30, ip_addr);
}

void get_net() {
    char iface[64] = "";
    char local_ip[INET6_ADDRSTRLEN] = "";
    char public_ip[128] = "";
    char public_ip_display[128] = "";
    bool up = false;

    if (!detect_primary_ip(iface, sizeof(iface), local_ip, sizeof(local_ip), &up)) {
        print_info("Net", "Disconnected", 20, 30);
        return;
    }

    const char *state = up ? "up" : "down";
    if (get_public_ip(public_ip, sizeof(public_ip))) {
        if (g_userConfig.network_show_full_public_ip) {
            snprintf(public_ip_display, sizeof(public_ip_display), "%s", public_ip);
        } else {
            mask_public_ip(public_ip, public_ip_display, sizeof(public_ip_display));
        }

        print_info("Net", "%s (%s) | local %s | public %s", 20, 30, iface, state, local_ip, public_ip_display);
    } else {
        print_info("Net", "%s (%s) | local %s | public unavailable", 20, 30, iface, state, local_ip);
    }
}

void get_battery() {
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) return;

    struct dirent *entry;
    unsigned long cap_sum = 0;
    size_t cap_count = 0;
    unsigned long energy_now_sum = 0;
    unsigned long energy_full_sum = 0;
    unsigned long energy_rate_sum = 0;
    unsigned long charge_now_sum = 0;
    unsigned long charge_full_sum = 0;
    unsigned long charge_rate_sum = 0;
    bool found_battery = false;
    char battery_status[64] = "";

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[512];
        char type[64];

        if (!build_power_supply_path(path, sizeof(path), entry->d_name, "/type")) continue;

        if (!read_first_line(path, type, sizeof(type))) continue;
        if (strcmp(type, "Battery") != 0) continue;

        found_battery = true;

        if (battery_status[0] == '\0') {
            if (build_power_supply_path(path, sizeof(path), entry->d_name, "/status")) {
                read_first_line(path, battery_status, sizeof(battery_status));
            }
        } else if (build_power_supply_path(path, sizeof(path), entry->d_name, "/status")) {
            char status_this[64];
            if (read_first_line(path, status_this, sizeof(status_this)) &&
                status_this[0] != '\0' && strcmp(battery_status, status_this) != 0) {
                snprintf(battery_status, sizeof(battery_status), "Mixed");
            }
        }

        unsigned long capacity = 0;
        if (build_power_supply_path(path, sizeof(path), entry->d_name, "/capacity") &&
            read_ulong_file(path, &capacity)) {
            cap_sum += capacity;
            cap_count++;
        }

        char now_path[512];
        char full_path[512];
        char rate_path[512];
        unsigned long now_val = 0;
        unsigned long full_val = 0;
        unsigned long rate_val = 0;

        bool have_energy_paths =
            build_power_supply_path(now_path, sizeof(now_path), entry->d_name, "/energy_now") &&
            build_power_supply_path(full_path, sizeof(full_path), entry->d_name, "/energy_full");

        if (have_energy_paths && read_ulong_file(now_path, &now_val) && read_ulong_file(full_path, &full_val) && full_val > 0) {
            energy_now_sum += now_val;
            energy_full_sum += full_val;

            if (build_power_supply_path(rate_path, sizeof(rate_path), entry->d_name, "/power_now") &&
                read_ulong_file(rate_path, &rate_val)) {
                energy_rate_sum += rate_val;
            }
            continue;
        }

        if (!build_power_supply_path(now_path, sizeof(now_path), entry->d_name, "/charge_now") ||
            !build_power_supply_path(full_path, sizeof(full_path), entry->d_name, "/charge_full")) {
            continue;
        }

        if (read_ulong_file(now_path, &now_val) && read_ulong_file(full_path, &full_val) && full_val > 0) {
            charge_now_sum += now_val;
            charge_full_sum += full_val;

            if (build_power_supply_path(rate_path, sizeof(rate_path), entry->d_name, "/current_now") &&
                read_ulong_file(rate_path, &rate_val)) {
                charge_rate_sum += rate_val;
            }
        }
    }

    closedir(dir);

    if (!found_battery) return;

    unsigned long percent = 0;
    if (cap_count > 0) {
        percent = cap_sum / cap_count;
    } else if (energy_full_sum > 0) {
        percent = (energy_now_sum * 100UL) / energy_full_sum;
    } else if (charge_full_sum > 0) {
        percent = (charge_now_sum * 100UL) / charge_full_sum;
    } else {
        return;
    }

    if (percent > 100UL) percent = 100UL;

    bool is_charging = contains_icase(battery_status, "charging") && !contains_icase(battery_status, "discharging");
    bool is_discharging = contains_icase(battery_status, "discharging");
    bool have_time = false;
    unsigned long remaining_seconds = 0;

    if (is_discharging) {
        if (energy_now_sum > 0 && energy_rate_sum > 0) {
            remaining_seconds = (unsigned long)(((unsigned long long)energy_now_sum * 3600ULL) / (unsigned long long)energy_rate_sum);
            have_time = true;
        } else if (charge_now_sum > 0 && charge_rate_sum > 0) {
            remaining_seconds = (unsigned long)(((unsigned long long)charge_now_sum * 3600ULL) / (unsigned long long)charge_rate_sum);
            have_time = true;
        }
    } else if (is_charging) {
        if (energy_full_sum > energy_now_sum && energy_rate_sum > 0) {
            unsigned long to_full = energy_full_sum - energy_now_sum;
            remaining_seconds = (unsigned long)(((unsigned long long)to_full * 3600ULL) / (unsigned long long)energy_rate_sum);
            have_time = true;
        } else if (charge_full_sum > charge_now_sum && charge_rate_sum > 0) {
            unsigned long to_full = charge_full_sum - charge_now_sum;
            remaining_seconds = (unsigned long)(((unsigned long long)to_full * 3600ULL) / (unsigned long long)charge_rate_sum);
            have_time = true;
        }
    }

    if (battery_status[0] != '\0') {
        if (have_time) {
            char duration[32];
            format_duration_compact(remaining_seconds, duration, sizeof(duration));
            if (is_discharging) {
                print_info("Battery", "%lu%% (%s, %s left)", 20, 30, percent, battery_status, duration);
            } else if (is_charging) {
                print_info("Battery", "%lu%% (%s, %s until full)", 20, 30, percent, battery_status, duration);
            } else {
                print_info("Battery", "%lu%% (%s)", 20, 30, percent, battery_status);
            }
        } else {
            print_info("Battery", "%lu%% (%s)", 20, 30, percent, battery_status);
        }
    } else {
        print_info("Battery", "%lu%%", 20, 30, percent);
    }
}

void get_gpu() {
    DIR *dir = opendir("/sys/class/drm");
    char gpu_summary[256] = "";

    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!is_drm_card_device(entry->d_name)) continue;

            char lspci_desc[256] = "";
            char pci_slot[64] = "";
            if (read_pci_slot_from_uevent(entry->d_name, pci_slot, sizeof(pci_slot)) &&
                detect_gpu_from_pci_slot(pci_slot, lspci_desc, sizeof(lspci_desc))) {
                append_csv_item(gpu_summary, sizeof(gpu_summary), lspci_desc);
                continue;
            }

            char path[512];
            char vendor_id[64] = "";
            char driver_link[512] = "";

            if (build_path3(path, sizeof(path), "/sys/class/drm/", entry->d_name, "/device/vendor")) {
                read_first_line(path, vendor_id, sizeof(vendor_id));
            }

            if (build_path3(path, sizeof(path), "/sys/class/drm/", entry->d_name, "/device/driver")) {
                ssize_t n = readlink(path, driver_link, sizeof(driver_link) - 1);
                if (n > 0) {
                    driver_link[n] = '\0';
                } else {
                    driver_link[0] = '\0';
                }
            }

            const char *vendor = (vendor_id[0] != '\0') ? gpu_vendor_name(vendor_id) : NULL;
            const char *driver = NULL;
            if (driver_link[0] != '\0') {
                driver = strrchr(driver_link, '/');
                driver = driver ? (driver + 1) : driver_link;
            }

            char item[640];
            if (vendor && driver) {
                snprintf(item, sizeof(item), "%s (%.600s)", vendor, driver);
            } else if (vendor) {
                snprintf(item, sizeof(item), "%s", vendor);
            } else if (driver) {
                snprintf(item, sizeof(item), "%.600s", driver);
            } else {
                continue;
            }

            append_csv_item(gpu_summary, sizeof(gpu_summary), item);
        }
        closedir(dir);
    }

    if (gpu_summary[0] == '\0') {
        if (!detect_gpu_from_lspci(gpu_summary, sizeof(gpu_summary))) {
            return;
        }
    }

    print_info("GPU", "%s", 20, 30, gpu_summary);
}

void get_available_memory() {
    // Linux-specific implementation
    // Source: https://github.com/KittyKatt/screenFetch/issues/386#issuecomment-249312716
    // Also used in neofetch
    
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
        mem_used * 1024 / g_userConfig.memory_unit_size,
        g_userConfig.memory_unit,
        mem_total * 1024 / g_userConfig.memory_unit_size,
        g_userConfig.memory_unit
    );
}


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
        if (starts_with(line, "processor")) {
            logical_threads++;
            continue;
        }

        char *sep = strchr(line, ':');
        if (!sep) continue;
        *sep = '\0';

        char *key = trim_spaces(line);
        char *value = trim_spaces(sep + 1);
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
    bool has_usage = detect_cpu_usage_percent(&cpu_usage);

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

static bool should_skip_storage_mount(const char *device, const char *mnt_point, const char *fs_type) {
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

void get_available_storage() {
    FILE* mount_file = fopen("/proc/mounts", "r");
    if (mount_file == NULL) {
        cupid_log(LogType_ERROR, "couldn't open /proc/mounts");
        return;
    }

    bool first = true;
    char device[256], mnt_point[256], fs_type[256];
    while (fscanf(mount_file, "%255s %255s %255s %*s %*d %*d", device, mnt_point, fs_type) == 3) {
        if (should_skip_storage_mount(device, mnt_point, fs_type)) {
            continue;
        }

        struct statvfs stat;

        if (statvfs(mnt_point, &stat) != 0) {
            cupid_log(LogType_INFO, "nothing for %s", mnt_point);
            continue;
        }

        unsigned long long total_bytes = (unsigned long long)stat.f_blocks * (unsigned long long)stat.f_frsize;
        unsigned long long available_bytes = (unsigned long long)stat.f_bavail * (unsigned long long)stat.f_frsize;

        unsigned long total = (unsigned long)(total_bytes / g_userConfig.storage_unit_size);
        unsigned long available = (unsigned long)(available_bytes / g_userConfig.storage_unit_size);
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

const char* get_home_directory() {
    static char *homeDir = NULL;

    // It may be fine to memoize
    if (homeDir != NULL) return homeDir;

    if ((homeDir = getenv("HOME")) == NULL) {
        struct passwd* pw = getpwuid(getuid());
        if (pw == NULL) {
	    fprintf(stderr, "home directory couldn't be found sir\n");
	    exit(EXIT_FAILURE);
	}
        homeDir = pw->pw_dir;
    }
    return homeDir;
}
