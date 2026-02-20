#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

static bool eq_icase_local(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void normalize_value(char *value) {
    if (!value) return;

    char *trimmed = cf_trim_spaces(value);
    if (trimmed != value) {
        memmove(value, trimmed, strlen(trimmed) + 1);
    }

    size_t len = strlen(value);
    if (len >= 2) {
        bool single_quoted = value[0] == '\'' && value[len - 1] == '\'';
        bool double_quoted = value[0] == '"' && value[len - 1] == '"';
        if (single_quoted || double_quoted) {
            memmove(value, value + 1, len - 2);
            value[len - 2] = '\0';
        }
    }

    trimmed = cf_trim_spaces(value);
    if (trimmed != value) {
        memmove(value, trimmed, strlen(trimmed) + 1);
    }
}

static bool read_ini_value(const char *path, const char *section, const char *key, char *out, size_t out_size) {
    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[512];
    char current_section[128] = "";

    while (fgets(line, sizeof(line), fp)) {
        cf_trim_newline(line);
        char *trimmed = cf_trim_spaces(line);
        if (!trimmed[0] || trimmed[0] == '#' || trimmed[0] == ';') continue;

        size_t len = strlen(trimmed);
        if (trimmed[0] == '[' && len > 2 && trimmed[len - 1] == ']') {
            trimmed[len - 1] = '\0';
            strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
            current_section[sizeof(current_section) - 1] = '\0';
            char *sec_trim = cf_trim_spaces(current_section);
            if (sec_trim != current_section) {
                memmove(current_section, sec_trim, strlen(sec_trim) + 1);
            }
            continue;
        }

        if (section && section[0] && !eq_icase_local(current_section, section)) continue;

        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *left = cf_trim_spaces(trimmed);
        if (!eq_icase_local(left, key)) continue;

        char *right = cf_trim_spaces(eq + 1);
        strncpy(out, right, out_size - 1);
        out[out_size - 1] = '\0';
        fclose(fp);
        normalize_value(out);
        return out[0] != '\0';
    }

    fclose(fp);
    return false;
}

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

static void print_theme_with_backend(const char *theme, const char *backend) {
    if (!theme || !theme[0]) return;

    if (backend && backend[0]) {
        print_info("Theme", "%s [%s]", 20, 30, theme, backend);
        return;
    }

    print_info("Theme", "%s", 20, 30, theme);
}

void get_theme() {
    const char *gtk_theme_env = getenv("GTK_THEME");
    if (gtk_theme_env && gtk_theme_env[0]) {
        print_theme_with_backend(gtk_theme_env, "GTK3");
        return;
    }

    char value[256];
    char conf_home[512];
    config_home(conf_home, sizeof(conf_home));

    if (conf_home[0]) {
        char path[768];

        snprintf(path, sizeof(path), "%s/gtk-4.0/settings.ini", conf_home);
        if (read_ini_value(path, "Settings", "gtk-theme-name", value, sizeof(value))) {
            print_theme_with_backend(value, "GTK4");
            return;
        }

        snprintf(path, sizeof(path), "%s/gtk-3.0/settings.ini", conf_home);
        if (read_ini_value(path, "Settings", "gtk-theme-name", value, sizeof(value))) {
            print_theme_with_backend(value, "GTK3");
            return;
        }

        snprintf(path, sizeof(path), "%s/kdeglobals", conf_home);
        if (read_ini_value(path, "KDE", "LookAndFeelPackage", value, sizeof(value)) ||
            read_ini_value(path, "General", "ColorScheme", value, sizeof(value))) {
            print_theme_with_backend(value, "KDE");
            return;
        }
    }

    const char *home = getenv("HOME");
    if (home && home[0]) {
        char gtk2_path[768];
        snprintf(gtk2_path, sizeof(gtk2_path), "%s/.gtkrc-2.0", home);
        if (read_ini_value(gtk2_path, NULL, "gtk-theme-name", value, sizeof(value))) {
            print_theme_with_backend(value, "GTK2");
            return;
        }
    }

    if (cf_executable_in_path("gsettings") &&
        cf_run_command_first_line("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null", value, sizeof(value))) {
        normalize_value(value);
        if (value[0]) {
            print_theme_with_backend(value, "GTK3");
            return;
        }
    }
}