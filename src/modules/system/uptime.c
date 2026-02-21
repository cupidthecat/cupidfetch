#include "../../cupidfetch.h"
#ifndef _WIN32
#include <sys/sysinfo.h>
#endif

void get_uptime() {
#ifdef _WIN32
    unsigned long long uptime_ms = GetTickCount64();
    unsigned long long uptime = uptime_ms / 1000ULL;
#else
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        cupid_log(LogType_ERROR, "couldn't read uptime from sysinfo");
        return;
    }

    long uptime = info.uptime;
#endif

    int days = (int)uptime / (60 * 60 * 24);
    int hours = ((int)uptime % (60 * 60 * 24)) / (60 * 60);
    int minutes = ((int)uptime % (60 * 60)) / 60;

    print_info("Uptime", "%d days, %02d:%02d", 20, 30, days, hours, minutes);
}
