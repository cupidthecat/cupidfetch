#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_net() {
    char iface[64] = "";
    char local_ip[INET6_ADDRSTRLEN] = "";
    char public_ip[128] = "";
    char public_ip_display[128] = "";
    bool up = false;

    if (!cf_detect_primary_ip(iface, sizeof(iface), local_ip, sizeof(local_ip), &up)) {
        print_info("Net", "Disconnected", 20, 30);
        return;
    }

    const char *state = up ? "up" : "down";
    if (cf_get_public_ip(public_ip, sizeof(public_ip))) {
        if (g_userConfig.network_show_full_public_ip) {
            snprintf(public_ip_display, sizeof(public_ip_display), "%s", public_ip);
        } else {
            cf_mask_public_ip(public_ip, public_ip_display, sizeof(public_ip_display));
        }

        print_info("Net", "%s (%s) | local %s | public %s", 20, 30, iface, state, local_ip, public_ip_display);
    } else {
        print_info("Net", "%s (%s) | local %s | public unavailable", 20, 30, iface, state, local_ip);
    }
}
