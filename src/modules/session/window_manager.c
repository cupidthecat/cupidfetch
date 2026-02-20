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
        {"wayfire", "Wayfire"},
        {"waybox", "Waybox"},
        {"weston", "Weston"},
        {"niri", "Niri"},
        {"cagebreak", "Cagebreak"},
        {"hikari", "Hikari"},
        {"i3", "i3"},
        {"i3wm", "i3"},
        {"i3-gaps", "i3"},
        {"bspwm", "bspwm"},
        {"herbstluftwm", "herbstluftwm"},
        {"xmonad", "XMonad"},
        {"qtile", "Qtile"},
        {"spectrwm", "spectrwm"},
        {"icewm", "IceWM"},
        {"jwm", "JWM"},
        {"pekwm", "PekWM"},
        {"fvwm3", "FVWM3"},
        {"fvwm", "FVWM"},
        {"blackbox", "Blackbox"},
        {"dwm", "dwm"},
        {"dwl", "dwl"},
        {"awesome", "Awesome"},
        {"openbox", "Openbox"},
        {"fluxbox", "Fluxbox"},
        {"cwm", "cwm"},
        {"2bwm", "2bwm"},
        {"berry", "berry"},
        {"leftwm", "LeftWM"},
        {"muffin", "Muffin"},
        {"kwin_wayland", "KWin (Wayland)"},
        {"kwin_x11", "KWin (X11)"},
        {"kwin", "KWin"},
        {"mutter", "Mutter"},
        {"xfwm", "Xfwm"},
        {"xfwm4", "Xfwm4"},
        {"marco", "Marco"},
        {"metacity", "Metacity"},
        {"enlightenment", "Enlightenment"},
        {"openbox-lxde", "Openbox (LXDE)"},
        {"gamescope", "gamescope"},
    };

    const char *detected = cf_detect_process_label(wm_candidates, sizeof(wm_candidates) / sizeof(wm_candidates[0]));
    if (detected) {
        print_info("WM", detected, 20, 30);
    }
}
