#ifndef CUPIDFETCH_H
#define CUPIDFETCH_H

#include "cupidconf.h"  // Use cupidconf instead of inih
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <windows.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #include <direct.h>
    #include <process.h>
    #ifndef PATH_MAX
        #define PATH_MAX MAX_PATH
    #endif
    #ifndef F_OK
        #define F_OK 0
    #endif
    #ifndef X_OK
        #define X_OK 1
    #endif
    #ifndef access
        #define access _access
    #endif
    #ifndef isatty
        #define isatty _isatty
    #endif
    #ifndef pclose
        #define pclose _pclose
    #endif
    #ifndef popen
        #define popen _popen
    #endif
    #ifndef getpid
        #define getpid _getpid
    #endif
    #ifndef strcasecmp
        #define strcasecmp _stricmp
    #endif
    #ifndef strncasecmp
        #define strncasecmp _strnicmp
    #endif
#else
    #include <arpa/inet.h>
    #include <ifaddrs.h>
    #include <net/if.h>
    #include <pwd.h>
    #include <sys/ioctl.h>
    #include <sys/utsname.h>
    #include <unistd.h>
#endif

#define MAX_NUM_MODULES 30
#define CONFIG_PATH_SIZE 256
#define LINUX_PROC_LINE_SZ 128
#define MEMORY_UNIT_LEN 128

struct CupidConfig {
    void (*modules[MAX_NUM_MODULES + 1])(void);
    char memory_unit[MEMORY_UNIT_LEN];
    unsigned long memory_unit_size;
    char storage_unit[MEMORY_UNIT_LEN];
    unsigned long storage_unit_size;
    bool network_show_full_public_ip;
};

typedef enum {
    LogType_INFO = 0,
    LogType_WARNING = 1,
    LogType_ERROR = 2,
    LogType_CRITICAL = 3
} LogType;

// print.c
int get_terminal_width();
void print_info(const char *key, const char *format, int align_key, int align_value, ...);
void print_cat(const char* distro);
void begin_info_capture(void);
void end_info_capture(void);
void render_fetch_panel(const char *distro, const char *user_host);
void render_json_output(const char *user_host);

// modules.c
void get_hostname();
void get_username();
void get_linux_kernel();
void get_uptime();
void get_distro();
void get_package_count();
void get_shell();
void get_terminal();
void get_desktop_environment();
void get_window_manager();
void get_theme();
void get_icons();
void get_display_server();
void get_net();
void get_local_ip();
void get_battery();
void get_gpu();
void get_available_memory();
void get_cpu();
void get_available_storage();
const char* get_home_directory();

// config.c
extern struct CupidConfig g_userConfig;
void init_g_config();
// New function to load configuration using cupidconf:
void load_config_file(const char* config_path, struct CupidConfig *config);

// log.c
void cupid_log(LogType ltp, const char *format, ...);

// main.c
extern FILE *g_log;
const char* detect_linux_distro();
void epitaph();

#endif // CUPIDFETCH_H
