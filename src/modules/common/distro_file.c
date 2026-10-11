#include "distro_file.h"

#ifndef _WIN32

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif

#ifdef CF_DISTRO_FILE_TESTING
static enum cf_distro_file_fault test_fault;
void cf_distro_file_test_fault(enum cf_distro_file_fault fault) {
    test_fault = fault;
}
#define FAIL_AT(stage) (test_fault == CF_DISTRO_FAULT_##stage)
#else
#define FAIL_AT(stage) 0
#endif

static bool valid_field(const char *s, size_t max) {
    size_t i;
    if (!s || !*s) return false;
    for (i = 0; i <= max && s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c == 127 || c == '"' || c == '\\') return false;
    }
    return i <= max;
}

static bool write_bytes(FILE *out, const char *s, size_t n) {
#ifdef CF_DISTRO_FILE_TESTING
    if (FAIL_AT(WRITE)) {
        /* Leave a partial temporary file so cleanup is exercised too. */
        if (n) (void)fwrite(s, 1, 1, out);
        return false;
    }
#endif
    return n == 0 || fwrite(s, 1, n, out) == n;
}

static bool has_no_extended_attributes(int fd) {
#ifdef __linux__
    ssize_t count = flistxattr(fd, NULL, 0);
    if (count >= 0) return count == 0;
    /* Filesystems without xattr support cannot have attributes to preserve.
     * Permission and other inspection failures must leave the file alone. */
    return errno == ENOTSUP || errno == EOPNOTSUPP;
#else
    (void)fd;
    return true;
#endif
}

