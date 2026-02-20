#include "../../cupidfetch.h"

void get_uptime() {
    FILE* uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file == NULL) {
        cupid_log(LogType_ERROR, "couldn't open /proc/uptime");
        return;
    }

    double uptime;
    if (fscanf(uptime_file, "%lf", &uptime) != 1) {
        cupid_log(LogType_ERROR, "couldn't read uptime from /proc/uptime");
        fclose(uptime_file);
        return;
    }
    fclose(uptime_file);

    int days = (int)uptime / (60 * 60 * 24);
    int hours = ((int)uptime % (60 * 60 * 24)) / (60 * 60);
    int minutes = ((int)uptime % (60 * 60)) / 60;

    print_info("Uptime", "%d days, %02d:%02d", 20, 30, days, hours, minutes);
}
