#define _POSIX_C_SOURCE 200809L

#include "cupidconf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fnmatch.h>

/* Helper: Check if a key matches a wildcard pattern. */
static int match_wildcard(const char *pattern, const char *key) {
    return fnmatch(pattern, key, 0) == 0;
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

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline if present */
        line[strcspn(line, "\n")] = '\0';

        /* Trim whitespace */
        char *trimmed = trim_whitespace(line);

        /* Skip empty lines and comments (lines starting with '#' or ';') */
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
            continue;

        /* Find the '=' separator */
        char *equal_sign = strchr(trimmed, '=');
        if (!equal_sign) {
            /* No '=' on this line; skip or handle error */
            continue;
        }

        /* Split key and value. */
        *equal_sign = '\0';
        char *key = trim_whitespace(trimmed);
        char *value = trim_whitespace(equal_sign + 1);

        if (add_entry(conf, key, value) != 0) {
            // On allocation failure, free everything and return NULL.
            cupidconf_free(conf);
            fclose(fp);
            return NULL;
        }
    }

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