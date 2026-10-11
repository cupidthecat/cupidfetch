#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../src/modules/common/package_helpers.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static int check_stream(const char *text, cf_package_format format, bool expected_ok, unsigned long expected) {
    FILE *stream = tmpfile();
    if (!stream) return 1;
    if (fputs(text, stream) == EOF || fflush(stream) != 0) { fclose(stream); return 1; }
    rewind(stream);
    unsigned long count = 987;
    bool ok = cf_count_package_stream(stream, format, &count);
    fclose(stream);
    if (ok != expected_ok || count != (expected_ok ? expected : 987)) {
        fprintf(stderr, "package format %d: expected success=%d count=%lu, got success=%d count=%lu\n",
                format, expected_ok, expected_ok ? expected : 987, ok, count);
        return 1;
    }
    return 0;
}

static int test_records(void) {
    int failed = 0;
    failed |= check_stream("", CF_PACKAGE_LINES, true, 0);
    failed |= check_stream("\n \t\r\n", CF_PACKAGE_LINES, true, 0);
    failed |= check_stream("alpha\r\n\n beta\nlast", CF_PACKAGE_LINES, true, 3);
    char large[8192];
    memset(large, 'x', sizeof(large));
    large[sizeof(large) - 1] = '\0';
    failed |= check_stream(large, CF_PACKAGE_LINES, true, 1);
    large[sizeof(large) - 2] = '\n';
    failed |= check_stream(large, CF_PACKAGE_LINES, true, 1);
    /* A fake field at an old fgets boundary is still a description continuation. */
    memset(large, 'x', 511);
    strcpy(large + 511, "Status: install ok installed\nStatus: hold ok installed");
    failed |= check_stream(large, CF_PACKAGE_DPKG_STATUS, true, 1);
    memset(large, 'x', 511);
    strcpy(large + 511, "P:fake\nP:real");
    failed |= check_stream(large, CF_PACKAGE_APK_DB, true, 1);
    unsigned long untouched = 987;
    if (cf_count_package_stream(NULL, CF_PACKAGE_LINES, &untouched) || untouched != 987) failed = 1;
    return failed;
}

static int test_bounded_records(void) {
    FILE *stream = tmpfile();
    if (!stream) return 1;
    for (size_t i = 0; i < 1024 * 1024 + 1; ++i) {
        if (fputc('x', stream) == EOF) { fclose(stream); return 1; }
    }
    rewind(stream);
    unsigned long count = 987;
    bool accepted = cf_count_package_stream(stream, CF_PACKAGE_LINES, &count);
    fclose(stream);
    return accepted || count != 987;
}

static int test_dpkg(void) {
    int failed = 0;
    failed |= check_stream(
        "Package: installed\nStatus: install ok installed\n\n"
        "Package: held\nStatus: hold ok installed\n\n"
        "Package: residual\nStatus: deinstall ok config-files\n\n"
        "Package: unpacked\nStatus: install ok unpacked\n\n"
        "Package: pending-removal\nStatus: deinstall ok installed\n\n"
        " Status: install ok installed\nStatus: install ok installed-extra\n"
        "Status: install ok installed extra\nStatus: hold ok installed\r\n",
        CF_PACKAGE_DPKG_STATUS, true, 4);
    failed |= check_stream(
        "install ok installed\nhold ok installed\ndeinstall ok config-files\n"
        "install ok unpacked\ninstall ok not-installed\ninstall ok half-installed\n"
        "deinstall ok installed\ninstall reinstreq installed\n",
        CF_PACKAGE_DPKG_QUERY, true, 4);
    failed |= check_stream("Status: hold ok installed", CF_PACKAGE_DPKG_STATUS, true, 1);
    failed |= check_stream("hold ok installed", CF_PACKAGE_DPKG_QUERY, true, 1);
    failed |= check_stream("", CF_PACKAGE_DPKG_STATUS, true, 0);
    return failed;
}

static int test_snap(void) {
    const char *header = "Name  Version  Rev  Tracking  Publisher  Notes\n";
    int failed = check_stream(header, CF_PACKAGE_SNAP_LIST, true, 0);
    failed |= check_stream("", CF_PACKAGE_SNAP_LIST, true, 0);
    failed |= check_stream(
        "\nName\tVersion\tRev\tTracking\tPublisher\tNotes\r\n"
        "core 1.0 100 latest/stable canonical -\n"
        "local 2.0 x1 - - devmode\n\n"
        "editor 3.0 200 latest/stable example classic",
        CF_PACKAGE_SNAP_LIST, true, 3);
    failed |= check_stream("core 1.0 100 latest/stable canonical -\n", CF_PACKAGE_SNAP_LIST, false, 0);
    failed |= check_stream("Name Version Rev Publisher Notes\n", CF_PACKAGE_SNAP_LIST, false, 0);
    failed |= check_stream("Name Version Rev Tracking Publisher Notes\ninvalid\n", CF_PACKAGE_SNAP_LIST, false, 0);
    failed |= check_stream("Name Version Rev Tracking Publisher Notes\ncore 1 2 stable me - extra\n", CF_PACKAGE_SNAP_LIST, false, 0);
    /* Generic manager output never gets an unconditional header subtraction. */
    failed |= check_stream("one\ntwo\n", CF_PACKAGE_LINES, true, 2);
    return failed;
}

static int test_totals(void) {
    int failed = check_stream(" 42\r\n", CF_PACKAGE_TOTAL, true, 42);
    failed |= check_stream("0", CF_PACKAGE_TOTAL, true, 0);
    failed |= check_stream("-1\n", CF_PACKAGE_TOTAL, false, 0);
    failed |= check_stream("+1\n", CF_PACKAGE_TOTAL, false, 0);
    failed |= check_stream("1 packages\n", CF_PACKAGE_TOTAL, false, 0);
    failed |= check_stream("1\n2\n", CF_PACKAGE_TOTAL, false, 0);
    failed |= check_stream("", CF_PACKAGE_TOTAL, false, 0);
    failed |= check_stream("999999999999999999999999999999999999999999999999999999999999999999999", CF_PACKAGE_TOTAL, false, 0);
    return failed;
}

