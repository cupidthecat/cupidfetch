#ifndef PACKAGE_HELPERS_H
#define PACKAGE_HELPERS_H

#include <stdbool.h>
#include <stdio.h>

typedef enum {
    CF_PACKAGE_LINES,
    CF_PACKAGE_DPKG_STATUS,
    CF_PACKAGE_DPKG_QUERY,
    CF_PACKAGE_APK_DB,
    CF_PACKAGE_SNAP_LIST,
    CF_PACKAGE_TOTAL
} cf_package_format;

typedef enum {
    CF_PACKAGE_DIRECTORIES,
    CF_PACKAGE_REGULAR_FILES
} cf_package_entry_kind;

/* Count nonblank logical records, including a final unterminated record.
 * Outputs are unchanged on failure. Streams remain owned by the caller.
 * SNAP_LIST requires the six-column C-locale header before any package rows.
 * DPKG_QUERY expects one ${Status} value per line, not package names. */
bool cf_count_package_stream(FILE *stream, cf_package_format format, unsigned long *count_out);
bool cf_count_package_file(const char *path, cf_package_format format, unsigned long *count_out);
bool cf_count_package_command(const char *command, cf_package_format format, unsigned long *count_out);
bool cf_count_package_directory(const char *path, cf_package_entry_kind kind, unsigned long *count_out);

#endif
