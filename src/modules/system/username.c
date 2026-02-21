#include "../../cupidfetch.h"

void get_username() {
    const char *username = getenv("USER");

#ifdef _WIN32
    if (username == NULL || username[0] == '\0') {
        username = getenv("USERNAME");
    }
#else
    if (username == NULL || username[0] == '\0') {
        char *login_name = getlogin();
        if (login_name != NULL && login_name[0] != '\0') {
            username = login_name;
        }
    }

    if (username == NULL || username[0] == '\0') {
        struct passwd *pw = getpwuid(geteuid());
        if (pw != NULL) {
            username = pw->pw_name;
        }
    }
#endif

    if (username != NULL && username[0] != '\0')
        print_info("Username", username, 20, 30);
    else
        cupid_log(LogType_ERROR, "couldn't get username");
}
