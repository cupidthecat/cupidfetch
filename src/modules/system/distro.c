#include "../../cupidfetch.h"

void get_distro() {
    const char *distro = detect_linux_distro();
    print_info("Distro", "%s", 20, 30, distro);
}
