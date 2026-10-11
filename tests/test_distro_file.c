#include "../src/modules/common/distro_file.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif

static void put(const char *path, const void *data, size_t length) {
    FILE *out = fopen(path, "wb");
    assert(out);
    assert(fwrite(data, 1, length, out) == length);
    assert(fclose(out) == 0);
}

static void expect(const char *path, const void *expected, size_t length) {
    FILE *in = fopen(path, "rb");
    assert(in);
    char *actual = malloc(length + 1);
    assert(actual);
    assert(fread(actual, 1, length + 1, in) == length);
    assert(!ferror(in));
    assert(memcmp(actual, expected, length) == 0);
    assert(fclose(in) == 0);
    free(actual);
}

static void no_temporary_files(const char *directory) {
    DIR *dir = opendir(directory);
    assert(dir);
    struct dirent *entry;
    while ((entry = readdir(dir))) assert(strstr(entry->d_name, ".tmp.") == NULL);
    assert(closedir(dir) == 0);
}

int main(void) {
    char directory[] = "/tmp/cupidfetch-distro-test-XXXXXX";
    assert(mkdtemp(directory));
    char path[512], other[512];
    assert(snprintf(path, sizeof(path), "%s/distros.def", directory) > 0);
    assert(snprintf(other, sizeof(other), "%s/other.def", directory) > 0);
    const char original[] = "header\r\n/* auto added */\r\nexisting\r\nlast";
    const char updated[] = "header\r\n/* auto added */\r\nDISTRO(\"test-os\", \"Test OS\", \"\")\nexisting\r\nlast";
    put(path, original, sizeof(original) - 1);
    assert(chmod(path, 0640) == 0);
    struct stat before, after;
    assert(stat(path, &before) == 0);
    assert(cf_insert_auto_added_distro(path, "test-os", "Test OS"));
    expect(path, updated, sizeof(updated) - 1);
    assert(stat(path, &after) == 0);
    assert((after.st_mode & 07777) == (before.st_mode & 07777));
    assert(after.st_uid == before.st_uid && after.st_gid == before.st_gid);
    before = after;
    assert(!cf_insert_auto_added_distro(path, "test-os", "Test OS"));
    expect(path, updated, sizeof(updated) - 1);
    assert(stat(path, &after) == 0 && before.st_ino == after.st_ino);

    /* Every injected failure touches only fixtures in this owned directory. */
    for (int fault = CF_DISTRO_FAULT_ALLOC; fault <= CF_DISTRO_FAULT_REPLACE; ++fault) {
        put(path, original, sizeof(original) - 1);
        assert(stat(path, &before) == 0);
        cf_distro_file_test_fault((enum cf_distro_file_fault)fault);
        assert(!cf_insert_auto_added_distro(path, "test-os", "Test OS"));
        cf_distro_file_test_fault(CF_DISTRO_FAULT_NONE);
        expect(path, original, sizeof(original) - 1);
        assert(stat(path, &after) == 0 && before.st_ino == after.st_ino);
        no_temporary_files(directory);
    }

    assert(!cf_insert_auto_added_distro(NULL, "id", "Name"));
    assert(!cf_insert_auto_added_distro("", "id", "Name"));
    assert(!cf_insert_auto_added_distro(path, NULL, "Name"));
    assert(!cf_insert_auto_added_distro(path, "id", NULL));
    const char *bad[] = {"", "bad\"name", "bad\\name", "bad\nname", "bad\rname", "bad\tname"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!cf_insert_auto_added_distro(path, bad[i], "Name"));
        assert(!cf_insert_auto_added_distro(path, "id", bad[i]));
        expect(path, original, sizeof(original) - 1);
    }
    char oversized[129];
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    assert(!cf_insert_auto_added_distro(path, oversized, "Name"));
    assert(!cf_insert_auto_added_distro(path, "id", oversized));

#ifdef __linux__
    /* Attribute refusal preserves both the owned fixture and its metadata. */
    int attr_fd = open(path, O_RDWR);
    assert(attr_fd >= 0);
    const char attribute[] = "preserve me";
    if (fsetxattr(attr_fd, "user.cupidfetch_test", attribute, sizeof(attribute), 0) == 0) {
        assert(fstat(attr_fd, &before) == 0);
        assert(!cf_insert_auto_added_distro(path, "id", "Name"));
        expect(path, original, sizeof(original) - 1);
        assert(stat(path, &after) == 0 && before.st_ino == after.st_ino);
        char value[sizeof(attribute)];
        assert(fgetxattr(attr_fd, "user.cupidfetch_test", value, sizeof(value)) == sizeof(value));
        assert(memcmp(value, attribute, sizeof(value)) == 0);
        assert(fremovexattr(attr_fd, "user.cupidfetch_test") == 0);
        no_temporary_files(directory);
    } else {
        assert(errno == ENOTSUP || errno == EOPNOTSUPP || errno == EPERM);
        puts("user xattr fixture unavailable; attribute test skipped");
    }
    assert(close(attr_fd) == 0);