#ifdef __GLIBC__
typedef struct { bool sent; } failing_input;

static ssize_t read_then_fail(void *cookie, char *buffer, size_t size) {
    failing_input *input = cookie;
    if (input->sent) { errno = EIO; return -1; }
    const char text[] = "Status: install ok installed\n";
    if (size < sizeof(text) - 1) { errno = EIO; return -1; }
    memcpy(buffer, text, sizeof(text) - 1);
    input->sent = true;
    return sizeof(text) - 1;
}

static int test_read_error(void) {
    failing_input input = {false};
    cookie_io_functions_t io = {0};
    io.read = read_then_fail;
    FILE *stream = fopencookie(&input, "r", io);
    if (!stream) return 1;
    unsigned long count = 987;
    bool ok = cf_count_package_stream(stream, CF_PACKAGE_DPKG_STATUS, &count);
    int failed = ok || count != 987 || !ferror(stream);
    fclose(stream);
    return failed;
}
#endif

#ifndef _WIN32
static int write_fixture(const char *path, const char *text) {
    FILE *stream = fopen(path, "w");
    if (!stream) return 1;
    int failed = fputs(text, stream) == EOF;
    if (fclose(stream) != 0) failed = 1;
    return failed;
}

static int test_files_and_directories(void) {
    char root[] = "/tmp/cupidfetch-packages-XXXXXX";
    if (!mkdtemp(root)) return 1;
    char package[256], metadata[256], hidden[256], link[256], status[256], missing[256];
    snprintf(package, sizeof(package), "%s/example-1.0", root);
    snprintf(metadata, sizeof(metadata), "%s/ALPM_DB_VERSION", root);
    snprintf(hidden, sizeof(hidden), "%s/.hidden", root);
    snprintf(link, sizeof(link), "%s/link", root);
    snprintf(status, sizeof(status), "%s/status", root);
    snprintf(missing, sizeof(missing), "%s/missing", root);
    int failed = mkdir(package, 0700) != 0 || write_fixture(metadata, "9\n") || write_fixture(hidden, "hidden\n");
    if (symlink(package, link) != 0) failed = 1;
    unsigned long count = 987;
    if (!cf_count_package_directory(root, CF_PACKAGE_DIRECTORIES, &count) || count != 1) failed = 1;
    /* Slackware's package entries are files, so regular files must still count. */
    if (!cf_count_package_directory(root, CF_PACKAGE_REGULAR_FILES, &count) || count != 1) failed = 1;
    if (write_fixture(status, "Status: install ok installed\nStatus: hold ok installed\nStatus: deinstall ok config-files\n")) failed = 1;
    if (!cf_count_package_file(status, CF_PACKAGE_DPKG_STATUS, &count) || count != 2) failed = 1;
    if (write_fixture(status, "P:first\nV:1\n\nP:second")) failed = 1;
    if (!cf_count_package_file(status, CF_PACKAGE_APK_DB, &count) || count != 2) failed = 1;
    if (write_fixture(status, "")) failed = 1;
    if (!cf_count_package_file(status, CF_PACKAGE_DPKG_STATUS, &count) || count != 0) failed = 1;
    count = 987;
    if (cf_count_package_file(missing, CF_PACKAGE_DPKG_STATUS, &count) || count != 987) failed = 1;
    if (cf_count_package_directory(missing, CF_PACKAGE_DIRECTORIES, &count) || count != 987) failed = 1;
    if (cf_count_package_directory(status, CF_PACKAGE_DIRECTORIES, &count) || count != 987) failed = 1;
    if (unlink(status) != 0 || unlink(link) != 0 || unlink(hidden) != 0 || unlink(metadata) != 0 || rmdir(package) != 0 || rmdir(root) != 0) failed = 1;
    return failed;
}

static int test_commands(void) {
    unsigned long count = 987;
    int failed = !cf_count_package_command("printf 'one\\ntwo'", CF_PACKAGE_LINES, &count) || count != 2;
    if (!cf_count_package_command("printf ''", CF_PACKAGE_LINES, &count) || count != 0) failed = 1;
    count = 987;
    if (cf_count_package_command("printf 'partial\\n'; exit 7", CF_PACKAGE_LINES, &count) || count != 987) failed = 1;
    if (cf_count_package_command("printf ''; exit 7", CF_PACKAGE_LINES, &count) || count != 987) failed = 1;
    if (cf_count_package_command("printf '42\\n'; exit 1", CF_PACKAGE_TOTAL, &count) || count != 987) failed = 1;
    if (!cf_count_package_command("printf 'install ok installed\\nhold ok installed\\ndeinstall ok config-files\\n'", CF_PACKAGE_DPKG_QUERY, &count) || count != 2) failed = 1;
    if (!cf_count_package_command("printf 'Name Version Rev Tracking Publisher Notes\\ncore 1 2 stable canonical -\\n'", CF_PACKAGE_SNAP_LIST, &count) || count != 1) failed = 1;
    return failed;
}
#endif

int main(void) {
    int failed = test_records() | test_bounded_records() | test_dpkg() | test_snap() | test_totals();
#ifdef __GLIBC__
    failed |= test_read_error();
#endif
#ifndef _WIN32
    failed |= test_files_and_directories() | test_commands();
#endif
    if (failed) fprintf(stderr, "Package counting tests failed\n");
    else puts("Package counting tests passed");
    return failed ? 1 : 0;
}
