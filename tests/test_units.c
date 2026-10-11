#include <stdio.h>
#include <limits.h>
#include <string.h>

#include "../src/modules/common/module_helpers.h"

static int test_cpu_times(void) {
    unsigned long long idle, total;
    if (!cf_parse_proc_stat_cpu_line("cpu 100 20 30 400 10 2 3 5 60 10", &idle, &total) ||
        idle != 410 || total != 570) {
        fprintf(stderr, "guest CPU time must not be counted twice\n");
        return 1;
    }
    if (!cf_parse_proc_stat_cpu_line("cpu 100 20 30 400", &idle, &total) ||
        idle != 400 || total != 550) {
        fprintf(stderr, "CPU time parser must accept older four-field records\n");
        return 1;
    }
    if (cf_parse_proc_stat_cpu_line("cpu0 1 2 3 4", &idle, &total) ||
        cf_parse_proc_stat_cpu_line("cpu 1 2", &idle, &total) ||
        cf_parse_proc_stat_cpu_line(NULL, &idle, &total)) {
        fprintf(stderr, "CPU time parser must reject invalid aggregate records\n");
        return 1;
    }
    return 0;
}

static int test_storage_usage(void) {
    struct { unsigned long long total, free; unsigned long unit; const char *expected; } cases[] = {
        {1900000000ULL, 990000000ULL, 1000000000UL, "0.91/1.90 GB (47%)"},
        {1900000000ULL, 1800000000ULL, 1000000000UL, "0.10/1.90 GB (5%)"},
        {3, 2, 1, "1/3 B (33%)"},
        {500, 250, 1000000000UL, "250/500 B (50%)"},
        {5000000000ULL, 1000000000ULL, 1, "4000000000/5000000000 B (80%)"},
        {ULLONG_MAX, 0, 1, "18446744073709551615/18446744073709551615 B (100%)"},
        {ULLONG_MAX, 1, 1, "18446744073709551614/18446744073709551615 B (99%)"},
        {ULLONG_MAX, ULLONG_MAX, 1, "0/18446744073709551615 B (0%)"},
    };
    char out[256];
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        if (!cf_format_storage_usage(cases[i].total, cases[i].free, cases[i].unit,
                                     "GB", out, sizeof(out)) || strcmp(out, cases[i].expected)) {
            fprintf(stderr, "storage case %zu: %s\n", i, out);
            return 1;
        }
    }
    char label[128];
    memset(label, 'x', sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    if (!cf_format_storage_usage(2000000000000ULL, 1000000000000ULL,
                                 2, label, out, sizeof(out)) || !strstr(out, label)) {
        fprintf(stderr, "long unit labels must fit\n");
        return 1;
    }
    if (cf_format_storage_usage(1, 0, 1, "B", NULL, 1) ||
        cf_format_storage_usage(1, 0, 1, "B", out, 0)) return 1;
    if (cf_format_storage_usage(0, 0, 1, "B", out, sizeof(out)) ||
        cf_format_storage_usage(1, 2, 1, "B", out, sizeof(out)) ||
        cf_format_storage_usage(1, 0, 0, "B", out, sizeof(out)) ||
        cf_format_storage_usage(1, 0, 1, NULL, out, sizeof(out)) ||
        cf_format_storage_usage(1, 0, 1, "B", out, 2) || out[0] != '\0') {
        fprintf(stderr, "invalid storage input must fail cleanly\n");
        return 1;
    }
    return 0;
}

int main(void) {
    if (test_cpu_times() || test_storage_usage()) return 1;
    if (cf_convert_bytes_to_unit(8589934592ULL, 1) != 8589934592ULL ||
        cf_convert_bytes_to_unit(18446744073709551615ULL, 1) != 18446744073709551615ULL) {
        fprintf(stderr, "unit conversion must retain all 64 bits\n");
        return 1;
    }
    if (cf_convert_bytes_to_unit(2048ULL, 1024UL) != 2UL) {
        fprintf(stderr, "convert 2048/1024 should be 2\n");
        return 1;
    }

    if (cf_convert_bytes_to_unit(999ULL, 1000UL) != 0UL) {
        fprintf(stderr, "convert 999/1000 should be 0\n");
        return 1;
    }

    if (cf_convert_bytes_to_unit(1000ULL, 0UL) != 0UL) {
        fprintf(stderr, "convert with unit_size=0 should be 0\n");
        return 1;
    }

    printf("test_units: OK\n");
    return 0;
}
