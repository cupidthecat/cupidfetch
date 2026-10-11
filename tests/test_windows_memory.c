#include "../src/cupidfetch.h"
#include "../src/modules/common/module_helpers.h"

#ifndef _WIN32
#define _WIN32 1
typedef struct {
    unsigned long dwLength;
    unsigned long long ullTotalPhys, ullAvailPhys;
} MEMORYSTATUSEX;
#endif

static unsigned long long total = 8589934592ULL, available = 5368709120ULL;
static int api_success = 1;
static char output[512];
struct CupidConfig g_userConfig;

static int fixture_memory(MEMORYSTATUSEX *memory) {
    if (memory->dwLength != sizeof(*memory)) abort();
    memory->ullTotalPhys = total;
    memory->ullAvailPhys = available;
    return api_success;
}
#define GlobalMemoryStatusEx fixture_memory
#include "../src/modules/hardware/memory.c"

void print_info(const char *key, const char *format, int a, int b, ...) {
    (void)key; (void)a; (void)b;
    va_list args;
    va_start(args, b);
    vsnprintf(output, sizeof(output), format, args);
    va_end(args);
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x, output); return 1; } } while (0)
int main(void) {
    g_userConfig.memory_unit_size = 1;
    strcpy(g_userConfig.memory_unit, "B");
    get_available_memory();
    CHECK(!strcmp(output, "3221225472 B / 8589934592 B"));
    available = 0; get_available_memory();
    CHECK(!strcmp(output, "8589934592 B / 8589934592 B"));
    total = available = 18446744073709551615ULL; get_available_memory();
    CHECK(!strcmp(output, "0 B / 18446744073709551615 B"));
    total = 8589934592ULL; available = 5368709120ULL;
    g_userConfig.memory_unit_size = 1048576;
    strcpy(g_userConfig.memory_unit, "MiB"); get_available_memory();
    CHECK(!strcmp(output, "3072 MiB / 8192 MiB"));
    output[0] = 0; api_success = 0; get_available_memory(); CHECK(!output[0]);
    puts("Windows memory fixtures passed");
    return 0;
}
