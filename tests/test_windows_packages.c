#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../src/modules/common/windows_packages.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef bool (*parser)(FILE *, unsigned long *);
static int check_bytes(parser parse, const char *data, size_t size, bool expected, unsigned long count) {
    FILE *f = tmpfile(); if (!f) return 1;
    if (fwrite(data, 1, size, f) != size || fflush(f)) { fclose(f); return 1; }
    rewind(f); unsigned long result = 987;
    bool ok = parse(f, &result); fclose(f);
    if (ok != expected || result != (expected ? count : 987)) {
        fprintf(stderr, "Windows package fixture failed: expected %d/%lu, got %d/%lu: %.100s\n", expected, expected ? count : 987, ok, result, data);
        return 1;
    }
    return 0;
}
#define CHECK(fn, data, ok, n) check_bytes(fn, data, sizeof(data) - 1, ok, n)
static int choco(void) {
    int n = 0;
    const char *old = "choco list --local-only --limit-output --no-color";
    const char *current = "choco list --limit-output --no-color";
    if (strcmp(cf_choco_list_command(0), old) || strcmp(cf_choco_list_command(1), old) ||
        strcmp(cf_choco_list_command(2), current) || strcmp(cf_choco_list_command(99), current)) n = 1;
    n |= CHECK(cf_parse_choco_major, "2.5.1\r\n", true, 2);
    n |= CHECK(cf_parse_choco_major, "0.10.15", true, 0);
    n |= CHECK(cf_parse_choco_major, "3.0.0-beta.1\n", true, 3);
    n |= CHECK(cf_parse_choco_major, "2", false, 0);
    n |= CHECK(cf_parse_choco_major, "2.0.0 junk", false, 0);
    n |= CHECK(cf_parse_choco_major, "2.0.0\n3.0.0", false, 0);
    n |= CHECK(cf_parse_choco_major, "99999999999999999999999999999.0", false, 0);
    n |= CHECK(cf_parse_choco_major, "2.99999999999999999999999999999", false, 0);
    n |= CHECK(cf_parse_choco_major, "2.0-", false, 0);
    n |= CHECK(cf_count_choco_packages, "", true, 0);
    n |= CHECK(cf_count_choco_packages, "example|1.2.3\r\nexample.portable|2.0.0-beta", true, 2);
    n |= CHECK(cf_count_choco_packages, "Id|Version\nexample|1.0", false, 0);
    n |= CHECK(cf_count_choco_packages, "example|1.0\n1 packages installed.\n", false, 0);
    n |= CHECK(cf_count_choco_packages, "example|1.0\n\n", false, 0);
    n |= CHECK(cf_count_choco_packages, "example|", false, 0);
    n |= CHECK(cf_count_choco_packages, "example|1.0|extra", false, 0);
    n |= CHECK(cf_count_choco_packages, "example\r|1.0", false, 0);
    n |= CHECK(cf_count_choco_packages, "example|1.0\0evil|1.0", false, 0);
    return n;
}
static int scoop(void) {
    int n = 0;
    /* Synthetic fixtures derived from upstream export/list implementations. */
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[]}", true, 0);
    n |= CHECK(cf_count_scoop_export,
        "{\"buckets\":[{\"Name\":\"main\"}],\"apps\":[{\"Name\":\"example\",\"Version\":\"1.0\"},"
        "{\"Name\":\"example\",\"Version\":\"1.0\",\"Info\":\"Global install\"}]}", true, 2);
    n |= CHECK(cf_count_scoop_export,
        "\r\n{\"apps\":[{\"Version\":\"1\",\"N\\u0061me\":\"quote\\\" slash\\\\ \\ud83d\\udc31\","
        "\"metadata\":{\"apps\":[1,-2.5e+3,true,false,null]},\"Info\":\"Held package\"}]}\n", true, 1);
    n |= CHECK(cf_count_scoop_export,
        "{\"apps\":[{\"Name\":\"bad\",\"Version\":\"1\",\"Info\":\"Global install, Install failed\"},"
        "{\"Name\":\"empty\",\"Version\":\"\"}]}", true, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[{\"Name\":\"caf\xc3\xa9\",\"Version\":\"1\"}]}", true, 1);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[]}{}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"apps\":[]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":{},\"meta\":[]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"buckets\":[]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[{\"Name\":\"a\",\"Name\":\"b\",\"Version\":\"1\"}]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[{\"Name\":\"a\",\"Version\":\"1\",\"Version\":\"2\"}]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[{\"Name\":\"a\",\"Version\":\"1\",\"Info\":\"\",\"Info\":\"\"}]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[{\"Name\":\"a\",\"Version\":1}]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[{\"Name\":null,\"Version\":\"1\"}]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[{\"Name\":\"a\"}]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[{\"Name\":\"a\",\"Version\":\"1\"}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[null]}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":01}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":1.}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":1e+}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":truefalse}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":\"\\ud800\"}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":\"\\udc00\"}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":\"\\u0000\"}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":\"\\x20\"}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":\"\xc0\xaf\"}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":\"\xed\xa0\x80\"}", false, 0);
    n |= CHECK(cf_count_scoop_export, "{\"apps\":[],\"x\":\"a\nb\"}", false, 0);
    n |= CHECK(cf_count_scoop_export, "ERROR\n{\"apps\":[]}", false, 0);
    char deep[200]; strcpy(deep, "{\"apps\":[],\"x\":"); size_t at = strlen(deep);
    for (int i = 0; i < 40; ++i) deep[at++] = '[';
    deep[at++] = '0'; for (int i = 0; i < 40; ++i) deep[at++] = ']';
    deep[at++] = '}'; deep[at] = 0;
    n |= check_bytes(cf_count_scoop_export, deep, at, false, 0);
    return n;
}
static int winget(void) {
    int n = 0;
    /* Match the real-application command fixture, including separator width. */
    n |= CHECK(cf_count_winget_list, "Name     Id          Version\r\n----------------------------\r\nExample  Example.App 1.0\r\n", true, 1);
    n |= CHECK(cf_count_winget_list, "Name     Id          Version\r\n---------------------------\r\nExample  Example.App 1.0\r\n", false, 0);
    /* Columns derive from upstream table renderer; values are synthetic. */
    n |= CHECK(cf_count_winget_list, "Name  Id      Version\r\n---------------------\r\nAlpha App.One 1.0\r\nBeta  App.Two 2.0", true, 2);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version Source\n----------------------------\nAlpha App.One 1.0     winget\n", true, 1);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version Available Source\n--------------------------------------\nAlpha App.One 1.0     2.0       winget\nBeta  App.Two 2.0\n", true, 2);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version\n---------------------\n", false, 0);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version\n---------------------\nAlpha App.One\n", false, 0);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version\n---------------------\nAlpha App.One 1.0\n1 upgrades available.\n", false, 0);
    n |= CHECK(cf_count_winget_list, "Warning\nName  Id      Version\n---------------------\nAlpha App.One 1.0\n", false, 0);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version\n---------------------\nAlpha App.One 1.0\n\n", false, 0);
    n |= CHECK(cf_count_winget_list, "Nom   Id      Version\n---------------------\nAlpha App.One 1.0\n", false, 0);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version\n-------\nAlpha App.One 1.0\n", false, 0);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version Source Source\n-----------------------------------\nAlpha App.One 1.0     winget winget\n", false, 0);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version\n---------------------\nAlp\xc3\xa9 App.One 1.0\n", false, 0);
    n |= CHECK(cf_count_winget_list, "\x1b[0mName  Id      Version\n---------------------\nAlpha App.One 1.0", false, 0);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version\n---------------------\nName  Id      Version\n", false, 0);
    n |= CHECK(cf_count_winget_list, "Name  Id      Version\n---------------------\nAlpha App.One 1\r0\n", false, 0);
    return n;
}
#if defined(__GLIBC__) && !defined(_WIN32)
static ssize_t fail_read(void *cookie, char *buffer, size_t size) {
    (void)cookie; (void)buffer; (void)size; errno = EIO; return -1;
}
#endif
static int limits(void) {
    parser parsers[] = {cf_parse_choco_major, cf_count_choco_packages, cf_count_scoop_export, cf_count_winget_list};
    char *huge = malloc(1024u * 1024u + 2u); if (!huge) return 1;
    memset(huge, 'x', 1024u * 1024u + 1u); huge[1024u * 1024u + 1u] = 0;
    int n = 0;
    for (size_t i = 0; i < sizeof(parsers) / sizeof(parsers[0]); ++i) {
        n |= check_bytes(parsers[i], huge, 1024u * 1024u + 1u, false, 0);
        unsigned long out = 987;
        if (parsers[i](NULL, &out) || out != 987) n = 1;
        FILE *f = tmpfile(); if (!f) { n = 1; continue; }
        if (parsers[i](f, NULL)) n = 1;
        fclose(f);
#if defined(__GLIBC__) && !defined(_WIN32)
        cookie_io_functions_t io = {0}; io.read = fail_read;
        f = fopencookie(NULL, "r", io); if (!f) { n = 1; continue; }
        if (parsers[i](f, &out) || out != 987) n = 1;
        fclose(f);
#endif
    }
    free(huge); return n;
}
int main(void) { return choco() | scoop() | winget() | limits(); }
