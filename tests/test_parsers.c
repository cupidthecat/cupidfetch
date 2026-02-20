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

int main(void) {
    if (test_parse_distro_def_line() != 0) return 1;
    if (test_parse_os_release_id_line() != 0) return 1;

    printf("test_parsers: OK\n");
    return 0;
}
