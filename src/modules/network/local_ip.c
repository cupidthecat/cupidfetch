#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_local_ip() {
    char iface[64] = "";
    char ip_addr[INET6_ADDRSTRLEN] = "";
    bool up = false;

    if (!cf_detect_primary_ip(iface, sizeof(iface), ip_addr, sizeof(ip_addr), &up)) {
        return;
    }

    print_info("Local IP", "%s", 20, 30, ip_addr);
}
