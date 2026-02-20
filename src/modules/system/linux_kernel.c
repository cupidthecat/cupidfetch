#include "../../cupidfetch.h"

void get_linux_kernel() {
    struct utsname uname_data;

    if (uname(&uname_data) != 0)
        cupid_log(LogType_ERROR, "couldn't get uname data");
    else
        print_info("Linux Kernel", uname_data.release, 20, 30);
}
