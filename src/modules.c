// File: modules.c
// -----------------------
#include <sys/statvfs.h>
#include "cupidfetch.h"
#define _GNU_SOURCE


#if defined(HAVE_X11) || defined(USE_X11)
#  include <X11/Xlib.h>
#  include <X11/Xatom.h>
#endif

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

/* --- /etc/os-release helpers --- */

static void trim_quotes(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n-1] == '"') || (s[0] == '\'' && s[n-1] == '\''))) {
        /* strip surrounding quotes */
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

static int os_release_get(const char *key, char *out, size_t outsz) {
    if (!key || !out || outsz == 0) return 0;
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) return 0;

    char line[512];
    int found = 0;
    size_t klen = strlen(key);

    while (fgets(line, sizeof line, f)) {
        /* Lines are like KEY=VALUE (VALUE may be quoted) */
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            char *val = line + klen + 1;
            /* Trim trailing newline */
            val[strcspn(val, "\r\n")] = '\0';
            trim_quotes(val);
            /* Copy out */
            strncpy(out, val, outsz - 1);
            out[outsz - 1] = '\0';
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

void get_distro() {
    char pretty[256] = {0};
    char name[128] = {0};
    char ver[128] = {0};
    char codename[128] = {0};
    char out[384] = {0};

    /* 1) Best: PRETTY_NAME straight from os-release */
    if (os_release_get("PRETTY_NAME", pretty, sizeof(pretty)) && pretty[0]) {
        print_info("Distro", "%s", 20, 30, pretty);
        return;
    }

    /* 2) Assemble NAME + VERSION_ID (+ codename) */
    os_release_get("NAME", name, sizeof(name));                 /* e.g., Ubuntu, Fedora, Arch Linux */
    os_release_get("VERSION_ID", ver, sizeof(ver));             /* e.g., 22.04, 40, rolling */
    os_release_get("VERSION_CODENAME", codename, sizeof(codename)); /* e.g., jammy */

    if (name[0]) {
        if (ver[0] && codename[0]) {
            /* "Ubuntu 22.04 (jammy)" */
            snprintf(out, sizeof(out), "%s %s (%s)", name, ver, codename);
        } else if (ver[0]) {
            /* "Ubuntu 22.04" */
            snprintf(out, sizeof(out), "%s %s", name, ver);
        } else {
            /* "Ubuntu" */
            snprintf(out, sizeof(out), "%s", name);
        }
        print_info("Distro", "%s", 20, 30, out);
        return;
    }

    /* 3) Fallback: keep current behavior (capitalized ID or known longname) */
    const char *fallback = detect_linux_distro();
    print_info("Distro", "%s", 20, 30, fallback);
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

void get_window_manager() {
    char wm_name[128];
    if (detect_window_manager(wm_name, sizeof(wm_name))) {
        print_info("Window Manager", "%s", 20, 30, wm_name);
    } else {
        cupid_log(LogType_ERROR, "couldn't detect window manager");
    }
}

/* --- Small utils --- */

static inline const char *getenv_nc(const char *k) {
    const char *v = getenv(k);
    return (v && *v) ? v : NULL;
}

static int str_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; ++p) {
        if (strncasecmp(p, needle, nlen) == 0) return 1;
    }
    return 0;
}

static void set_out(char *buf, size_t buflen, const char *s) {
    if (!buf || buflen == 0) return;
    if (!s) { buf[0] = '\0'; return; }
    strncpy(buf, s, buflen - 1);
    buf[buflen - 1] = '\0';
}

/* --- /proc process lookup (no root required) --- */

static int proc_has_process_named(const char *target) {
    if (!target || !*target) return 0;
    DIR *d = opendir("/proc");
    if (!d) return 0;

    struct dirent *de;
    char path[256];
    int found = 0;

    while ((de = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)de->d_name[0])) continue;

        snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;

        char comm[256] = {0};
        if (fgets(comm, sizeof(comm), f)) {
            // Trim newline
            size_t len = strlen(comm);
            if (len && comm[len - 1] == '\n') comm[len - 1] = '\0';
            if (strcmp(comm, target) == 0) { found = 1; fclose(f); break; }
        }
        fclose(f);
    }
    closedir(d);
    return found;
}

/* Given a prioritized list, return the first process name that exists. */
static int find_first_running(const char **names, size_t count, char *out, size_t outsz) {
    for (size_t i = 0; i < count; ++i) {
        if (proc_has_process_named(names[i])) {
            set_out(out, outsz, names[i]);
            return 1;
        }
    }
    return 0;
}

