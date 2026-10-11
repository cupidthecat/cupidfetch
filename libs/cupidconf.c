#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "cupidconf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int wildcard_match_impl(const char *pattern, const char *text) {
    if (!pattern || !text) return 0;
    const char *star_tail = NULL, *retry = NULL;
    while (*text) {
        if (*pattern == '*') {
            while (*pattern == '*') pattern++;
            if (!*pattern) return 1;
            star_tail = pattern;
            retry = text;
        } else if (*pattern == '?' || *pattern == *text) {
            pattern++;
            text++;
        } else if (star_tail) {
            /* Extend the most recent star by one byte. No recursive search
             * tree or stack is needed; retry only advances through text. */
            pattern = star_tail;
            text = ++retry;
        } else {
            return 0;
        }
    }
    while (*pattern == '*') pattern++;
    return *pattern == '\0';
}

/* Helper: Check if a key matches a wildcard pattern. */
static int match_wildcard(const char *pattern, const char *key) {
    return wildcard_match_impl(pattern, key) == 1;
}

/* Helper: Trim leading and trailing whitespace in place. */
static char *trim_whitespace(char *str) {
    char *end;

    /* Trim leading space */
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0)  /* All spaces? */
        return str;

    /* Trim trailing space */
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    /* Write new null terminator */
    *(end+1) = '\0';

    return str;
}

/* Create a new entry and add it to the linked list. */
static int add_entry(cupidconf_t *conf, const char *key, const char *value) {
    cupidconf_entry *entry = malloc(sizeof(cupidconf_entry));
    if (!entry) return -1;

    entry->key = strdup(key);
    entry->value = strdup(value);
    entry->next = NULL;

    if (!entry->key || !entry->value) {
        free(entry->key);
        free(entry->value);
        free(entry);
        return -1;
    }

    // Add to the front of the list for simplicity.
    entry->next = conf->entries;
    conf->entries = entry;
    return 0;
}

cupidconf_t *cupidconf_load(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("cupidconf_load: fopen");
        return NULL;
    }

    cupidconf_t *conf = malloc(sizeof(cupidconf_t));
    if (!conf) {
        fclose(fp);
        return NULL;
    }
    conf->entries = NULL;

    /* Keep a bounded buffer, but consume each physical line in full. A long
     * comment or value must never turn its suffix into a separate setting. */
    char line[1024];
    char section[1024] = "";
    int ch;
    do {
        size_t length = 0;
        int invalid = 0;
        while ((ch = fgetc(fp)) != EOF && ch != '\n') {
            if (ch == '\0' || length == sizeof(line) - 1) {
                invalid = 1;
            } else {
                line[length++] = (char)ch;
            }
        }
        if (ferror(fp)) {
            cupidconf_free(conf);
            fclose(fp);
            return NULL;
        }
        line[length] = '\0';
        if (invalid) continue;

        char *trimmed = trim_whitespace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
            continue;

        if (trimmed[0] == '[') {
            char *close = strchr(trimmed + 1, ']');
            if (!close) continue;
            char *tail = trim_whitespace(close + 1);
            if (*tail && *tail != '#' && *tail != ';') continue;
            *close = '\0';
            char *name = trim_whitespace(trimmed + 1);
            memcpy(section, name, strlen(name) + 1);
            continue;
        }

        char *equal_sign = strchr(trimmed, '=');
        if (!equal_sign) continue;

        *equal_sign = '\0';
        char *key = trim_whitespace(trimmed);
        char *value = trim_whitespace(equal_sign + 1);
        if (!*key) continue;

        /* Dotted keys are absolute, including when mixed with INI sections. */
        char scoped_key[sizeof(section) + sizeof(line)];
        if (*section && !strchr(key, '.')) {
            snprintf(scoped_key, sizeof(scoped_key), "%s.%s", section, key);
            key = scoped_key;
        }
        if (add_entry(conf, key, value) != 0) {
            cupidconf_free(conf);
            fclose(fp);
            return NULL;
        }
    } while (ch != EOF);

    fclose(fp);
    return conf;
}

const char *cupidconf_get(cupidconf_t *conf, const char *key) {
    if (!conf || !key)
        return NULL;

    for (cupidconf_entry *entry = conf->entries; entry != NULL; entry = entry->next) {
        if (match_wildcard(key, entry->key))
            return entry->value;
    }
    return NULL;
}

char **cupidconf_get_list(cupidconf_t *conf, const char *key, int *count) {
    if (!conf || !key || !count)
        return NULL;

    int cnt = 0;
    for (cupidconf_entry *entry = conf->entries; entry != NULL; entry = entry->next) {
        if (match_wildcard(key, entry->key))
            cnt++;
    }

    if (cnt == 0) {
        *count = 0;
        return NULL;
    }

    char **values = malloc(sizeof(char *) * cnt);
    if (!values) {
        *count = 0;
        return NULL;
    }

    int idx = 0;
    for (cupidconf_entry *entry = conf->entries; entry != NULL; entry = entry->next) {
        if (match_wildcard(key, entry->key))
            values[idx++] = entry->value;
    }

    *count = cnt;
    return values;
}

void cupidconf_free(cupidconf_t *conf) {
    if (!conf)
        return;
    cupidconf_entry *entry = conf->entries;
    while (entry) {
        cupidconf_entry *next = entry->next;
        free(entry->key);
        free(entry->value);
        free(entry);
        entry = next;
    }
    free(conf);
}
