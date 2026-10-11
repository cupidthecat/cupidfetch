#ifndef CF_INI_HELPERS_H
#define CF_INI_HELPERS_H
#include <stdbool.h>
#include <stddef.h>

void cf_normalize_config_value(char *value);
bool cf_read_ini_value(const char *path, const char *section, const char *key,
                       char *out, size_t out_size);
#endif
