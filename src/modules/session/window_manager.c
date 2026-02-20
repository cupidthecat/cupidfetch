#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_window_manager() {
    const char *wm_env = getenv("WINDOWMANAGER");
    const char *wm_name = cf_basename_or_self(wm_env);
    if (wm_name && wm_name[0]) {
        print_info("WM", wm_name, 20, 30);
        return;
    }

    static const struct process_match wm_candidates[] = {
        {"hyprland", "Hyprland"},
        {"sway", "Sway"},
        {"river", "River"},
        {"labwc", "labwc"},
        {"i3", "i3"},
        {"bspwm", "bspwm"},
        {"dwm", "dwm"},
        {"awesome", "Awesome"},
        {"openbox", "Openbox"},
        {"fluxbox", "Fluxbox"},
        {"kwin_wayland", "KWin (Wayland)"},
        {"kwin_x11", "KWin (X11)"},
        {"mutter", "Mutter"},
        {"xfwm4", "Xfwm4"},
        {"marco", "Marco"},
    };

    const char *detected = cf_detect_process_label(wm_candidates, sizeof(wm_candidates) / sizeof(wm_candidates[0]));
    if (detected) {
        print_info("WM", detected, 20, 30);
    }
}
