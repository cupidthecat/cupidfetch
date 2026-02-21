#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

typedef bool (*count_fn_t)(unsigned long *count_out);

typedef struct {
    const char *label;
    const char *binary;
    count_fn_t count_fn;
    const char *fallback_command;
} package_manager_probe;

static bool parse_count(const char *text, unsigned long *count_out) {
    if (!text || !text[0] || !count_out) return false;

    char buffer[64];
    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *trimmed = cf_trim_spaces(buffer);
    if (!trimmed || !trimmed[0]) return false;

    char *endptr = NULL;
    errno = 0;
    unsigned long parsed = strtoul(trimmed, &endptr, 10);
    if (errno != 0 || endptr == trimmed) return false;

    while (*endptr) {
        if (!isspace((unsigned char)*endptr)) return false;
        endptr++;
    }

    *count_out = parsed;
    return true;
}

static bool count_lines_from_command(const char *command, unsigned long *count_out) {
    if (!command || !command[0] || !count_out) return false;

    FILE *fp = popen(command, "r");
    if (!fp) return false;

    char line[512];
    unsigned long lines = 0;
    while (fgets(line, sizeof(line), fp)) {
        lines++;
    }

    int status = pclose(fp);
    if (status == -1) return false;

    *count_out = lines;
    return true;
}

static bool count_entries_in_dir(const char *path, unsigned long *count_out) {
    if (!path || !count_out) return false;

    DIR *dir = opendir(path);
    if (!dir) return false;

    struct dirent *entry;
    unsigned long count = 0;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (name[0] == '.') continue;
        count++;
    }

    closedir(dir);
    *count_out = count;
    return true;
}

static bool count_pacman_local(unsigned long *count_out) {
    return count_entries_in_dir("/var/lib/pacman/local", count_out);
}

static bool count_dpkg_status(unsigned long *count_out) {
    if (!count_out) return false;

    FILE *fp = fopen("/var/lib/dpkg/status", "r");
    if (!fp) return false;

    char line[512];
    unsigned long count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Status:", 7) != 0) continue;
        if (cf_contains_icase(line, "install ok installed")) count++;
    }

    fclose(fp);
    *count_out = count;
    return true;
}

static bool count_apk_db(unsigned long *count_out) {
    if (!count_out) return false;

    FILE *fp = fopen("/lib/apk/db/installed", "r");
    if (!fp) return false;

    char line[512];
    unsigned long count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "P:", 2) == 0) count++;
    }

    fclose(fp);
    *count_out = count;
    return true;
}

static bool count_slackware_db(unsigned long *count_out) {
    return count_entries_in_dir("/var/log/packages", count_out);
}

static bool is_shell_heavy_command(const char *command) {
    if (!command || !command[0]) return true;

    for (const char *p = command; *p; p++) {
        if (*p == '|' || *p == ';' || *p == '&' || *p == '>' || *p == '<' ||
            *p == '`' || *p == '$' || *p == '(' || *p == ')' ||
            *p == '\n' || *p == '\r') {
            return true;
        }
    }

    return false;
}

static bool distro_matches_any(const char *distro, const char *const *names, size_t names_count) {
    if (!distro || !distro[0]) return false;
    for (size_t i = 0; i < names_count; i++) {
        if (cf_contains_icase(distro, names[i])) return true;
    }
    return false;
}

