#include "../../cupidfetch.h"

void get_hostname() {
    char hostname[256];
#ifdef _WIN32
    const char *computer_name = getenv("COMPUTERNAME");
    if (!computer_name || !computer_name[0]) {
        cupid_log(LogType_ERROR, "couldn't get hostname");
        return;
    }
    strncpy(hostname, computer_name, sizeof(hostname) - 1);
    hostname[sizeof(hostname) - 1] = '\0';
    print_info("Hostname", hostname, 20, 30);
#else
    if (gethostname(hostname, sizeof(hostname)) != 0)
        cupid_log(LogType_ERROR, "couldn't get hostname");
    else
        print_info("Hostname", hostname, 20, 30);
#endif
}
