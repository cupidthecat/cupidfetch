#include "battery_helpers.h"
#include <limits.h>

void cf_battery_add(cf_battery_total *t, const cf_battery_sample *s) {
    if (!t->count) { t->single = *s; t->status = s->status; }
    else if (t->status != s->status) t->status = CF_BATTERY_MIXED;
    t->count++;
    if (s->has_energy && s->energy_full > 0 && s->energy_now <= s->energy_full) {
        t->energy_count++;
        t->energy_now += s->energy_now;
        t->energy_full += s->energy_full;
        if (s->has_power && s->power > 0) {
            t->power_count++;
            t->power += s->power;
        }
    }
}

cf_battery_result cf_battery_finish(const cf_battery_total *t) {
    cf_battery_result r = {0};
    long double now = 0, full = 0, rate = 0;
    r.status = t->status;
    if (!t->count) return r;
    if (t->energy_count == t->count) {
        now = t->energy_now; full = t->energy_full;
        if (t->power_count == t->count) rate = t->power;
    } else if (t->count == 1 && t->single.has_charge &&
               t->single.charge_full > 0 && t->single.charge_now <= t->single.charge_full) {
        /* Charge/current have compatible units for one battery. Across packs,
           different voltages make their capacities unsuitable as weights. */
        now = t->single.charge_now; full = t->single.charge_full;
        if (t->single.has_current && t->single.current > 0) rate = t->single.current;
    }
    if (full > 0) {
        r.has_percent = true;
        r.percent = (unsigned long)(100.0L * now / full);
    } else if (t->count == 1 && t->single.has_capacity && t->single.capacity <= 100) {
        r.has_percent = true;
        r.percent = t->single.capacity;
    }
    if (rate > 0 && (r.status == CF_BATTERY_CHARGING || r.status == CF_BATTERY_DISCHARGING)) {
        long double seconds = 3600.0L * (r.status == CF_BATTERY_CHARGING ? full - now : now) / rate;
        /* Strict upper bound also avoids a rounded ULONG_MAX on platforms
           whose long double has only double precision. */
        if (seconds >= 0 && seconds < (long double)ULONG_MAX) {
            r.has_time = true;
            r.seconds = (unsigned long)seconds;
        }
    }
    return r;
}

const char *cf_battery_status_name(cf_battery_status s) {
    switch (s) {
    case CF_BATTERY_CHARGING: return "Charging";
    case CF_BATTERY_DISCHARGING: return "Discharging";
    case CF_BATTERY_FULL: return "Full";
    case CF_BATTERY_IDLE: return "Not charging";
    case CF_BATTERY_MIXED: return "Mixed";
    default: return "Unknown";
    }
}
