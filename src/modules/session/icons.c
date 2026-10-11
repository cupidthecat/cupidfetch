#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

#include "../common/ini_helpers.h"

static void config_home(char *out, size_t out_size) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        strncpy(out, xdg, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    const char *home = getenv("HOME");
    if (home && home[0]) {
        snprintf(out, out_size, "%s/.config", home);
        return;
    }

    out[0] = '\0';
}

static void print_icons_with_backend(const char *icons, const char *backend) {
    if (!icons || !icons[0]) return;

    if (backend && backend[0]) {
        print_info("Icons", "%s [%s]", 20, 30, icons, backend);
        return;
    }

    print_info("Icons", "%s", 20, 30, icons);
}

void get_icons() {
    char value[256];
    char conf_home[512];
    config_home(conf_home, sizeof(conf_home));

    if (conf_home[0]) {
        char path[768];

        snprintf(path, sizeof(path), "%s/gtk-4.0/settings.ini", conf_home);
        if (cf_read_ini_value(path, "Settings", "gtk-icon-theme-name", value, sizeof(value))) {
            print_icons_with_backend(value, "GTK4");
            return;
        }

        snprintf(path, sizeof(path), "%s/gtk-3.0/settings.ini", conf_home);
        if (cf_read_ini_value(path, "Settings", "gtk-icon-theme-name", value, sizeof(value))) {
            print_icons_with_backend(value, "GTK3");
            return;
        }

        snprintf(path, sizeof(path), "%s/kdeglobals", conf_home);
        if (cf_read_ini_value(path, "Icons", "Theme", value, sizeof(value))) {
            print_icons_with_backend(value, "KDE");
            return;
        }
    }

    const char *home = getenv("HOME");
    if (home && home[0]) {
        char gtk2_path[768];
        snprintf(gtk2_path, sizeof(gtk2_path), "%s/.gtkrc-2.0", home);
        if (cf_read_ini_value(gtk2_path, NULL, "gtk-icon-theme-name", value, sizeof(value))) {
            print_icons_with_backend(value, "GTK2");
            return;
        }
    }

    if (cf_executable_in_path("gsettings") &&
        cf_run_command_first_line("gsettings get org.gnome.desktop.interface icon-theme 2>/dev/null", value, sizeof(value))) {
        cf_normalize_config_value(value);
        if (value[0]) {
            print_icons_with_backend(value, "GTK3");
            return;
        }
    }
}
