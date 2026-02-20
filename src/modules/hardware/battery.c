#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_battery() {
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) return;

    struct dirent *entry;
    unsigned long cap_sum = 0;
    size_t cap_count = 0;
    unsigned long energy_now_sum = 0;
    unsigned long energy_full_sum = 0;
    unsigned long energy_rate_sum = 0;
    unsigned long charge_now_sum = 0;
    unsigned long charge_full_sum = 0;
    unsigned long charge_rate_sum = 0;
    bool found_battery = false;
    char battery_status[64] = "";

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[512];
        char type[64];

        if (!cf_build_power_supply_path(path, sizeof(path), entry->d_name, "/type")) continue;

        if (!cf_read_first_line(path, type, sizeof(type))) continue;
        if (strcmp(type, "Battery") != 0) continue;

        found_battery = true;

        if (battery_status[0] == '\0') {
            if (cf_build_power_supply_path(path, sizeof(path), entry->d_name, "/status")) {
                cf_read_first_line(path, battery_status, sizeof(battery_status));
            }
        } else if (cf_build_power_supply_path(path, sizeof(path), entry->d_name, "/status")) {
            char status_this[64];
            if (cf_read_first_line(path, status_this, sizeof(status_this)) &&
                status_this[0] != '\0' && strcmp(battery_status, status_this) != 0) {
                snprintf(battery_status, sizeof(battery_status), "Mixed");
            }
        }

        unsigned long capacity = 0;
        if (cf_build_power_supply_path(path, sizeof(path), entry->d_name, "/capacity") &&
            cf_read_ulong_file(path, &capacity)) {
            cap_sum += capacity;
            cap_count++;
        }

        char now_path[512];
        char full_path[512];
        char rate_path[512];
        unsigned long now_val = 0;
        unsigned long full_val = 0;
        unsigned long rate_val = 0;

        bool have_energy_paths =
            cf_build_power_supply_path(now_path, sizeof(now_path), entry->d_name, "/energy_now") &&
            cf_build_power_supply_path(full_path, sizeof(full_path), entry->d_name, "/energy_full");

        if (have_energy_paths && cf_read_ulong_file(now_path, &now_val) && cf_read_ulong_file(full_path, &full_val) && full_val > 0) {
            energy_now_sum += now_val;
            energy_full_sum += full_val;

            if (cf_build_power_supply_path(rate_path, sizeof(rate_path), entry->d_name, "/power_now") &&
                cf_read_ulong_file(rate_path, &rate_val)) {
                energy_rate_sum += rate_val;
            }
            continue;
        }

        if (!cf_build_power_supply_path(now_path, sizeof(now_path), entry->d_name, "/charge_now") ||
            !cf_build_power_supply_path(full_path, sizeof(full_path), entry->d_name, "/charge_full")) {
            continue;
        }

        if (cf_read_ulong_file(now_path, &now_val) && cf_read_ulong_file(full_path, &full_val) && full_val > 0) {
            charge_now_sum += now_val;
            charge_full_sum += full_val;

            if (cf_build_power_supply_path(rate_path, sizeof(rate_path), entry->d_name, "/current_now") &&
                cf_read_ulong_file(rate_path, &rate_val)) {
                charge_rate_sum += rate_val;
            }
        }
    }

    closedir(dir);

    if (!found_battery) return;

    unsigned long percent = 0;
    if (cap_count > 0) {
        percent = cap_sum / cap_count;
    } else if (energy_full_sum > 0) {
        percent = (energy_now_sum * 100UL) / energy_full_sum;
    } else if (charge_full_sum > 0) {
        percent = (charge_now_sum * 100UL) / charge_full_sum;
    } else {
        return;
    }

    if (percent > 100UL) percent = 100UL;

    bool is_charging = cf_contains_icase(battery_status, "charging") && !cf_contains_icase(battery_status, "discharging");
    bool is_discharging = cf_contains_icase(battery_status, "discharging");
    bool have_time = false;
    unsigned long remaining_seconds = 0;

    if (is_discharging) {
        if (energy_now_sum > 0 && energy_rate_sum > 0) {
            remaining_seconds = (unsigned long)(((unsigned long long)energy_now_sum * 3600ULL) / (unsigned long long)energy_rate_sum);
            have_time = true;
        } else if (charge_now_sum > 0 && charge_rate_sum > 0) {
            remaining_seconds = (unsigned long)(((unsigned long long)charge_now_sum * 3600ULL) / (unsigned long long)charge_rate_sum);
            have_time = true;
        }
    } else if (is_charging) {
        if (energy_full_sum > energy_now_sum && energy_rate_sum > 0) {
            unsigned long to_full = energy_full_sum - energy_now_sum;
            remaining_seconds = (unsigned long)(((unsigned long long)to_full * 3600ULL) / (unsigned long long)energy_rate_sum);
            have_time = true;
        } else if (charge_full_sum > charge_now_sum && charge_rate_sum > 0) {
            unsigned long to_full = charge_full_sum - charge_now_sum;
            remaining_seconds = (unsigned long)(((unsigned long long)to_full * 3600ULL) / (unsigned long long)charge_rate_sum);
            have_time = true;
        }
    }

    if (battery_status[0] != '\0') {
        if (have_time) {
            char duration[32];
            cf_format_duration_compact(remaining_seconds, duration, sizeof(duration));
            if (is_discharging) {
                print_info("Battery", "%lu%% (%s, %s left)", 20, 30, percent, battery_status, duration);
            } else if (is_charging) {
                print_info("Battery", "%lu%% (%s, %s until full)", 20, 30, percent, battery_status, duration);
            } else {
                print_info("Battery", "%lu%% (%s)", 20, 30, percent, battery_status);
            }
        } else {
            print_info("Battery", "%lu%% (%s)", 20, 30, percent, battery_status);
        }
    } else {
        print_info("Battery", "%lu%%", 20, 30, percent);
    }
}
