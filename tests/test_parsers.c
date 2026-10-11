#include <stdio.h>
#include <string.h>

#include "../src/modules/common/module_helpers.h"

static int test_parse_distro_def_line(void) {
    char shortname[64];
    char longname[128];
    char pkgcmd[128];

    if (!cf_parse_distro_def_line(
            "DISTRO(\"ubuntu\", \"Ubuntu\", \"\")",
            shortname,
            sizeof(shortname),
            longname,
            sizeof(longname),
            pkgcmd,
            sizeof(pkgcmd))) {
        fprintf(stderr, "parse_distro_def_line should parse valid DISTRO line\n");
        return 1;
    }

    if (strcmp(shortname, "ubuntu") != 0 || strcmp(longname, "Ubuntu") != 0 || strcmp(pkgcmd, "") != 0) {
        fprintf(stderr, "parse_distro_def_line parsed unexpected values\n");
        return 1;
    }

    if (cf_parse_distro_def_line("/* comment */", shortname, sizeof(shortname), longname, sizeof(longname), pkgcmd, sizeof(pkgcmd))) {
        fprintf(stderr, "parse_distro_def_line should reject comment lines\n");
        return 1;
    }

    return 0;
}

static int test_parse_os_release_id_line(void) {
    char id[128];

    if (!cf_parse_os_release_id_line("ID=Ubuntu\n", id, sizeof(id))) {
        fprintf(stderr, "parse_os_release_id_line should parse simple ID line\n");
        return 1;
    }
    if (strcmp(id, "ubuntu") != 0) {
        fprintf(stderr, "parse_os_release_id_line should lowercase distro ID\n");
        return 1;
    }

    if (!cf_parse_os_release_id_line("ID=\"Fedora\"\n", id, sizeof(id))) {
        fprintf(stderr, "parse_os_release_id_line should parse quoted ID line\n");
        return 1;
    }
    if (strcmp(id, "fedora") != 0) {
        fprintf(stderr, "parse_os_release_id_line should unquote and lowercase ID\n");
        return 1;
    }

    if (cf_parse_os_release_id_line("NAME=Ubuntu\n", id, sizeof(id))) {
        fprintf(stderr, "parse_os_release_id_line should reject non-ID lines\n");
        return 1;
    }

    return 0;
}

static int check_cpuinfo(const char *text, int expected_cores, int expected_threads) {
    FILE *fp = tmpfile();
    if (!fp) return 1;
    fputs(text, fp);
    rewind(fp);
    char model[80];
    int cores, threads;
    bool ok = cf_parse_cpuinfo(fp, model, sizeof(model), &cores, &threads);
    fclose(fp);
    if (!ok || cores != expected_cores || threads != expected_threads) {
        fprintf(stderr, "cpuinfo topology: expected %dC/%dT, got %dC/%dT\n",
                expected_cores, expected_threads, cores, threads);
        return 1;
    }
    return 0;
}

static int test_cpuinfo(void) {
    /* Both sockets reuse core IDs; SMT siblings must not add physical cores. */
    if (check_cpuinfo(
            "processor: 0\nphysical id: 0\ncore id: 0\ncpu cores: 2\n\n"
            "processor: 1\nphysical id: 0\ncore id: 0\ncpu cores: 2\n\n"
            "processor: 2\nphysical id: 0\ncore id: 1\ncpu cores: 2\n\n"
            "processor: 3\nphysical id: 0\ncore id: 1\ncpu cores: 2\n\n"
            "processor: 4\nphysical id: 1\ncore id: 0\ncpu cores: 2\n\n"
            "processor: 5\nphysical id: 1\ncore id: 0\ncpu cores: 2\n\n"
            "processor: 6\nphysical id: 1\ncore id: 1\ncpu cores: 2\n\n"
            "processor: 7\nphysical id: 1\ncore id: 1\ncpu cores: 2", 4, 8)) return 1;
    if (check_cpuinfo("processor: 0\nphysical id: 0\ncore id: 7\n\n"
                      "processor: 1\nphysical id: 0\ncore id: 7\n", 1, 2)) return 1;
    if (check_cpuinfo("processor: 0\n\nprocessor: 1\n", 0, 2)) return 1;
    if (check_cpuinfo("processor: 0\nphysical id: 0\ncore id: 0\n\n"
                      "processor: 1\nphysical id: 1\n", 0, 2)) return 1;
    if (check_cpuinfo("", 0, 0)) return 1;
    return 0;
}

#ifndef _WIN32
static int test_storage_mounts(void) {
    FILE *fp = tmpfile();
    if (!fp) return 1;
    fputs("/dev/a /media/My\\040Disk ext4 rw 0 0\n", fp);
    fputs("/dev/b /media/tab\\011slash\\134name ext4 rw 0 0\n", fp);
    /* A field larger than the old fscanf width must not desynchronize entries. */
    fputs("/dev/", fp);
    for (int i = 0; i < 600; ++i) fputc('a', fp);
    fputs(" /long ext4 rw 0 0\n/dev/c /last xfs rw 0 0\n", fp);
    rewind(fp);
    const char *device, *mountpoint, *type;
    int failed = 0;
    if (!cf_read_storage_mount(fp, &device, &mountpoint, &type) ||
        strcmp(mountpoint, "/media/My Disk") != 0 || strcmp(type, "ext4") != 0) failed = 1;
    if (!cf_read_storage_mount(fp, &device, &mountpoint, &type) ||
        strcmp(mountpoint, "/media/tab\tslash\\name") != 0) failed = 1;
    if (!cf_read_storage_mount(fp, &device, &mountpoint, &type) ||
        strlen(device) != 605 || strcmp(mountpoint, "/long") != 0) failed = 1;
    if (!cf_read_storage_mount(fp, &device, &mountpoint, &type) ||
        strcmp(device, "/dev/c") != 0 || strcmp(mountpoint, "/last") != 0 ||
        strcmp(type, "xfs") != 0) failed = 1;
    if (cf_read_storage_mount(fp, &device, &mountpoint, &type)) failed = 1;
    fclose(fp);
    if (failed) fprintf(stderr, "mount parsing should decode escapes and preserve whole records\n");
    return failed;
}
#endif

int main(void) {
    if (test_parse_distro_def_line() != 0) return 1;
    if (test_parse_os_release_id_line() != 0) return 1;

    if (test_cpuinfo() != 0) return 1;
#ifndef _WIN32
    if (test_storage_mounts() != 0) return 1;
#endif

    printf("test_parsers: OK\n");
    return 0;
}
