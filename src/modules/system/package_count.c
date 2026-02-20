#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_package_count() {
    const char* package_command = NULL;
    const char* distro = detect_linux_distro();

    #define DISTRO(shortname, longname, pkgcmd) else if(strcmp(distro, longname) == 0) {\
        package_command = pkgcmd;}

    if (0) {}
    #include "../../../data/distros.def"

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
        got_output = cf_run_command_first_line(package_command, output, sizeof(output));
    }

    if (!got_output) {
        for (size_t i = 0; i < sizeof(fallback_pkg_managers) / sizeof(fallback_pkg_managers[0]); i++) {
            if (!cf_executable_in_path(fallback_pkg_managers[i].binary)) continue;
            if (cf_run_command_first_line(fallback_pkg_managers[i].count_command, output, sizeof(output))) {
                got_output = true;
                break;
            }
        }
    }

    if (got_output) {
        print_info("Package Count", "%s", 20, 30, output);
    }
}