static bool distro_prefers_manager(const char *distro, const char *label) {
    static const char *dpkg_distros[] = {
        "Ubuntu", "Debian", "Linux Mint", "Pop!_OS", "Zorin", "Kali", "MX", "elementary", "Peppermint"
    };
    static const char *pacman_distros[] = {
        "Arch", "Manjaro", "EndeavourOS", "Artix", "Antergos"
    };
    static const char *rpm_distros[] = {
        "Fedora", "CentOS", "openSUSE", "Mageia", "AlmaLinux", "Rocky", "Red Hat", "RHEL", "Amazon Linux"
    };
    static const char *apk_distros[] = {"Alpine"};
    static const char *xbps_distros[] = {"Void"};
    static const char *portage_distros[] = {"Gentoo"};
    static const char *eopkg_distros[] = {"Solus"};
    static const char *nix_distros[] = {"NixOS"};
    static const char *slack_distros[] = {"Slackware"};

    if (strcmp(label, "dpkg") == 0) {
        return distro_matches_any(distro, dpkg_distros, sizeof(dpkg_distros) / sizeof(dpkg_distros[0]));
    }
    if (strcmp(label, "pacman") == 0) {
        return distro_matches_any(distro, pacman_distros, sizeof(pacman_distros) / sizeof(pacman_distros[0]));
    }
    if (strcmp(label, "rpm") == 0) {
        return distro_matches_any(distro, rpm_distros, sizeof(rpm_distros) / sizeof(rpm_distros[0]));
    }
    if (strcmp(label, "apk") == 0) {
        return distro_matches_any(distro, apk_distros, sizeof(apk_distros) / sizeof(apk_distros[0]));
    }
    if (strcmp(label, "xbps") == 0) {
        return distro_matches_any(distro, xbps_distros, sizeof(xbps_distros) / sizeof(xbps_distros[0]));
    }
    if (strcmp(label, "portage") == 0) {
        return distro_matches_any(distro, portage_distros, sizeof(portage_distros) / sizeof(portage_distros[0]));
    }
    if (strcmp(label, "eopkg") == 0) {
        return distro_matches_any(distro, eopkg_distros, sizeof(eopkg_distros) / sizeof(eopkg_distros[0]));
    }
    if (strcmp(label, "nix") == 0) {
        return distro_matches_any(distro, nix_distros, sizeof(nix_distros) / sizeof(nix_distros[0]));
    }
    if (strcmp(label, "slackpkg") == 0) {
        return distro_matches_any(distro, slack_distros, sizeof(slack_distros) / sizeof(slack_distros[0]));
    }

    return false;
}

static const char *manager_label_from_command(const char *command) {
    if (!command || !command[0]) return "pkg";

    if (cf_contains_icase(command, "dpkg") || cf_contains_icase(command, "dpkg-query")) return "dpkg";
    if (cf_contains_icase(command, "pacman")) return "pacman";
    if (cf_contains_icase(command, "rpm")) return "rpm";
    if (cf_contains_icase(command, "xbps-query")) return "xbps";
    if (cf_contains_icase(command, "apk")) return "apk";
    if (cf_contains_icase(command, "equery")) return "portage";
    if (cf_contains_icase(command, "eopkg")) return "eopkg";
    if (cf_contains_icase(command, "slackpkg") || cf_contains_icase(command, "/var/log/packages")) return "slackpkg";
    if (cf_contains_icase(command, "nix-store") || cf_contains_icase(command, "nix-env")) return "nix";
    if (cf_contains_icase(command, "flatpak")) return "flatpak";
    if (cf_contains_icase(command, "snap")) return "snap";
    if (cf_contains_icase(command, "yay")) return "yay";
    if (cf_contains_icase(command, "paru")) return "paru";

    return "pkg";
}

static bool run_manager_probe(const package_manager_probe *probe, unsigned long *count_out) {
    if (!probe || !count_out) return false;

    if (probe->count_fn && probe->count_fn(count_out)) {
        return true;
    }

    if (probe->fallback_command && probe->fallback_command[0]) {
        return count_lines_from_command(probe->fallback_command, count_out);
    }

    return false;
}

static bool label_already_used(const char *label, char used[][24], size_t used_count) {
    if (!label || !label[0]) return false;
    for (size_t i = 0; i < used_count; i++) {
        if (strcmp(used[i], label) == 0) return true;
    }
    return false;
}

static void remember_label(const char *label, char used[][24], size_t *used_count, size_t used_cap) {
    if (!label || !label[0] || !used_count || *used_count >= used_cap) return;
    strncpy(used[*used_count], label, 23);
    used[*used_count][23] = '\0';
    (*used_count)++;
}

static bool append_labeled_count(char *out, size_t out_size, unsigned long count, const char *label) {
    if (!out || out_size == 0 || !label || !label[0]) return false;

    char item[96];
    snprintf(item, sizeof(item), "%lu (%s)", count, label);

    if (!out[0]) {
        size_t item_len = strlen(item);
        if (item_len + 1 > out_size) return false;
        memcpy(out, item, item_len + 1);
        return true;
    }

    size_t cur_len = strlen(out);
    size_t item_len = strlen(item);
    if (cur_len + 2 + item_len + 1 > out_size) return false;

    out[cur_len++] = ',';
    out[cur_len++] = ' ';
    memcpy(out + cur_len, item, item_len + 1);
    return true;
}

