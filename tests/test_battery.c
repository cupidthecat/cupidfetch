#include "../src/modules/common/battery_helpers.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static cf_battery_result finish(cf_battery_sample a, cf_battery_sample b, int count) {
    cf_battery_total t = {0};
    if (count > 0) cf_battery_add(&t, &a);
    if (count > 1) cf_battery_add(&t, &b);
    return cf_battery_finish(&t);
}
int main(void) {
    cf_battery_sample a = {0}, b = {0};
    cf_battery_result r = finish(a,b,0);
    CHECK(!r.has_percent && !r.has_time);
    a.has_energy = a.has_power = a.has_capacity = true;
    a.energy_now = 10; a.energy_full = 20; a.power = 5; a.capacity = 50;
    a.status = CF_BATTERY_DISCHARGING;
    b = a; b.energy_now = 90; b.energy_full = 100; b.power = 15; b.capacity = 90;
    r = finish(a,b,2);
    CHECK(r.has_percent && r.percent == 83 && r.has_time && r.seconds == 18000);
    b.has_power = false; r = finish(a,b,2);
    CHECK(r.has_percent && r.percent == 83 && !r.has_time);
    b.has_power = true; b.power = 0; r = finish(a,b,2); CHECK(!r.has_time);
    b.power = 15; b.has_energy = false; r = finish(a,b,2);
    CHECK(!r.has_percent && !r.has_time);
    b.has_charge = true; b.charge_now = 90; b.charge_full = 100;
    b.has_current = true; b.current = 15;
    r = finish(a,b,2); CHECK(!r.has_percent && !r.has_time);
    a.has_energy = false; a.has_charge = true; a.charge_now = 10; a.charge_full = 20;
    a.has_current = true; a.current = 5;
    r = finish(a,b,2); CHECK(!r.has_percent && !r.has_time);
    r = finish(a,b,1); CHECK(r.percent == 50 && r.has_time && r.seconds == 7200);
    a.status = CF_BATTERY_CHARGING; a.charge_now = 5;
    r = finish(a,b,1); CHECK(r.percent == 25 && r.seconds == 10800);
    a.status = CF_BATTERY_IDLE; r = finish(a,b,1); CHECK(!r.has_time);
    a.has_charge = false; r = finish(a,b,1); CHECK(r.has_percent && r.percent == 50 && !r.has_time);
    a.capacity = 101; r = finish(a,b,1); CHECK(!r.has_percent);
    a.has_capacity = false; r = finish(a,b,1); CHECK(!r.has_percent);
    a.has_energy = a.has_power = true; a.status = CF_BATTERY_DISCHARGING;
    a.energy_now = 0; a.energy_full = 100; a.power = 1;
    r = finish(a,b,1); CHECK(r.has_percent && r.percent == 0 && r.has_time && r.seconds == 0);
    b = a; b.status = CF_BATTERY_UNKNOWN;
    r = finish(a,b,2); CHECK(r.status == CF_BATTERY_MIXED && !r.has_time);
    b.status = CF_BATTERY_CHARGING; r = finish(a,b,2); CHECK(!r.has_time);
    a.energy_now = a.energy_full = a.power = ULONG_MAX; b = a;
    r = finish(a,b,2); CHECK(r.percent == 100 && r.seconds == 3600);
    a.power = 1; r = finish(a,b,1); CHECK(!r.has_time);
    a.energy_now = 101; a.energy_full = 100;
    r = finish(a,b,1); CHECK(!r.has_percent && !r.has_time);
    a.energy_full = 0; r = finish(a,b,1); CHECK(!r.has_percent && !r.has_time);
    puts("battery aggregation fixtures passed");
    return 0;
}
