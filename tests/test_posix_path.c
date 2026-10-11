#include "../src/modules/common/module_helpers.h"

int main(void) {
    char root[] = "/tmp/cupidfetch-path-XXXXXX";
    if (!mkdtemp(root)) return 1;
    char executable[256], plain[256], directory[256], long_path[8192];
    snprintf(executable, sizeof(executable), "%s/command", root);
    snprintf(plain, sizeof(plain), "%s/plain", root);
    snprintf(directory, sizeof(directory), "%s/directory", root);
    int failed = 0;
    FILE *fp = fopen(executable, "w");
    if (!fp) { rmdir(root); return 1; }
    if (fclose(fp) != 0 || chmod(executable, 0700) != 0) failed = 1;
    fp = fopen(plain, "w");
    if (!fp) failed = 1;
    else if (fclose(fp) != 0 || chmod(plain, 0600) != 0) failed = 1;
    if (mkdir(directory, 0700) != 0 || setenv("PATH", root, 1) != 0) failed = 1;
    if (!cf_executable_in_path("command") || cf_executable_in_path("plain") ||
        cf_executable_in_path("directory") || cf_executable_in_path("missing")) failed = 1;
    if (setenv("PATH", "/nonexistent", 1) != 0 || cf_executable_in_path("command")) failed = 1;
    if (setenv("PATH", root, 1) != 0 || !cf_executable_in_path("command")) failed = 1;
    long_path[0] = '\0';
    for (int i = 0; i < 350; i++) strcat(long_path, "/nonexistent:");
    strcat(long_path, root);
    if (setenv("PATH", long_path, 1) != 0 || !cf_executable_in_path("command") ||
        cf_executable_in_path("directory")) failed = 1;
    if (unlink(executable) != 0 || unlink(plain) != 0 || rmdir(directory) != 0 || rmdir(root) != 0) failed = 1;
    if (failed) { fputs("POSIX PATH regression failed\n", stderr); return 1; }
    puts("POSIX PATH regression passed");
    return 0;
}
