#ifndef CUPIDCONF_H
#define CUPIDCONF_H

#ifdef __cplusplus
extern "C" {
#endif

// A single key/value entry.
typedef struct cupidconf_entry {
    char *key;
    char *value;
    struct cupidconf_entry *next;
} cupidconf_entry;

// The configuration object.
typedef struct cupidconf {
    cupidconf_entry *entries;
} cupidconf_t;

/**
 * Loads a configuration file from the given filename.
 *
 * The file should contain lines of the form:
 *    key = value
 * Lines starting with '#' or ';' are ignored (as comments).
 *
 * Returns a pointer to a cupidconf_t on success, or NULL on error.
 */
cupidconf_t *cupidconf_load(const char *filename);

/**
 * Returns the value for the given key.
 * If the key contains a wildcard ('*'), it returns the first value that matches the pattern.
 *
 * @param conf  The config object returned by cupidconf_load().
 * @param key   The key to look for (may contain wildcards).
 *
 * @return      The corresponding value string (read-only), or NULL if not found.
 */
const char *cupidconf_get(cupidconf_t *conf, const char *key);

/**
 * Returns all values for the given key.
 * If the key contains a wildcard ('*'), it returns all values that match the pattern.
 *
 * @param conf   The config object.
 * @param key    The key to search (may contain wildcards).
 * @param count  Pointer to an int that will receive the number of values found.
 *
 * @return       An array of char pointers (read-only) that the caller must free,
 *               or NULL if the key is not found.
 */
char **cupidconf_get_list(cupidconf_t *conf, const char *key, int *count);

/**
 * Frees the configuration object and all associated memory.
 */
void cupidconf_free(cupidconf_t *conf);

#ifdef __cplusplus
}
#endif

#endif // CUPIDCONF_H