#endif

    /* A symlink, dangling symlink, hard link, directory and FIFO are refused. */
    assert(symlink(path, other) == 0);
    assert(!cf_insert_auto_added_distro(other, "id", "Name"));
    expect(path, original, sizeof(original) - 1);
    assert(lstat(other, &after) == 0 && S_ISLNK(after.st_mode));
    assert(unlink(other) == 0);
    assert(symlink("missing", other) == 0);
    assert(!cf_insert_auto_added_distro(other, "id", "Name"));
    assert(unlink(other) == 0);
    assert(link(path, other) == 0);
    assert(!cf_insert_auto_added_distro(path, "id", "Name"));
    expect(path, original, sizeof(original) - 1);
    assert(unlink(other) == 0);
    assert(!cf_insert_auto_added_distro(directory, "id", "Name"));
    assert(mkfifo(other, 0600) == 0);
    assert(!cf_insert_auto_added_distro(other, "id", "Name"));
    assert(unlink(other) == 0);

    const char binary[] = "header\0hidden\n";
    put(path, binary, sizeof(binary) - 1);
    assert(!cf_insert_auto_added_distro(path, "id", "Name"));
    expect(path, binary, sizeof(binary) - 1);
    char *large = malloc(CF_DISTRO_FILE_MAX_BYTES + 1u);
    assert(large);
    memset(large, 'x', CF_DISTRO_FILE_MAX_BYTES + 1u);
    put(path, large, CF_DISTRO_FILE_MAX_BYTES + 1u);
    assert(!cf_insert_auto_added_distro(path, "id", "Name"));
    expect(path, large, CF_DISTRO_FILE_MAX_BYTES + 1u);
    put(path, large, CF_DISTRO_FILE_MAX_BYTES);
    assert(!cf_insert_auto_added_distro(path, "id", "Name"));
    expect(path, large, CF_DISTRO_FILE_MAX_BYTES);
    free(large);

    /* Long lines are preserved, and marker matching never crosses a newline. */
    char long_line[4096];
    memset(long_line, 'x', sizeof(long_line));
    put(path, long_line, sizeof(long_line));
    assert(cf_insert_auto_added_distro(path, "id", "Name"));
    FILE *in = fopen(path, "rb");
    assert(in);
    char copied[4096];
    assert(fread(copied, 1, sizeof(copied), in) == sizeof(copied));
    assert(memcmp(copied, long_line, sizeof(copied)) == 0);
    assert(fclose(in) == 0);
    assert(!cf_insert_auto_added_distro(path, "id", "Name"));
    const char missing_marker[] = "header";
    const char appended[] = "header\n/* auto added */\nDISTRO(\"id\", \"Name\", \"\")\n";
    put(path, missing_marker, sizeof(missing_marker) - 1);
    assert(cf_insert_auto_added_distro(path, "id", "Name"));
    expect(path, appended, sizeof(appended) - 1);
    const char marker_only[] = "/* auto added */";
    const char marker_added[] = "/* auto added */\nDISTRO(\"id\", \"Name\", \"\")\n";
    put(path, marker_only, sizeof(marker_only) - 1);
    assert(cf_insert_auto_added_distro(path, "id", "Name"));
    expect(path, marker_added, sizeof(marker_added) - 1);
    put(path, "", 0);
    assert(cf_insert_auto_added_distro(path, "id", "Name"));
    expect(path, marker_added, sizeof(marker_added) - 1);

    /* New-file failures must not leave a destination or temporary file. */
    assert(unlink(path) == 0);
    for (int fault = CF_DISTRO_FAULT_WRITE; fault <= CF_DISTRO_FAULT_REPLACE; ++fault) {
        cf_distro_file_test_fault((enum cf_distro_file_fault)fault);
        assert(!cf_insert_auto_added_distro(path, "id", "Name"));
        cf_distro_file_test_fault(CF_DISTRO_FAULT_NONE);
        assert(lstat(path, &after) != 0);
        no_temporary_files(directory);
    }
    assert(cf_insert_auto_added_distro(path, "id", "Name"));
    assert(!cf_insert_auto_added_distro(path, "id", "Name"));
    no_temporary_files(directory);
    assert(unlink(path) == 0);
    assert(snprintf(other, sizeof(other), "%s/missing/distros.def", directory) > 0);
    assert(!cf_insert_auto_added_distro(other, "id", "Name"));
    no_temporary_files(directory);
    assert(rmdir(directory) == 0);
    puts("distro file tests passed");
    return 0;
}
