#include "windows_packages.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_LIMIT (1024u * 1024u)
#define DEPTH_LIMIT 32u
static bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
static bool letter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool space(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static char *read_all(FILE *f) {
    if (!f) return NULL;
    char *s = malloc(INPUT_LIMIT + 1u);
    if (!s) return NULL;
    size_t n = fread(s, 1, INPUT_LIMIT, f);
    if (ferror(f) || memchr(s, 0, n) || (n == INPUT_LIMIT && fgetc(f) != EOF) || ferror(f)) {
        free(s); return NULL;
    }
    s[n] = 0;
    return s;
}
static bool increment(unsigned long *n) { if (*n == ULONG_MAX) return false; ++*n; return true; }
static bool number_component(const char **p, unsigned long *value) {
    unsigned long n = 0;
    if (!digit((unsigned char)**p)) return false;
    do {
        unsigned long d = (unsigned long)(*(*p)++ - '0');
        if (n > (ULONG_MAX - d) / 10) return false;
        n = n * 10 + d;
    } while (digit((unsigned char)**p));
    *value = n; return true;
}
/* https://docs.chocolatey.org/en-us/guides/upgrading-to-chocolatey-v2-v6/
 * Select local-only flags by major version; never query old bare `list`. */
bool cf_parse_choco_major(FILE *f, unsigned long *out) {
    if (!out) return false;
    char *s = read_all(f); if (!s) return false;
    const char *p = s; unsigned long major = 0, ignored = 0;
    bool ok = number_component(&p, &major);
    unsigned parts = 1;
    while (ok && *p == '.') { ++p; ok = number_component(&p, &ignored); ++parts; }
    if (*p == '-') {
        ++p; const char *start = p;
        while (letter((unsigned char)*p) || digit((unsigned char)*p) || *p == '.' || *p == '-') ++p;
        if (p == start) ok = false;
    }
    if (*p == '\r') ++p;
    if (*p == '\n') ++p;
    ok = ok && parts >= 2 && parts <= 4 && !*p;
    if (ok) *out = major;
    free(s); return ok;
}
const char *cf_choco_list_command(unsigned long major) {
    return major < 2 ? "choco list --local-only --limit-output --no-color"
                     : "choco list --limit-output --no-color";
}
static char *line(char **cursor) {
    if (!**cursor) return NULL;
    char *s = *cursor, *end = strchr(s, '\n');
    if (end) { *end = 0; *cursor = end + 1; if (end > s && end[-1] == '\r') end[-1] = 0; }
    else *cursor = s + strlen(s);
    return s;
}
/* Machine-readable ID|version, no --include-headers.
 * https://docs.chocolatey.org/en-us/choco/commands/list/ */
bool cf_count_choco_packages(FILE *f, unsigned long *out) {
    if (!out) return false;
    char *s = read_all(f); if (!s) return false;
    char *cursor = s, *row; unsigned long count = 0; bool ok = true;
    while ((row = line(&cursor)) != NULL) {
        char *p = row;
        if (!letter((unsigned char)*p) && !digit((unsigned char)*p)) { ok = false; break; }
        while (letter((unsigned char)*p) || digit((unsigned char)*p) || *p == '_' || *p == '-' || *p == '.') ++p;
        if (*p++ != '|' || !digit((unsigned char)*p)) { ok = false; break; }
        while (letter((unsigned char)*p) || digit((unsigned char)*p) || *p == '.' || *p == '-' || *p == '+') ++p;
        if (*p || !increment(&count)) { ok = false; break; }
    }
    if (ok) *out = count;
    free(s); return ok;
}

typedef struct { const unsigned char *p; unsigned depth; } json;
static void ws(json *j) { while (space(*j->p)) ++j->p; }
static int hex(unsigned char c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool hex4(json *j, unsigned *n) {
    *n = 0;
    for (unsigned i = 0; i < 4; ++i) { int v = hex(*j->p); if (v < 0) return false; ++j->p; *n = *n * 16u + (unsigned)v; }
    return true;
}
static void emit_utf8(char **p, unsigned c) {
    if (c < 0x80) *(*p)++ = (char)c;
    else if (c < 0x800) { *(*p)++ = (char)(0xc0 | (c >> 6)); *(*p)++ = (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { *(*p)++ = (char)(0xe0 | (c >> 12)); *(*p)++ = (char)(0x80 | ((c >> 6) & 63)); *(*p)++ = (char)(0x80 | (c & 63)); }
    else { *(*p)++ = (char)(0xf0 | (c >> 18)); *(*p)++ = (char)(0x80 | ((c >> 12) & 63)); *(*p)++ = (char)(0x80 | ((c >> 6) & 63)); *(*p)++ = (char)(0x80 | (c & 63)); }
}
/* Decode strings, validate UTF-8 and surrogate pairs. Escaped NUL is rejected
 * deliberately because critical fields use C strings. */
static char *string(json *j) {
    if (*j->p != '"') return NULL;
    ++j->p;
    const unsigned char *end = j->p;
    while (*end && *end != '"') {
        if (*end == '\\') { ++end; if (!*end) return NULL; }
        ++end;
    }
    if (*end != '"') return NULL;
    char *s = malloc((size_t)(end - j->p) + 1), *d = s;
    if (!s) return NULL;
    while (*j->p && *j->p != '"') {
        unsigned c = *j->p++;
        if (c < 32) goto bad;
        if (c == '\\') {
            c = *j->p; if (!c) goto bad; ++j->p;
            switch (c) {
                case '"': case '\\': case '/': *d++ = (char)c; break;
                case 'b': *d++ = '\b'; break; case 'f': *d++ = '\f'; break;
                case 'n': *d++ = '\n'; break; case 'r': *d++ = '\r'; break; case 't': *d++ = '\t'; break;
                case 'u': {
                    if (!hex4(j, &c) || !c) goto bad;
                    if (c >= 0xd800 && c <= 0xdbff) {
                        if (*j->p != '\\') goto bad;
                        ++j->p;
                        if (*j->p != 'u') goto bad;
                        ++j->p;
                        unsigned low; if (!hex4(j, &low) || low < 0xdc00 || low > 0xdfff) goto bad;
                        c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
                    } else if (c >= 0xdc00 && c <= 0xdfff) goto bad;
                    emit_utf8(&d, c); break;
                }
                default: goto bad;
            }
        } else if (c < 128) *d++ = (char)c;
        else {
            unsigned remaining, minimum, value;
            if (c >= 0xc2 && c <= 0xdf) { remaining = 1; minimum = 0x80; value = c & 31; }
            else if (c >= 0xe0 && c <= 0xef) { remaining = 2; minimum = 0x800; value = c & 15; }
            else if (c >= 0xf0 && c <= 0xf4) { remaining = 3; minimum = 0x10000; value = c & 7; }
            else goto bad;
            while (remaining--) { unsigned next = *j->p; if (next < 0x80 || next > 0xbf) goto bad; ++j->p; value = (value << 6) | (next & 63); }
            if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) goto bad;
            emit_utf8(&d, value);
        }
    }
    if (*j->p != '"') goto bad;
    ++j->p; *d = 0; return s;
bad: free(s); return NULL;
}
static bool value(json *j);
static bool primitive(json *j) {
    const unsigned char *p = j->p;
    if (*p == 't' && strncmp((const char *)p, "true", 4) == 0) p += 4;
    else if (*p == 'f' && strncmp((const char *)p, "false", 5) == 0) p += 5;
    else if (*p == 'n' && strncmp((const char *)p, "null", 4) == 0) p += 4;
    else {
        if (*p == '-') ++p;
        if (*p == '0') ++p;
        else { if (*p < '1' || *p > '9') return false; while (digit(*p)) ++p; }
        if (*p == '.') { ++p; if (!digit(*p)) return false; while (digit(*p)) ++p; }
        if (*p == 'e' || *p == 'E') { ++p; if (*p == '+' || *p == '-') ++p; if (!digit(*p)) return false; while (digit(*p)) ++p; }
    }
    j->p = p; return true;
}
static bool value(json *j) {
    ws(j);
    if (*j->p == '"') { char *s = string(j); if (!s) return false; free(s); return true; }
    if (*j->p != '{' && *j->p != '[') return primitive(j);
    if (++j->depth > DEPTH_LIMIT) return false;
    bool object = *j->p++ == '{'; unsigned char end = object ? '}' : ']'; ws(j);
    if (*j->p != end) for (;;) {
        if (object) { char *key = string(j); if (!key) return false; free(key); ws(j); if (*j->p != ':') return false; ++j->p; }
        if (!value(j)) return false;
        ws(j); if (*j->p == end) break;
        if (*j->p != ',') return false;
        ++j->p; ws(j);
    }
    ++j->p; --j->depth; return true;
}
static bool nonblank(const char *s) { while (*s) { if (!space((unsigned char)*s)) return true; ++s; } return false; }
static bool failed_install(const char *s) {
    while (*s) {
        while (*s == ' ') ++s;
        const char *end = strchr(s, ','); if (!end) end = s + strlen(s);
        const char *trim = end; while (trim > s && trim[-1] == ' ') --trim;
        if ((size_t)(trim - s) == 14 && memcmp(s, "Install failed", 14) == 0) return true;
        s = *end ? end + 1 : end;
    }
    return false;
}
static bool app(json *j, unsigned long *count) {
    if (*j->p != '{' || ++j->depth > DEPTH_LIMIT) return false;
    ++j->p; ws(j); unsigned seen = 0; bool name = false, version = false, failed = false;
    if (*j->p != '}') for (;;) {
        char *key = string(j); if (!key) return false;
        unsigned bit = !strcmp(key, "Name") ? 1u : !strcmp(key, "Version") ? 2u : !strcmp(key, "Info") ? 4u : 0u;
        free(key); ws(j); if (*j->p != ':') return false; ++j->p; ws(j);
        if (bit) {
            if (seen & bit) return false;
            seen |= bit;
            char *s = string(j); if (!s) return false;
            if (bit == 1) name = nonblank(s);
            if (bit == 2) version = nonblank(s);
            if (bit == 4) failed = failed_install(s);
            free(s);
        } else if (!value(j)) return false;
        ws(j); if (*j->p == '}') break;
        if (*j->p != ',') return false;
        ++j->p; ws(j);
    }
    ++j->p; --j->depth;
    if (!name || !(seen & 2u)) return false;
    return failed || !version || increment(count);
}
/* https://raw.githubusercontent.com/ScoopInstaller/Scoop/master/libexec/scoop-export.ps1
 * https://raw.githubusercontent.com/ScoopInstaller/Scoop/master/libexec/scoop-list.ps1
 * Count only direct apps entries, never buckets or lexical key occurrences. */
bool cf_count_scoop_export(FILE *f, unsigned long *out) {
    if (!out) return false;
    char *s = read_all(f); if (!s) return false;
    json j = {(unsigned char *)s, 1}; unsigned long count = 0; bool seen = false, ok = false;
    ws(&j); if (*j.p != '{') goto done; ++j.p; ws(&j);
    if (*j.p != '}') for (;;) {
        char *key = string(&j); if (!key) goto done;
        bool apps = !strcmp(key, "apps"); free(key);
        ws(&j); if (*j.p != ':') goto done; ++j.p; ws(&j);
        if (apps) {
            if (seen || *j.p != '[') goto done;
            seen = true; ++j.p; ++j.depth; ws(&j);
            if (*j.p != ']') for (;;) {
                if (!app(&j, &count)) goto done;
                ws(&j); if (*j.p == ']') break;
                if (*j.p != ',') goto done;
                ++j.p; ws(&j);
            }
            ++j.p; --j.depth;
        } else if (!value(&j)) goto done;
        ws(&j); if (*j.p == '}') break;
        if (*j.p != ',') goto done;
        ++j.p; ws(&j);
    }
    ++j.p; ws(&j); ok = seen && !*j.p;
    if (ok) *out = count;
done: free(s); return ok;
}

/* https://raw.githubusercontent.com/microsoft/winget-cli/master/src/AppInstallerCLICore/TableOutput.cpp
 * No translated-header guessing, progress stripping, or byte/display-width
 * confusion: non-ASCII output is deliberately unsupported by this fallback. */
bool cf_count_winget_list(FILE *f, unsigned long *out) {
    if (!out) return false;
    char *s = read_all(f); if (!s) return false;
    bool ok = false; unsigned long count = 0;
    for (const unsigned char *p = (unsigned char *)s; *p; ++p)
        if ((*p < 32 && *p != '\r' && *p != '\n') || *p > 126) goto done;
    char *cursor = s, *header = line(&cursor); if (!header) goto done;
    size_t offsets[5], columns = 0; char *p = header;
    bool source_seen = false;
    const char *names[5] = {"Name", "Id", "Version", "Available", "Source"};
    while (*p) {
        if (columns == 5 || source_seen || *p == ' ') goto done;
        offsets[columns] = (size_t)(p - header);
        char *start = p; while (*p && *p != ' ') ++p;
        size_t length = (size_t)(p - start);
        const char *expected = names[columns];
        if (columns == 3 && length == 6 && !memcmp(start, "Source", 6)) expected = "Source";
        if (strlen(expected) != length || memcmp(start, expected, length)) goto done;
        if (!strcmp(expected, "Source")) source_seen = true;
        ++columns;
        while (*p == ' ') ++p;
    }
    if (columns < 3) goto done;
    char *separator = line(&cursor); if (!separator) goto done;
    size_t width = strlen(separator); if (width < strlen(header)) goto done;
    for (p = separator; *p; ++p) if (*p != '-') goto done;
    char *row;
    while ((row = line(&cursor)) != NULL) {
        size_t length = strlen(row);
        if (!length || length > width || strchr(row, '\r') || !strcmp(row, header)) goto done;
        for (size_t i = 0; i < columns; ++i) {
            size_t start = offsets[i], end = i + 1 < columns ? offsets[i + 1] : length;
            if (i && start <= length && row[start - 1] != ' ') goto done;
            if (end > length) end = length;
            bool present = false;
            for (size_t k = start; k < end; ++k) if (row[k] != ' ') present = true;
            if (i < 3 && !present) goto done;
        }
        if (!increment(&count)) goto done;
    }
    /* Upstream does not emit a header-only table for zero matches. */
    ok = count != 0; if (ok) *out = count;
done: free(s); return ok;
}
