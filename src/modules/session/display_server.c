#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_display_server() {
    const char *session_type = getenv("XDG_SESSION_TYPE");
    if (session_type && session_type[0]) {
        if (cf_contains_icase(session_type, "wayland")) {
            print_info("Display Server", "Wayland", 20, 30);
            return;
        }
        if (cf_contains_icase(session_type, "x11")) {
            print_info("Display Server", "X11", 20, 30);
            return;
        }
        print_info("Display Server", session_type, 20, 30);
        return;
    }

    if (getenv("WAYLAND_DISPLAY")) {
        print_info("Display Server", "Wayland", 20, 30);
        return;
    }

    if (getenv("DISPLAY")) {
        print_info("Display Server", "X11", 20, 30);
        return;
    }

    static const struct process_match ds_candidates[] = {
        {"xwayland", "XWayland"},
        {"Xorg", "X11"},
        {"wayland", "Wayland"},
    };

    const char *detected = cf_detect_process_label(ds_candidates, sizeof(ds_candidates) / sizeof(ds_candidates[0]));
    if (detected) {
        print_info("Display Server", detected, 20, 30);
    }
}