bool cf_insert_auto_added_distro(const char *path, const char *id, const char *name) {
    static const char marker[] = "/* auto added */";
    static const char seed[] = "/* dpkg */\nDISTRO(\"ubuntu\", \"Ubuntu\", \"\")\n"
                               "// ... etc ...\n/* please don't remove this */\n"
                               "/* auto added */\n";
    char entry[256], *data = NULL, *temp = NULL;
    size_t length = 0, insertion = 0, path_len, entry_len;
    bool existed = false, found_marker = false, success = false, temp_exists = false;
    FILE *in = NULL, *out = NULL;
    int fd = -1;
    struct stat original, current;

    if (!path || !*path || !valid_field(id, 63) || !valid_field(name, 127)) return false;
    /* A fixed bound also makes the sibling-name allocation overflow-proof. */
    path_len = strlen(path);
    if (path_len > 4096) return false;
    int formatted = snprintf(entry, sizeof(entry), "DISTRO(\"%s\", \"%s\", \"\")", id, name);
    if (formatted < 0 || (size_t)formatted >= sizeof(entry)) return false;
    entry_len = (size_t)formatted;
    if (FAIL_AT(ALLOC)) goto done;
    data = malloc(CF_DISTRO_FILE_MAX_BYTES + 1u);
    if (FAIL_AT(TEMP_ALLOC)) goto done;
    temp = malloc(path_len + 64u);
    if (!data || !temp) goto done;

    if (lstat(path, &original) != 0) {
        if (errno != ENOENT) goto done;
    } else {
        /* Replacing a hard link would silently disconnect its other names. */
        if (!S_ISREG(original.st_mode) || original.st_nlink != 1) goto done;
        fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) goto done;
        if (fstat(fd, &current) != 0 || !S_ISREG(current.st_mode) ||
            current.st_dev != original.st_dev || current.st_ino != original.st_ino) goto done;
        if (!has_no_extended_attributes(fd)) goto done;
        in = fdopen(fd, "rb");
        if (!in) goto done;
        fd = -1;
        existed = true;
    }

    if (existed) {
        length = fread(data, 1, CF_DISTRO_FILE_MAX_BYTES + 1u, in);
        bool read_failed = ferror(in) || FAIL_AT(READ) || length > CF_DISTRO_FILE_MAX_BYTES;
        if (fclose(in) != 0) read_failed = true;
        in = NULL;
        if (read_failed || memchr(data, '\0', length)) goto done;
    } else {
        length = sizeof(seed) - 1;
        memcpy(data, seed, length);
    }
    data[length] = '\0';

    /* Scan complete lines, including ones longer than the old fgets buffer. */
    for (size_t start = 0; start < length;) {
        size_t end = start;
        while (end < length && data[end] != '\n') ++end;
        size_t content_end = end;
        if (content_end > start && data[content_end - 1] == '\r') --content_end;
        if (!found_marker) {
            for (size_t pos = start; pos + sizeof(marker) - 1 <= content_end; ++pos) {
                if (memcmp(data + pos, marker, sizeof(marker) - 1) == 0) {
                    found_marker = true;
                    insertion = end < length ? end + 1 : end;
                    break;
                }
            }
        } else if (content_end - start == entry_len &&
                   memcmp(data + start, entry, entry_len) == 0) {
            goto done;
        }
        start = end < length ? end + 1 : end;
    }
    if (!found_marker) insertion = length;
    size_t extra = entry_len + 1u + (found_marker ? 0u : sizeof(marker)) +
                   (insertion && data[insertion - 1] != '\n' ? 1u : 0u);
    if (length > CF_DISTRO_FILE_MAX_BYTES - extra) goto done;

    snprintf(temp, path_len + 64u, "%s.tmp.XXXXXX", path);
    fd = mkstemp(temp);
    if (fd < 0) goto done;
    temp_exists = true;
    /* This also refuses inherited ACLs or security labels on the new inode. */
    if (!has_no_extended_attributes(fd)) goto done;
    if (existed) {
        struct stat tmp_stat;
        if (fstat(fd, &tmp_stat) != 0) goto done;
        if ((tmp_stat.st_uid != original.st_uid || tmp_stat.st_gid != original.st_gid) &&
            fchown(fd, original.st_uid, original.st_gid) != 0) goto done;
    }
    out = fdopen(fd, "wb");
    if (!out) goto done;
    fd = -1;
    if (!write_bytes(out, data, insertion) ||
        (insertion && data[insertion - 1] != '\n' && !write_bytes(out, "\n", 1)) ||
        (!found_marker && !write_bytes(out, "/* auto added */\n", sizeof(marker))) ||
        !write_bytes(out, entry, entry_len) || !write_bytes(out, "\n", 1) ||
        !write_bytes(out, data + insertion, length - insertion)) goto done;
    if (fflush(out) != 0 || FAIL_AT(FLUSH)) goto done;
    /* Apply mode after writing: writes can clear set-ID bits. */
    if (existed && fchmod(fileno(out), original.st_mode & 07777) != 0) goto done;
    if (!has_no_extended_attributes(fileno(out))) goto done;
    if (fsync(fileno(out)) != 0 || FAIL_AT(SYNC)) goto done;
    int close_result = fclose(out);
    out = NULL;
    if (close_result != 0 || FAIL_AT(CLOSE) || FAIL_AT(REPLACE)) goto done;

    if (existed) {
        if (lstat(path, &current) != 0 || current.st_dev != original.st_dev ||
            current.st_ino != original.st_ino || current.st_size != original.st_size ||
            current.st_mtime != original.st_mtime || current.st_ctime != original.st_ctime ||
            current.st_mode != original.st_mode || current.st_uid != original.st_uid ||
            current.st_gid != original.st_gid ||
#ifdef __linux__
            current.st_mtim.tv_nsec != original.st_mtim.tv_nsec ||
            current.st_ctim.tv_nsec != original.st_ctim.tv_nsec ||
#endif
            !S_ISREG(current.st_mode) || current.st_nlink != 1) goto done;
        if (rename(temp, path) != 0) goto done;
        temp_exists = false;
    } else {
        /* Unlike rename, link refuses to overwrite a concurrently created path. */
        if (link(temp, path) != 0) goto done;
        if (unlink(temp) == 0) temp_exists = false;
    }
    success = true;

done:
    if (in) fclose(in);
    if (out) fclose(out);
    if (fd >= 0) {
        close(fd);
    }
    if (temp_exists) unlink(temp);
    free(temp);
    free(data);
    return success;
}

#else

/* Windows detection does not auto-add Linux distribution definitions. */
bool cf_insert_auto_added_distro(const char *path, const char *id, const char *name) {
    (void)path;
    (void)id;
    (void)name;
    return false;
}

#endif
