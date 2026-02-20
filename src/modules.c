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

    if (package_command == NULL) return;

    // Run the package command and display the result
    FILE* fp = popen(package_command, "r");
    if (fp == NULL) {
        cupid_log(LogType_ERROR, "popen failed for package command");
        return;
    }

    char output[100];
    if (fgets(output, sizeof(output), fp) != NULL) {
        // Remove everything but the first line (if there are other lines)
        output[strcspn(output, "\n")] = 0;
        print_info("Package Count", "%s", 20, 30, output);
    }

    pclose(fp);
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
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        cupid_log(LogType_ERROR, "getifaddrs failed");
        return;
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr != NULL && ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char ip_addr[16]; // Allocate enough space for an IPv6 address
            snprintf(ip_addr, sizeof(ip_addr), "%s", inet_ntoa(addr->sin_addr));

            if (0 != strcmp(ifa->ifa_name, "lo")) {
                print_info("Local IP", "%s", 20, 30, ip_addr);
                break;
	    }
        }
    }
    freeifaddrs(ifaddr);
}

void get_battery() {
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) return;

    struct dirent *entry;
    unsigned long cap_sum = 0;
    size_t cap_count = 0;
    unsigned long energy_now_sum = 0;
    unsigned long energy_full_sum = 0;
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
        }

        unsigned long capacity = 0;
        if (build_power_supply_path(path, sizeof(path), entry->d_name, "/capacity") &&
            read_ulong_file(path, &capacity)) {
            cap_sum += capacity;
            cap_count++;
            continue;
        }

        char now_path[512];
        char full_path[512];
        unsigned long now_val = 0;
        unsigned long full_val = 0;

        bool have_energy_paths =
            build_power_supply_path(now_path, sizeof(now_path), entry->d_name, "/energy_now") &&
            build_power_supply_path(full_path, sizeof(full_path), entry->d_name, "/energy_full");

        if (!have_energy_paths || !read_ulong_file(now_path, &now_val) || !read_ulong_file(full_path, &full_val)) {
            if (!build_power_supply_path(now_path, sizeof(now_path), entry->d_name, "/charge_now") ||
                !build_power_supply_path(full_path, sizeof(full_path), entry->d_name, "/charge_full")) {
                continue;
            }
        }

        if (read_ulong_file(now_path, &now_val) && read_ulong_file(full_path, &full_val) && full_val > 0) {
            energy_now_sum += now_val;
            energy_full_sum += full_val;
        }
    }

    closedir(dir);

    if (!found_battery) return;

    unsigned long percent = 0;
    if (cap_count > 0) {
        percent = cap_sum / cap_count;
    } else if (energy_full_sum > 0) {
        percent = (energy_now_sum * 100UL) / energy_full_sum;
    } else {
        return;
    }

    if (percent > 100UL) percent = 100UL;

    if (battery_status[0] != '\0') {
        print_info("Battery", "%lu%% (%s)", 20, 30, percent, battery_status);
    } else {
        print_info("Battery", "%lu%%", 20, 30, percent);
    }
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

    char line[100];
    char model_name[100];
    int num_cores = 0;
    int num_threads = 0;

    while (fgets(line, sizeof(line), cpuinfo)) {
        char* token = strtok(line, ":");
        if (token != NULL) {
            if (strstr(token, "model name") != NULL) {
                // Extract CPU model name
                char* model_value = strtok(NULL, ":");
                if (model_value != NULL) {
                    snprintf(model_name, sizeof(model_name), "%s", model_value);
                    // Remove leading and trailing whitespaces
                    size_t len = strlen(model_name);
                    if (len > 0 && model_name[len - 1] == '\n') {
                        model_name[len - 1] = '\0';  // Remove newline character
                    }

                    // You can keep this block to remove leading whitespaces
                    char* start = model_name;
                    while (*start && (*start == ' ' || *start == '\t')) {
                        start++;
                    }
                    memmove(model_name, start, strlen(start) + 1);
                }
            } else if (strstr(token, "cpu cores") != NULL) {
                // Extract number of CPU cores
                char* cores_str = strtok(NULL, ":");
                if (cores_str != NULL) {
                    num_cores = atoi(cores_str);
                }
            } else if (strstr(token, "siblings") != NULL) {
                // Extract number of threads (siblings)
                char* threads_str = strtok(NULL, ":");
                if (threads_str != NULL) {
                    num_threads = atoi(threads_str);
                }
            }
        }
    }

    fclose(cpuinfo);

    if (strlen(model_name) > 0 && num_cores > 0 && num_threads > 0) {
        print_info("CPU Info", "Model: %s, Cores: %d, Threads: %d", 20, 30, model_name, num_cores, num_threads);
    } else {
        cupid_log(LogType_ERROR, "Failed to retrieve CPU information");
    }
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
        struct statvfs stat;

        if (statvfs(mnt_point, &stat) != 0) {
            cupid_log(LogType_INFO, "nothing for %s", mnt_point);
            continue;
        }

        unsigned long available = stat.f_bavail * stat.f_frsize / g_userConfig.storage_unit_size;
        unsigned long total = stat.f_blocks * stat.f_frsize / g_userConfig.storage_unit_size;

        if (total == 0) continue;

        print_info(
            first ? "Storage Information" : "",
            "%s: %lu %s / %lu %s",
            20, 30,
            mnt_point, available, g_userConfig.storage_unit,
            total, g_userConfig.storage_unit
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
