#include <stdio.h>

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

int main(void) {
    if (cf_convert_bytes_to_unit(8589934592ULL, 1) != 8589934592ULL ||
        cf_convert_bytes_to_unit(18446744073709551615ULL, 1) != 18446744073709551615ULL) {
        fprintf(stderr, "unit conversion must retain all 64 bits\n");
        return 1;
    }
    if (test_cpu_times()) return 1;
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
