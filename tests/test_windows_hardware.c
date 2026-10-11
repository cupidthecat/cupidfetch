/* Execute Windows branches with API fixtures on any host. No commands or
   hardware probes run. Windows builds use the real SDK declarations. */
#define CUPIDFETCH_H
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <errno.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#else
#define _WIN32 1
typedef uint32_t DWORD;
typedef struct { int Relationship; DWORD Size; unsigned char Processor[32]; }
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;
typedef struct { unsigned char ACLineStatus, BatteryFlag, BatteryLifePercent, SystemStatusFlag;
    DWORD BatteryLifeTime, BatteryFullLifeTime; } SYSTEM_POWER_STATUS;
#define RelationProcessorCore 0
#define ERROR_INSUFFICIENT_BUFFER 122
#define ALL_PROCESSOR_GROUPS 0xffff
#endif
#define GetLastError fixture_GetLastError
#define GetActiveProcessorCount fixture_GetActiveProcessorCount
#define GetLogicalProcessorInformationEx fixture_GetLogicalProcessorInformationEx
#define GetSystemPowerStatus fixture_GetSystemPowerStatus
static int api_mode, power_ok = 1;
static DWORD logical = 16;
static SYSTEM_POWER_STATUS power_fixture;
static char output[512];
static DWORD GetLastError(void) { return ERROR_INSUFFICIENT_BUFFER; }
static DWORD GetActiveProcessorCount(unsigned short group) {
    if (group != ALL_PROCESSOR_GROUPS) abort();
    return logical;
}
static int GetLogicalProcessorInformationEx(int relation,
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX out, DWORD *size) {
    if (relation != RelationProcessorCore) abort();
    if (api_mode == 1) { *size = 0; return 0; }
    if (!out) { *size = 8 * sizeof(*out); return 0; }
    if (api_mode == 2) return 0;
    memset(out, 0, *size);
    for (int i = 0; i < 8; i++) out[i].Size = sizeof(*out);
    if (api_mode == 3) out[1].Size = 0;
    if (api_mode == 4) out[1].Size = *size + 1;
    if (api_mode == 5) *size -= 1;
    if (api_mode == 6) out[1].Relationship = 3;
    if (api_mode == 7) *size += 1;
    return 1;
}
static int GetSystemPowerStatus(SYSTEM_POWER_STATUS *out) { *out = power_fixture; return power_ok; }
void print_info(const char *key, const char *fmt, int a, int b, ...) {
    (void)key; (void)a; (void)b;
    va_list args; va_start(args,b); vsnprintf(output,sizeof(output),fmt,args); va_end(args);
}
static char *fixture_getenv(const char *name) { (void)name; return "Fixture CPU"; }
#define getenv fixture_getenv
#include "../src/modules/hardware/cpu.c"
#undef getenv
#include "../src/modules/hardware/battery.c"
FILE *cf_capture_command(const char *cmd, unsigned int timeout, size_t limit) {
    (void)cmd; (void)timeout; (void)limit; return NULL;
}
void cf_trim_newline(char *s) { s[strcspn(s,"\r\n")] = 0; }
char *cf_trim_spaces(char *s) { return s; }
bool cf_contains_icase(const char *s, const char *n) { (void)s; (void)n; return false; }
void cf_format_duration_compact(unsigned long seconds, char *out, size_t size) {
    snprintf(out,size,"%lus",seconds);
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#x,output); exit(1); } } while(0)
int main(void) {
    get_cpu(); CHECK(strstr(output,"8C/16T"));
    for (api_mode = 1; api_mode <= 7; api_mode++) {
        get_cpu(); CHECK(strstr(output,"(16T)") && !strstr(output,"16C"));
    }
    api_mode = 0; logical = 128; get_cpu(); CHECK(strstr(output,"8C/128T"));
    logical = 0; get_cpu(); CHECK(strstr(output,"(8C)"));
    api_mode = 1; get_cpu(); CHECK(!strchr(output,'('));
    power_fixture = (SYSTEM_POWER_STATUS){0, 1, 60, 0, 7200, 10000};
    get_battery(); CHECK(!strcmp(output,"60% (Discharging, 7200s left)"));
    power_fixture.BatteryLifeTime = (DWORD)-1; get_battery(); CHECK(!strcmp(output,"60% (Discharging)"));
    power_fixture.BatteryFlag = 8; power_fixture.ACLineStatus = 1;
    get_battery(); CHECK(!strcmp(output,"60% (Charging)"));
    power_fixture.BatteryFlag = 1; get_battery(); CHECK(!strcmp(output,"60% (Not charging)"));
    power_fixture.BatteryFlag = 255; power_fixture.BatteryLifePercent = 255;
    get_battery(); CHECK(!strcmp(output,"Unknown (Unknown)"));
    power_fixture.BatteryFlag = 128; output[0] = 0; get_battery(); CHECK(!output[0]);
    power_ok = 0; get_battery(); CHECK(!output[0]);
    puts("Windows hardware API fixtures passed"); return 0;
}
