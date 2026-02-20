#include <stdio.h>

#include "../src/modules/common/module_helpers.h"

int main(void) {
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
