#include "../../cupidfetch.h"
#include "package_helpers.h"
#include <limits.h>

/* Read a whole record so continuation chunks cannot masquerade as fields. */
static int read_record(FILE *stream, char **line, size_t *capacity) {
    size_t length = 0;
    int ch;
    while ((ch = fgetc(stream)) != EOF && ch != '\n') {
        if (ch == '\0' || length >= 1024 * 1024) return -1;
        if (length + 1 >= *capacity) {
            if (*capacity > (size_t)-1 / 2) return -1;
            size_t next = *capacity ? *capacity * 2 : 256;
            char *grown = realloc(*line, next);
            if (!grown) return -1;
            *line = grown;
            *capacity = next;
        }
        (*line)[length++] = (char)ch;
    }
    if (ferror(stream)) return -1;
    if (ch == EOF && length == 0) return 0;
    if (*capacity == 0) {
        *line = malloc(1);
        if (!*line) return -1;
        *capacity = 1;
    }
    (*line)[length] = '\0';
    return 1;
}

static size_t split_fields(char *line, char **fields, size_t capacity) {
    size_t count = 0;
    while (*line) {
        while (isspace((unsigned char)*line)) line++;
        if (!*line) break;
        if (count == capacity) return capacity + 1;
        fields[count++] = line;
        while (*line && !isspace((unsigned char)*line)) line++;
        if (*line) *line++ = '\0';
    }
    return count;
}

static bool installed_status(char *line) {
    char *fields[3];
    /* Selection (install, hold, deinstall, ...) is independent of state. */
    return split_fields(line, fields, 3) == 3 && strcmp(fields[2], "installed") == 0;
}

bool cf_count_package_stream(FILE *stream, cf_package_format format, unsigned long *count_out) {
    if (!stream || !count_out || format < CF_PACKAGE_LINES || format > CF_PACKAGE_TOTAL) return false;
    char *line = NULL;
    size_t capacity = 0;
    unsigned long count = 0;
    bool header_seen = false, total_seen = false, ok = true;
    int result;
    while ((result = read_record(stream, &line, &capacity)) > 0) {
        char *trimmed = line;
        while (isspace((unsigned char)*trimmed)) trimmed++;
        if (!*trimmed) continue;
        bool include = false;
        switch (format) {
        case CF_PACKAGE_LINES:
            include = true;
            break;
        case CF_PACKAGE_DPKG_STATUS:
            if (strncmp(line, "Status:", 7) == 0) include = installed_status(line + 7);
            break;
        case CF_PACKAGE_DPKG_QUERY:
            include = installed_status(trimmed);
            break;
        case CF_PACKAGE_APK_DB:
            include = strncmp(line, "P:", 2) == 0 && line[2] != '\0';
            break;
        case CF_PACKAGE_SNAP_LIST: {
            /* snap list: https://snapcraft.io/docs/revisions */
            static const char *const header[] = {"Name", "Version", "Rev", "Tracking", "Publisher", "Notes"};
            char *fields[6];
            if (split_fields(trimmed, fields, 6) != 6) { ok = false; break; }
            if (!header_seen) {
                for (size_t i = 0; i < 6; i++) {
                    if (strcmp(fields[i], header[i]) != 0) ok = false;
                }
                header_seen = true;
            } else {
                include = true;
            }
            break;
        }
        case CF_PACKAGE_TOTAL: {
            char *end;
            if (total_seen || !isdigit((unsigned char)*trimmed)) { ok = false; break; }
            errno = 0;
            unsigned long value = strtoul(trimmed, &end, 10);
            if (errno) { ok = false; break; }
            while (isspace((unsigned char)*end)) end++;
            if (*end) { ok = false; break; }
            count = value;
            total_seen = true;
            break;
        }
        }
        if (!ok) break;
        if (include) {
            if (count == ULONG_MAX) { ok = false; break; }
            count++;
        }
    }
    free(line);
    if (!ok || result < 0 || ferror(stream) || (format == CF_PACKAGE_TOTAL && !total_seen)) return false;
    *count_out = count;
    return true;
}

bool cf_count_package_file(const char *path, cf_package_format format, unsigned long *count_out) {
    if (!path || !count_out) return false;
    FILE *stream = fopen(path, "r");
    if (!stream) return false;
    unsigned long count;
    bool ok = cf_count_package_stream(stream, format, &count);
    if (fclose(stream) != 0) ok = false;
    if (ok) *count_out = count;
    return ok;
}

bool cf_count_package_command(const char *command, cf_package_format format, unsigned long *count_out) {
    if (!command || !*command || !count_out) return false;
    FILE *stream = popen(command, "r");
    if (!stream) return false;
    unsigned long count;
    bool ok = cf_count_package_stream(stream, format, &count);
    /* Drain after parser failures so a child with more output can finish. */
    if (!ok && !ferror(stream)) while (fgetc(stream) != EOF) {}
    /* Zero alone means normal success for both POSIX pclose and _pclose. */
    if (pclose(stream) != 0) ok = false;
    if (ok) *count_out = count;
    return ok;
}

bool cf_count_package_directory(const char *path, cf_package_entry_kind kind, unsigned long *count_out) {
    if (!path || !count_out || (kind != CF_PACKAGE_DIRECTORIES && kind != CF_PACKAGE_REGULAR_FILES)) return false;
    DIR *dir = opendir(path);
    if (!dir) return false;
    unsigned long count = 0;
    bool ok = true;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (!entry) { if (errno) ok = false; break; }
        if (entry->d_name[0] == '.') continue;
        size_t base = strlen(path), name = strlen(entry->d_name);
        if (base > (size_t)-1 - name - 2) { ok = false; break; }
        char *full = malloc(base + name + 2);
        if (!full) { ok = false; break; }
        snprintf(full, base + name + 2, "%s/%s", path, entry->d_name);
        struct stat info;
#ifdef _WIN32
        int status = stat(full, &info);
#else
        int status = lstat(full, &info);
#endif
        free(full);
        if (status != 0) { ok = false; break; }
        bool include = kind == CF_PACKAGE_DIRECTORIES ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode);
        if (include) {
            if (count == ULONG_MAX) { ok = false; break; }
            count++;
        }
    }
    if (closedir(dir) != 0) ok = false;
    if (ok) *count_out = count;
    return ok;
}
