// File: modules.c
// -----------------------
#include <sys/statvfs.h>
#include "cupidfetch.h"

void get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    	cupid_log(LogType_ERROR, "couldn't get hostname");
    else
	print_info("Hostname", hostname, 20, 30);
}

void get_username() {
    char* username = getlogin();
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

// FIXME: dead code
/*
void get_window_manager() {
    // Only get window managers, not desktop environments
    // Same logic as get_desktop_environment() but only looking or WMs

    DIR *dir;
    struct dirent *entry;

    dir = opendir("/proc");
    if (dir == NULL) return;

    int wmFound = 0;  // Flag to indicate if a window manager has been found

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            char path[270];
            snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);

            FILE *cmdlineFile = fopen(path, "r");
            if (cmdlineFile != NULL) {
                char cmdline[270];
                if (fgets(cmdline, sizeof(cmdline), cmdlineFile) != NULL) {
                    // FIXME: hardcoded names

                    // Example: Check if "fvwm" is in the cmdline
                    if (strstr(cmdline, "fvwm") != NULL) {
                        print_info("WM", "FVWM", 20, 30);
                        wmFound = 1;
                        break;
                    }

                    if (strstr(cmdline, "i3") != NULL) {
                        print_info("WM", "i3", 20, 30);
                        wmFound = 1;
                        break;
                    }

                    if (strstr(cmdline, "awesome") != NULL) {
                        print_info("WM", "Awesome", 20, 30);
                        wmFound = 1;
                        break;
                    }

                    if (strstr(cmdline, "bspwm") != NULL) {
                        print_info("WM", "bspwm", 20, 30);
                        wmFound = 1;
                        break;
                    }

                    if (strstr(cmdline, "dwm") != NULL) {
                        print_info("WM", "dwm", 20, 30);
                        wmFound = 1;
                        break;
                    }

                    if (strstr(cmdline, "hyprland") != NULL) {
                        print_info("WM", "hyprland", 20, 30);
                        wmFound = 1;
                        break;
                    }

                }

                fclose(cmdlineFile);
            }
        }
    }

    closedir(dir);

    // If no desktop environment is detected, print nothing since we dont want to display anything
    if (!wmFound) {
        //print_info("WM", "Unknown", 20, 30);
    }
}
*/

void get_desktop_environment() {
    const char* xdgDesktop = getenv("XDG_CURRENT_DESKTOP");
    if (xdgDesktop != NULL && strlen(xdgDesktop) > 0) {
        print_info("DE", xdgDesktop, 20, 30);
        return; // Successfully retrieved desktop environment
    }

    // If $XDG_CURRENT_DESKTOP is empty or not found, try checking processes
    // NOTE: hardcoded :(

    DIR *dir;
    struct dirent *entry;

    dir = opendir("/proc");
    if (dir == NULL) {
        cupid_log(LogType_ERROR, "Failed to open /proc directory");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            char path[270];
            snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);

            FILE *cmdlineFile = fopen(path, "r");
            if (cmdlineFile != NULL) {
                char cmdline[270];
                if (fgets(cmdline, sizeof(cmdline), cmdlineFile) != NULL) {

                    // Example: Check if "gnome-shell" is in the cmdline
                    if (strstr(cmdline, "gnome-shell") != NULL) {
                        print_info("DE", "GNOME", 20, 30);
                        break;
                    }

                    // Example: Check if "plasma-desktop" is in the cmdline
                    if (strstr(cmdline, "plasma-desktop") != NULL) {
                        print_info("DE", "KDE Plasma", 20, 30);
                        break;
                    }

                    if (strstr(cmdline, "xfce4-session") != NULL) {
                        print_info("DE", "XFCE", 20, 30);
                        break;
                    }

                    if (strstr(cmdline, "mate-session") != NULL) {
                        print_info("DE", "MATE", 20, 30);
                        break;
                    }

                    if (strstr(cmdline, "lxqt-session") != NULL) {
                        print_info("DE", "LXQt", 20, 30);
                        break;
                    }

                    if (strstr(cmdline, "lxde-session") != NULL) {
                        print_info("DE", "LXDE", 20, 30);
                        break;
                    }

                }

                fclose(cmdlineFile);
            }
        }
    }

    closedir(dir);

    // This can just be removed ig idk
    if (entry == NULL) {
        cupid_log(LogType_INFO, "Failed to read /proc directory");
        //print_info("DE", "Unknown", 20, 30);
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
