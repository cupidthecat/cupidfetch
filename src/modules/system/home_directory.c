#include "../../cupidfetch.h"

const char* get_home_directory() {
    static char *homeDir = NULL;

    if (homeDir != NULL) return homeDir;

    if ((homeDir = getenv("HOME")) == NULL) {
        struct passwd* pw = getpwuid(getuid());
        if (pw == NULL) {
            fprintf(stderr, "home directory couldn't be found sir\n");
            exit(EXIT_FAILURE);
        }
        homeDir = pw->pw_dir;
    }
    return homeDir;
}
