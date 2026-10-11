#include "ini_helpers.h"
#include "module_helpers.h"

static bool eq_icase_local(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

void cf_normalize_config_value(char *value) {
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

bool cf_read_ini_value(const char *path, const char *section, const char *key, char *out, size_t out_size) {
    if (!path || !key || !out || !out_size) return false;
    out[0] = '\0';
    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[512];
    char current_section[128] = "";

    for (;;) {
        size_t used = 0;
        bool invalid = false, saw_nonspace = false, section_header = false;
        int ch;
        while ((ch = fgetc(fp)) != EOF && ch != '\n') {
            if (!saw_nonspace && !isspace((unsigned char)ch)) {
                saw_nonspace = true;
                section_header = ch == '[';
            }
            if (ch == '\0') invalid = true;
            if (used < sizeof(line) - 1) line[used++] = (char)ch;
            else invalid = true;
        }
        if (ferror(fp) || (ch == EOF && used == 0)) break;
        line[used] = '\0';
        /* A section header changes scope even when it cannot be retained. */
        if (section_header) current_section[0] = '\0';
        if (invalid) continue;
        char *trimmed = cf_trim_spaces(line);
        if (!trimmed[0] || trimmed[0] == '#' || trimmed[0] == ';') continue;

        size_t len = strlen(trimmed);
        if (trimmed[0] == '[' && len > 2 && trimmed[len - 1] == ']') {
            trimmed[len - 1] = '\0';
            if (len - 2 >= sizeof(current_section)) continue;
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
        cf_normalize_config_value(right);
        if (!right[0] || strlen(right) >= out_size) continue;
        strcpy(out, right);
        fclose(fp);
        return true;
    }

    fclose(fp);
    return false;
}
