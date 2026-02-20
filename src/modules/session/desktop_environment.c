#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

void get_desktop_environment() {
    const char* xdgDesktop = getenv("XDG_CURRENT_DESKTOP");
    if (xdgDesktop != NULL && strlen(xdgDesktop) > 0) {
        print_info("DE", xdgDesktop, 20, 30);
        return;
    }

    const char* desktopSession = getenv("DESKTOP_SESSION");
    if (desktopSession != NULL && strlen(desktopSession) > 0) {
        print_info("DE", desktopSession, 20, 30);
        return;
    }

    static const struct process_match de_candidates[] = {
        {"gnome-shell", "GNOME"},
        {"plasmashell", "KDE Plasma"},
        {"kwin_wayland", "KDE Plasma"},
        {"kwin_x11", "KDE Plasma"},
        {"xfce4-session", "XFCE"},
        {"mate-session", "MATE"},
        {"lxqt-session", "LXQt"},
        {"lxsession", "LXDE"},
        {"cinnamon-session", "Cinnamon"},
        {"budgie-desktop", "Budgie"},
        {"enlightenment", "Enlightenment"},
        {"pantheon-session", "Pantheon"},
        {"startdde", "Deepin"},
        {"ukui-session", "UKUI"},
        {"cutefish-session", "Cutefish"},
        {"sway", "Sway"},
        {"hyprland", "Hyprland"},
    };

    const char *detected = cf_detect_process_label(de_candidates, sizeof(de_candidates) / sizeof(de_candidates[0]));
    if (detected) {
        print_info("DE", detected, 20, 30);
    }
}