void get_package_count() {
#ifdef _WIN32
    static const package_manager_probe win_pkg_managers[] = {
        {"winget", "winget", NULL, "winget list 2>nul"},
        {"choco", "choco", NULL, "choco list -lo 2>nul"},
        {"scoop", "scoop", NULL, "scoop list 2>nul"},
    };

    char output[256] = "";
    bool appended_any = false;

    for (size_t i = 0; i < sizeof(win_pkg_managers) / sizeof(win_pkg_managers[0]); i++) {
        if (!cf_executable_in_path(win_pkg_managers[i].binary)) continue;
        unsigned long count = 0;
        if (!run_manager_probe(&win_pkg_managers[i], &count) || count == 0) continue;
        if (append_labeled_count(output, sizeof(output), count, win_pkg_managers[i].label)) {
            appended_any = true;
        }
    }

    if (appended_any) {
        print_info("Package Count", "%s", 20, 30, output);
    }
    return;
#else
    const char* package_command = NULL;
    const char* distro = detect_linux_distro();

    #define DISTRO(shortname, longname, pkgcmd) else if(strcmp(distro, longname) == 0) {\
        package_command = pkgcmd;}

    if (0) {}
    #include "../../../data/distros.def"

    static const package_manager_probe pkg_managers[] = {
        {"pacman", "pacman", count_pacman_local, "pacman -Qq 2>/dev/null"},
        {"dpkg", "dpkg-query", count_dpkg_status, "dpkg-query -W -f='${Package}\n' 2>/dev/null"},
        {"rpm", "rpm", NULL, "rpm -qa 2>/dev/null"},
        {"xbps", "xbps-query", NULL, "xbps-query -l 2>/dev/null"},
        {"apk", "apk", count_apk_db, "apk info 2>/dev/null"},
        {"portage", "equery", NULL, "equery -q list '*' 2>/dev/null"},
        {"eopkg", "eopkg", NULL, "eopkg list-installed 2>/dev/null"},
        {"nix", "nix-store", NULL, "nix-store --query --requisites /run/current-system/sw 2>/dev/null"},
        {"slackpkg", "slackpkg", count_slackware_db, NULL},
        {"snap", "snap", NULL, "snap list 2>/dev/null"},
        {"flatpak", "flatpak", NULL, "flatpak list --app 2>/dev/null"},
        {"yay", "yay", NULL, "yay -Qm 2>/dev/null"},
        {"paru", "paru", NULL, "paru -Qm 2>/dev/null"},
    };

    char output[256] = "";
    char cmd_output[128] = "";
    char used_labels[16][24] = {{0}};
    size_t used_label_count = 0;
    bool appended_any = false;

    for (size_t i = 0; i < sizeof(pkg_managers) / sizeof(pkg_managers[0]); i++) {
        if (!distro_prefers_manager(distro, pkg_managers[i].label)) continue;
        if (label_already_used(pkg_managers[i].label, used_labels, used_label_count)) continue;
        if (!cf_executable_in_path(pkg_managers[i].binary) && pkg_managers[i].count_fn == NULL) continue;

        unsigned long count = 0;
        if (!run_manager_probe(&pkg_managers[i], &count) || count == 0) continue;

        if (append_labeled_count(output, sizeof(output), count, pkg_managers[i].label)) {
            remember_label(pkg_managers[i].label, used_labels, &used_label_count, sizeof(used_labels) / sizeof(used_labels[0]));
            appended_any = true;
        }
    }

    if (!appended_any && package_command != NULL && package_command[0] != '\0' && !is_shell_heavy_command(package_command)) {
        if (cf_run_command_first_line(package_command, cmd_output, sizeof(cmd_output))) {
            unsigned long count = 0;
            if (parse_count(cmd_output, &count) && count > 0) {
                const char *label = manager_label_from_command(package_command);
                if (append_labeled_count(output, sizeof(output), count, label)) {
                    remember_label(label, used_labels, &used_label_count, sizeof(used_labels) / sizeof(used_labels[0]));
                    appended_any = true;
                }
            }
        }
    }

    for (size_t i = 0; i < sizeof(pkg_managers) / sizeof(pkg_managers[0]); i++) {
        if (label_already_used(pkg_managers[i].label, used_labels, used_label_count)) continue;
        if (!cf_executable_in_path(pkg_managers[i].binary) && pkg_managers[i].count_fn == NULL) continue;

        unsigned long count = 0;
        if (!run_manager_probe(&pkg_managers[i], &count) || count == 0) continue;

        if (append_labeled_count(output, sizeof(output), count, pkg_managers[i].label)) {
            remember_label(pkg_managers[i].label, used_labels, &used_label_count, sizeof(used_labels) / sizeof(used_labels[0]));
            appended_any = true;
        }
    }

    if (appended_any) {
        print_info("Package Count", "%s", 20, 30, output);
    }
#endif
}
