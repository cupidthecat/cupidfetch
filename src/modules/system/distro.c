#include "../../cupidfetch.h"

void get_distro() {
    const char *distro = detect_linux_distro();
    print_info("Distro", distro, 20, 30);
}
