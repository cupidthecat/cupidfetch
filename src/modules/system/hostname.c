#include "../../cupidfetch.h"

void get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        cupid_log(LogType_ERROR, "couldn't get hostname");
    else
        print_info("Hostname", hostname, 20, 30);
}