/* --- Session kind detection --- */

enum session_kind detect_session_kind(void) {
    const char *xdg_type = getenv_nc("XDG_SESSION_TYPE");
    if (xdg_type) {
        if (!strcasecmp(xdg_type, "x11"))     return SESSION_X11;
        if (!strcasecmp(xdg_type, "wayland")) return SESSION_WAYLAND;
    }
    /* Heuristics if XDG_SESSION_TYPE isn’t set */
    if (getenv_nc("WAYLAND_DISPLAY")) return SESSION_WAYLAND;
    if (getenv_nc("DISPLAY"))         return SESSION_X11;
    return SESSION_UNKNOWN;
}

/* --- X11 (EWMH) path --- */
#if defined(HAVE_X11) || defined(USE_X11)
static int get_wm_name_x11(char *buf, size_t buflen) {
    const char *display_name = getenv_nc("DISPLAY");
    if (!display_name) return 0;

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 0;

    Atom NET_SUPPORTING_WM_CHECK = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", True);
    if (NET_SUPPORTING_WM_CHECK == None) { XCloseDisplay(dpy); return 0; }

    Atom NET_WM_NAME = XInternAtom(dpy, "_NET_WM_NAME", True);
    Atom UTF8_STRING = XInternAtom(dpy, "UTF8_STRING", True);

    Window root = DefaultRootWindow(dpy);
    Atom actual_type; int actual_format; unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(dpy, root, NET_SUPPORTING_WM_CHECK, 0, 32, False, AnyPropertyType,
                           &actual_type, &actual_format, &nitems, &bytes_after, &prop) != Success) {
        XCloseDisplay(dpy);
        return 0;
    }

    if (!prop || actual_type == None || actual_format == 0 || nitems < 1) {
        if (prop) XFree(prop);
        XCloseDisplay(dpy);
        return 0;
    }

    Window wm_win = *(Window*)prop;
    XFree(prop);
    prop = NULL;

    /* Per EWMH, the wm_win should also have _NET_SUPPORTING_WM_CHECK pointing to itself. Optional check. */
    if (XGetWindowProperty(dpy, wm_win, NET_SUPPORTING_WM_CHECK, 0, 32, False, AnyPropertyType,
                           &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
        if (prop) XFree(prop);
        prop = NULL;
    }

    /* Try _NET_WM_NAME (UTF8_STRING) on wm_win */
    int ok = 0;
    if (NET_WM_NAME != None && UTF8_STRING != None) {
        if (XGetWindowProperty(dpy, wm_win, NET_WM_NAME, 0, (~0L), False, UTF8_STRING,
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
            if (prop && actual_type == UTF8_STRING && actual_format == 8 && nitems > 0) {
                set_out(buf, buflen, (const char*)prop);
                ok = 1;
            }
            if (prop) { XFree(prop); prop = NULL; }
        }
    }

    /* Fallback: WM_NAME (XA_STRING) */
    if (!ok) {
        Atom WM_NAME = XInternAtom(dpy, "WM_NAME", True);
        if (WM_NAME != None) {
            if (XGetWindowProperty(dpy, wm_win, WM_NAME, 0, (~0L), False, AnyPropertyType,
                                   &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
                if (prop && actual_format == 8 && nitems > 0) {
                    set_out(buf, buflen, (const char*)prop);
                    ok = 1;
                }
                if (prop) { XFree(prop); prop = NULL; }
            }
        }
    }

    XCloseDisplay(dpy);
    return ok;
}
#else
static int get_wm_name_x11(char *buf, size_t buflen) {
    (void)buf; (void)buflen;
    return 0; /* X11 support not compiled in */
}
#endif

/* --- Wayland env checks (fast path) --- */
static int get_wm_name_wayland_env(char *buf, size_t buflen) {
    /* Highly indicative single-var hints */
    if (getenv_nc("SWAYSOCK"))                          { set_out(buf, buflen, "sway");      return 1; }
    if (getenv_nc("HYPRLAND_INSTANCE_SIGNATURE"))       { set_out(buf, buflen, "Hyprland");  return 1; }
    if (getenv_nc("WAYFIRE_SHELL_REPLACED") || getenv_nc("WAYFIRE_PLUGIN_LOADED"))
                                                       { set_out(buf, buflen, "wayfire");    return 1; }
    if (getenv_nc("WESTON_CONFIG_FILE") || str_contains_ci(getenv_nc("XDG_SESSION_DESKTOP"), "weston"))
                                                       { set_out(buf, buflen, "weston");     return 1; }
    if (getenv_nc("RIVER_OPTIONS") || str_contains_ci(getenv_nc("XDG_SESSION_DESKTOP"), "river"))
                                                       { set_out(buf, buflen, "river");      return 1; }
    if (getenv_nc("LABWC_CONFIG") || str_contains_ci(getenv_nc("XDG_SESSION_DESKTOP"), "labwc"))
                                                       { set_out(buf, buflen, "labwc");      return 1; }

    /* Desktop family hints → map to compositor */
    const char *xdg_cur = getenv_nc("XDG_CURRENT_DESKTOP");
    const char *xdg_ses = getenv_nc("XDG_SESSION_DESKTOP");
    const char *desk = xdg_cur ? xdg_cur : xdg_ses;

    if (desk) {
        if (str_contains_ci(desk, "KDE") || str_contains_ci(desk, "PLASMA")) {
            set_out(buf, buflen, "KWin");
            return 1;
        }
        if (str_contains_ci(desk, "GNOME")) {
            /* On Wayland GNOME, Mutter is the compositor; users often recognize "GNOME" */
            set_out(buf, buflen, "Mutter");
            return 1;
        }
        if (str_contains_ci(desk, "COSMIC")) {
            set_out(buf, buflen, "cosmic-comp"); /* name used upstream for COSMIC compositor */
            return 1;
        }
        if (str_contains_ci(desk, "LXQt")) {
            /* LXQt on Wayland typically rides KWin */
            set_out(buf, buflen, "KWin");
            return 1;
        }
    }

    /* Generic Wayland present? Leave to process fallback */
    if (getenv_nc("WAYLAND_DISPLAY")) return 0;

    return 0;
}

/* --- X11 process fallback (unusual/no EWMH) --- */
static int get_wm_name_x11_proc(char *buf, size_t buflen) {
    /* Prioritized list (most common first) */
    static const char *candidates[] = {
        "i3", "bspwm", "awesome", "xmonad", "openbox", "herbstluftwm",
        "fluxbox", "icewm", "fvwm", "qtile", "wmaker", "pekwm", "jwm",
        "metacity", "muffin", "xfwm4", "kwin_x11", "kwin", "cinnamon", "sway", "dwm",
    };
    return find_first_running(candidates, sizeof(candidates)/sizeof(candidates[0]), buf, buflen);
}

/* --- Wayland process fallback --- */
static int get_wm_name_wayland_proc(char *buf, size_t buflen) {
    static const char *candidates[] = {
        "sway", "Hyprland", "kwin_wayland", "kwin", "gnome-shell", "mutter",
        "wayfire", "weston", "river", "labwc", "hikari", "cage", "cagebreak",
        "wlroots", /* rarely correct alone, but keep after others */
    };
    return find_first_running(candidates, sizeof(candidates)/sizeof(candidates[0]), buf, buflen);
}

/* --- Top-level orchestrator --- */
int detect_window_manager(char *buf, size_t buflen) {
    set_out(buf, buflen, NULL); /* clear */

    enum session_kind kind = detect_session_kind();

    if (kind == SESSION_X11) {
        if (get_wm_name_x11(buf, buflen)) return 1;
        /* EWMH failed/unusual WM → process fallback */
        if (get_wm_name_x11_proc(buf, buflen)) return 1;
        /* As a last resort, try Wayland env/process in case user is on mixed env */
        if (get_wm_name_wayland_env(buf, buflen)) return 1;
        if (get_wm_name_wayland_proc(buf, buflen)) return 1;
        return 0;
    }

    if (kind == SESSION_WAYLAND) {
        if (get_wm_name_wayland_env(buf, buflen)) return 1;
        if (get_wm_name_wayland_proc(buf, buflen)) return 1;
        /* Mixed case: some WMs run an X server inside Wayland */
        if (get_wm_name_x11(buf, buflen)) return 1;
        if (get_wm_name_x11_proc(buf, buflen)) return 1;
        return 0;
    }

    /* Unknown session: try everything in a sensible order */
    if (get_wm_name_wayland_env(buf, buflen)) return 1;
    if (get_wm_name_x11(buf, buflen)) return 1;
    if (get_wm_name_wayland_proc(buf, buflen)) return 1;
    if (get_wm_name_x11_proc(buf, buflen)) return 1;

    return 0;
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
