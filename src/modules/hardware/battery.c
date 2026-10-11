#include "../../cupidfetch.h"
#include "../common/module_helpers.h"
#include "../common/battery_helpers.h"

#ifndef _WIN32
static bool read_battery_number(const char *name, const char *field, unsigned long *value) {
    char path[512], line[64], *end;
    if (!cf_build_power_supply_path(path, sizeof(path), name, field) ||
        !cf_read_first_line(path, line, sizeof(line)) ||
        strlen(line) == sizeof(line) - 1 || !isdigit((unsigned char)line[0])) return false;
    errno = 0;
    unsigned long parsed = strtoul(line, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    if (errno || *end) return false;
    *value = parsed;
    return true;
}

static cf_battery_status read_battery_status(const char *name) {
    char path[512], status[64];
    if (!cf_build_power_supply_path(path, sizeof(path), name, "/status") ||
        !cf_read_first_line(path, status, sizeof(status))) return CF_BATTERY_UNKNOWN;
    if (!strcmp(status, "Charging")) return CF_BATTERY_CHARGING;
    if (!strcmp(status, "Discharging")) return CF_BATTERY_DISCHARGING;
    if (!strcmp(status, "Full")) return CF_BATTERY_FULL;
    if (!strcmp(status, "Not charging")) return CF_BATTERY_IDLE;
    return CF_BATTERY_UNKNOWN;
}
#endif

void get_battery() {
    cf_battery_result result = {0};
#ifdef _WIN32
    SYSTEM_POWER_STATUS power;
    if (!GetSystemPowerStatus(&power) ||
        (power.BatteryFlag != 255 && (power.BatteryFlag & 128))) return;
    result.has_percent = power.BatteryLifePercent <= 100;
    result.percent = power.BatteryLifePercent;
    if (power.BatteryFlag != 255 && (power.BatteryFlag & 8)) {
        result.status = CF_BATTERY_CHARGING;
    } else if (power.ACLineStatus == 0 && power.BatteryFlag != 255) {
        result.status = CF_BATTERY_DISCHARGING;
        if (power.BatteryLifeTime != (DWORD)-1) {
            result.has_time = true;
            result.seconds = power.BatteryLifeTime;
        }
    } else if (power.ACLineStatus == 1 && power.BatteryFlag != 255) {
        result.status = CF_BATTERY_IDLE;
    }
#else
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) return;
    cf_battery_total total = {0};
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[512], type[64];
        if (!cf_build_power_supply_path(path, sizeof(path), entry->d_name, "/type") ||
            !cf_read_first_line(path, type, sizeof(type)) || strcmp(type, "Battery")) continue;
        char scope[64];
        if (cf_build_power_supply_path(path, sizeof(path), entry->d_name, "/scope") &&
            cf_read_first_line(path, scope, sizeof(scope)) && !strcmp(scope, "Device")) continue;
        unsigned long present;
        if (read_battery_number(entry->d_name, "/present", &present) && present == 0) continue;
        cf_battery_sample sample = {0};
        sample.status = read_battery_status(entry->d_name);
        sample.has_capacity = read_battery_number(entry->d_name, "/capacity", &sample.capacity);
        sample.has_energy = read_battery_number(entry->d_name, "/energy_now", &sample.energy_now) &&
            read_battery_number(entry->d_name, "/energy_full", &sample.energy_full);
        sample.has_power = read_battery_number(entry->d_name, "/power_now", &sample.power);
        sample.has_charge = read_battery_number(entry->d_name, "/charge_now", &sample.charge_now) &&
            read_battery_number(entry->d_name, "/charge_full", &sample.charge_full);
        sample.has_current = read_battery_number(entry->d_name, "/current_now", &sample.current);
        cf_battery_add(&total, &sample);
    }
    closedir(dir);
    if (!total.count) return;
    result = cf_battery_finish(&total);
#endif
    char capacity[32];
    if (result.has_percent) snprintf(capacity, sizeof(capacity), "%lu%%", result.percent);
    else snprintf(capacity, sizeof(capacity), "Unknown");
    const char *status = cf_battery_status_name(result.status);
    if (result.has_time) {
        char duration[32];
        cf_format_duration_compact(result.seconds, duration, sizeof(duration));
        print_info("Battery", "%s (%s, %s %s)", 20, 30, capacity, status, duration,
                   result.status == CF_BATTERY_CHARGING ? "until full" : "left");
    } else {
        print_info("Battery", "%s (%s)", 20, 30, capacity, status);
    }
}
