#include <stdarg.h>
#include "../src/cupidfetch.h"

void get_hostname(void) {}
void get_username(void) {}
void get_linux_kernel(void) {}
void get_uptime(void) {}
void get_distro(void) {}
void get_package_count(void) {}
void get_shell(void) {}
void get_terminal(void) {}
void get_desktop_environment(void) {}
void get_window_manager(void) {}
void get_theme(void) {}
void get_icons(void) {}
void get_display_server(void) {}
void get_net(void) {}
void get_local_ip(void) {}
void get_battery(void) {}
void get_gpu(void) {}
void get_available_memory(void) {}
void get_cpu(void) {}
void get_available_storage(void) {}

void cupid_log(LogType ltp, const char *format, ...) {
    (void)ltp;
    (void)format;
}
