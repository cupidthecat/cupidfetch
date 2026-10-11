#include "../src/cupidfetch.h"
#include <unistd.h>
static char root[256], output[512];
static DIR *fixture_opendir(const char *path) { (void)path; return opendir(root); }
#define opendir fixture_opendir
#include "../src/modules/hardware/battery.c"
#undef opendir
void print_info(const char *key, const char *fmt, int a, int b, ...) {
    (void)key; (void)a; (void)b;
    va_list args; va_start(args,b); vsnprintf(output,sizeof(output),fmt,args); va_end(args);
}
bool cf_build_power_supply_path(char *out,size_t size,const char *name,const char *field) {
    int n = snprintf(out,size,"%s/%s%s",root,name,field);
    return n >= 0 && (size_t)n < size;
}
bool cf_read_first_line(const char *path,char *out,size_t size) {
    FILE *f = fopen(path,"r"); if (!f) return false;
    bool ok = fgets(out,size,f) != NULL; fclose(f);
    if (ok) out[strcspn(out,"\r\n")] = 0;
    return ok;
}
void cf_format_duration_compact(unsigned long seconds,char *out,size_t size) { snprintf(out,size,"%lus",seconds); }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#x,output); exit(1); } } while (0)
static void put(const char *name,const char *field,const char *text) {
    char path[512]; CHECK(cf_build_power_supply_path(path,sizeof(path),name,field));
    FILE *f = fopen(path,"w"); CHECK(f); CHECK(fputs(text,f) >= 0); CHECK(fclose(f) == 0);
}
int main(void) {
    strcpy(root,"/tmp/cupidfetch-battery-XXXXXX"); CHECK(mkdtemp(root));
    char path[512]; snprintf(path,sizeof(path),"%s/BAT0",root); CHECK(!mkdir(path,0700));
    snprintf(path,sizeof(path),"%s/BAT1",root); CHECK(!mkdir(path,0700));
    put("BAT0","/type","Battery\n"); put("BAT0","/status","Not charging\n");
    put("BAT0","/capacity","75\n"); get_battery(); CHECK(!strcmp(output,"75% (Not charging)"));
    const char *bad[] = {"-1", "+1", "50junk", "", "184467440737095516160", " 50", "00000000000000000000000000000000000000000000000000000000000000000junk"};
    unsigned long n;
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) {
        put("BAT0","/capacity",bad[i]); CHECK(!read_battery_number("BAT0","/capacity",&n));
    }
    put("BAT0","/capacity","50\n"); put("BAT0","/energy_now","10");
    put("BAT0","/energy_full","20"); put("BAT0","/power_now","5");
    put("BAT0","/status","Discharging");
    put("BAT1","/type","Battery"); put("BAT1","/capacity","90");
    put("BAT1","/energy_now","90"); put("BAT1","/energy_full","100");
    put("BAT1","/status","Discharging");
    get_battery(); CHECK(!strcmp(output,"83% (Discharging)"));
    put("BAT1","/power_now","15"); get_battery(); CHECK(!strcmp(output,"83% (Discharging, 18000s left)"));
    put("BAT1","/energy_full","oops"); get_battery(); CHECK(!strcmp(output,"Unknown (Discharging)"));
    put("BAT1","/present","0"); get_battery(); CHECK(!strcmp(output,"50% (Discharging, 7200s left)"));
    snprintf(path,sizeof(path),"%s/mouse0",root); CHECK(!mkdir(path,0700));
    put("BAT0","/scope","System");
    put("mouse0","/type","Battery"); put("mouse0","/capacity","90");
    put("mouse0","/status","Discharging"); put("mouse0","/scope","Device");
    get_battery(); CHECK(!strcmp(output,"50% (Discharging, 7200s left)"));
    put("mouse0","/scope","Unknown");
    get_battery(); CHECK(!strcmp(output,"Unknown (Discharging)"));
    put("mouse0","/scope","System");
    get_battery(); CHECK(!strcmp(output,"Unknown (Discharging)"));
    CHECK(cf_build_power_supply_path(path,sizeof(path),"mouse0","/scope")); CHECK(!unlink(path));
    get_battery(); CHECK(!strcmp(output,"Unknown (Discharging)"));
    put("mouse0","/scope","Device");
    put("BAT0","/present","0"); output[0] = 0; get_battery(); CHECK(!output[0]);
    CHECK(cf_build_power_supply_path(path,sizeof(path),"BAT0","/scope")); CHECK(!unlink(path));
    const char *peripheral_fields[] = {"/type","/capacity","/status","/scope"};
    for (size_t i=0;i<sizeof(peripheral_fields)/sizeof(*peripheral_fields);i++) {
        CHECK(cf_build_power_supply_path(path,sizeof(path),"mouse0",peripheral_fields[i])); CHECK(!unlink(path));
    }
    CHECK(cf_build_power_supply_path(path,sizeof(path),"mouse0","")); CHECK(!rmdir(path));
    const char *fields[] = {"/type","/status","/capacity","/energy_now","/energy_full","/power_now","/present"};
    for (int i=0;i<2;i++) { char name[16]; snprintf(name,sizeof(name),"BAT%d",i);
        for (size_t j=0;j<sizeof(fields)/sizeof(*fields);j++) {
            CHECK(cf_build_power_supply_path(path,sizeof(path),name,fields[j])); CHECK(!unlink(path));
        }
        CHECK(cf_build_power_supply_path(path,sizeof(path),name,"")); CHECK(!rmdir(path));
    }
    CHECK(!rmdir(root)); puts("battery sysfs fixtures passed"); return 0;
}
