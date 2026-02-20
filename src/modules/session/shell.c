#include "../../cupidfetch.h"

void get_shell() {
    uid_t uid = geteuid();

    struct passwd *pw = getpwuid(uid);
    if (pw == NULL) {
        cupid_log(LogType_ERROR, "getpwuid failed to retrieve user password entry");
        return;
    }

    const char *shell = pw->pw_shell;
    if (shell == NULL) {
        cupid_log(LogType_ERROR, "getpwuid failed to retrieve user shell information");
        return;
    }

    const char *baseName = strrchr(shell, '/');
    baseName = (baseName != NULL) ? baseName + 1 : shell;

    print_info("Shell", baseName, 20, 30);
}
