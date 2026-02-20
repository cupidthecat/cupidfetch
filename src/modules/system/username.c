#include "../../cupidfetch.h"

void get_username() {
    char* username = getlogin();
    if (username == NULL) {
        struct passwd *pw = getpwuid(geteuid());
        if (pw != NULL)
            username = pw->pw_name;
    }
    if (username != NULL)
        print_info("Username", username, 20, 30);
    else
        cupid_log(LogType_ERROR, "couldn't get username");
}
