#ifndef CF_BATTERY_HELPERS_H
#define CF_BATTERY_HELPERS_H
#include <stdbool.h>
#include <stddef.h>

typedef enum { CF_BATTERY_UNKNOWN, CF_BATTERY_CHARGING,
    CF_BATTERY_DISCHARGING, CF_BATTERY_FULL, CF_BATTERY_IDLE,
    CF_BATTERY_MIXED } cf_battery_status;
typedef struct {
    bool has_capacity, has_energy, has_charge, has_power, has_current;
    unsigned long capacity, energy_now, energy_full, power;
    unsigned long charge_now, charge_full, current;
    cf_battery_status status;
} cf_battery_sample;
typedef struct {
    size_t count, energy_count, power_count;
    long double energy_now, energy_full, power;
    cf_battery_sample single;
    cf_battery_status status;
} cf_battery_total;
typedef struct {
    bool has_percent, has_time;
    unsigned long percent, seconds;
    cf_battery_status status;
} cf_battery_result;
void cf_battery_add(cf_battery_total *total, const cf_battery_sample *sample);
cf_battery_result cf_battery_finish(const cf_battery_total *total);
const char *cf_battery_status_name(cf_battery_status status);
#endif
