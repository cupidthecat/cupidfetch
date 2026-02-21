#include "../../cupidfetch.h"

void get_shell() {
#ifdef _WIN32
    const char *shell = getenv("ComSpec");
    if (!shell || !shell[0]) shell = "cmd.exe";
#else
    const char *shell = getenv("SHELL");
    if (shell == NULL || shell[0] == '\0') {
        uid_t uid = geteuid();
        struct passwd *pw = getpwuid(uid);
        if (pw == NULL) {
            cupid_log(LogType_ERROR, "getpwuid failed to retrieve user password entry");
            return;
        }
        shell = pw->pw_shell;
    }

    if (shell == NULL) {
        cupid_log(LogType_ERROR, "getpwuid failed to retrieve user shell information");
        return;
    }
#endif

#ifdef _WIN32
    const char *baseName = strrchr(shell, '\\');
    if (baseName == NULL) baseName = strrchr(shell, '/');
    baseName = (baseName != NULL) ? baseName + 1 : shell;
#else
    const char *baseName = strrchr(shell, '/');
    baseName = (baseName != NULL) ? baseName + 1 : shell;
#endif

    print_info("Shell", baseName, 20, 30);
}
