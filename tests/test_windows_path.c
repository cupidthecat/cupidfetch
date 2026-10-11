#include "../src/modules/common/module_helpers.h"

#ifndef _WIN32
#error This test must be compiled and run natively on Windows.
#endif

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
    } \
} while (0)
#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "SETUP FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
        goto cleanup; \
    } \
} while (0)

static char *copy_environment(const char *name) {
    const char *value = getenv(name);
    if (!value) value = "";
    char *copy = malloc(strlen(value) + 1);
    if (copy) strcpy(copy, value);
    return copy;
}

int main(void) {
    const char *files[] = {"winget.exe", "choco.com", "scoop.cmd", "batch.bat",
                           "custom.xyz", "readme.txt", "bare", "version.1.cmd"};
    const char *directories[] = {"directory.exe", "directory.cmd", "bare_directory"};
    char temp[MAX_PATH], root[MAX_PATH] = "", shims[MAX_PATH] = "";
    char path[MAX_PATH], search_path[8192], extensions[8192];
    char *saved_path = copy_environment("PATH");
    char *saved_extensions = copy_environment("PATHEXT");
    size_t created_files = 0, created_directories = 0;
    bool root_created = false, shims_created = false;
    DWORD temp_len = GetTempPathA(sizeof(temp), temp);
    REQUIRE(saved_path && saved_extensions);
    REQUIRE(temp_len > 0 && temp_len < sizeof(temp));
    REQUIRE(GetTempFileNameA(temp, "cfp", 0, root) != 0);
    REQUIRE(DeleteFileA(root));
    REQUIRE(CreateDirectoryA(root, NULL));
    root_created = true;
    REQUIRE(strlen(root) + sizeof("\\Scoop shims") < sizeof(shims));
    snprintf(shims, sizeof(shims), "%s\\Scoop shims", root);
    REQUIRE(CreateDirectoryA(shims, NULL));
    shims_created = true;

    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        int len = snprintf(path, sizeof(path), "%s\\%s", shims, files[i]);
        REQUIRE(len > 0 && (size_t)len < sizeof(path));
        FILE *file = fopen(path, "wb");
        REQUIRE(file != NULL);
        created_files++;
        REQUIRE(fclose(file) == 0);
    }
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        int len = snprintf(path, sizeof(path), "%s\\%s", shims, directories[i]);
        REQUIRE(len > 0 && (size_t)len < sizeof(path));
        REQUIRE(CreateDirectoryA(path, NULL));
        created_directories++;
    }

    REQUIRE(_putenv_s("PATH", shims) == 0);
    REQUIRE(_putenv_s("PATHEXT", ".COM;.EXE;.BAT;.CMD") == 0);
    CHECK(cf_executable_in_path("winget"));
    CHECK(cf_executable_in_path("choco"));
    CHECK(cf_executable_in_path("scoop"));
    CHECK(cf_executable_in_path("batch"));
    CHECK(cf_executable_in_path("winget.exe"));
    CHECK(cf_executable_in_path("SCOOP.CMD"));
    CHECK(cf_executable_in_path("version.1"));
    CHECK(!cf_executable_in_path("absent"));
    CHECK(!cf_executable_in_path("readme"));
    CHECK(!cf_executable_in_path("readme.txt"));
    CHECK(!cf_executable_in_path("bare"));
    CHECK(!cf_executable_in_path("directory"));
    CHECK(!cf_executable_in_path("directory.exe"));
    CHECK(!cf_executable_in_path("directory.cmd"));
    CHECK(!cf_executable_in_path("bare_directory"));
    CHECK(!cf_executable_in_path(NULL));
    CHECK(!cf_executable_in_path(""));

    /* Both positive and negative cached answers must follow PATHEXT changes. */
    CHECK(!cf_executable_in_path("custom"));
    REQUIRE(_putenv_s("PATHEXT", ".XYZ") == 0);
    CHECK(cf_executable_in_path("custom"));
    CHECK(!cf_executable_in_path("scoop"));
    REQUIRE(_putenv_s("PATHEXT", ".cmd;.exe;.com;.bat") == 0);
    CHECK(cf_executable_in_path("scoop"));
    CHECK(!cf_executable_in_path("custom"));
    CHECK(cf_executable_in_path("winget"));

    /* PATH changes must invalidate both types of cache entry as well. */
    REQUIRE(_putenv_s("PATH", root) == 0);
    CHECK(!cf_executable_in_path("scoop"));
    REQUIRE(_putenv_s("PATH", shims) == 0);
    CHECK(cf_executable_in_path("scoop"));

    snprintf(search_path, sizeof(search_path), "%s\\missing;;\"%s\";", root, shims);
    REQUIRE(_putenv_s("PATH", search_path) == 0);
    CHECK(cf_executable_in_path("scoop"));
    CHECK(!cf_executable_in_path("directory"));

    /* Unset/empty PATHEXT uses the usual native and batch extensions. */
    REQUIRE(_putenv_s("PATHEXT", "") == 0);
    CHECK(cf_executable_in_path("winget"));
    CHECK(cf_executable_in_path("choco"));
    CHECK(cf_executable_in_path("batch"));
    CHECK(cf_executable_in_path("scoop"));

    /* Nested PATH/PATHEXT iteration must reach later directories and suffixes. */
    snprintf(search_path, sizeof(search_path), "%s;%s", root, shims);
    REQUIRE(_putenv_s("PATH", search_path) == 0);
    REQUIRE(_putenv_s("PATHEXT", ";invalid;.missing;;.CMD;") == 0);
    CHECK(cf_executable_in_path("scoop"));

    /* A real entry after the old 4096-byte limit must still be visited. */
    search_path[0] = '\0';
    while (strlen(search_path) < 4200) {
        strcat(search_path, root);
        strcat(search_path, ";");
    }
    strcat(search_path, shims);
    REQUIRE(_putenv_s("PATH", search_path) == 0);
    CHECK(cf_executable_in_path("scoop"));
    CHECK(!cf_executable_in_path("absent"));
    REQUIRE(_putenv_s("PATH", shims) == 0);
    extensions[0] = '\0';
    while (strlen(extensions) < 4200) strcat(extensions, ".missing;");
    strcat(extensions, ".CMD");
    REQUIRE(_putenv_s("PATHEXT", extensions) == 0);
    CHECK(cf_executable_in_path("scoop"));
    CHECK(!cf_executable_in_path("winget"));

    REQUIRE(_putenv_s("PATH", "") == 0);
    CHECK(!cf_executable_in_path("scoop"));

cleanup:
    if (saved_path) CHECK(_putenv_s("PATH", saved_path) == 0);
    if (saved_extensions) CHECK(_putenv_s("PATHEXT", saved_extensions) == 0);
    free(saved_path);
    free(saved_extensions);
    for (size_t i = 0; i < created_files; i++) {
        snprintf(path, sizeof(path), "%s\\%s", shims, files[i]);
        CHECK(DeleteFileA(path));
    }
    for (size_t i = 0; i < created_directories; i++) {
        snprintf(path, sizeof(path), "%s\\%s", shims, directories[i]);
        CHECK(RemoveDirectoryA(path));
    }
    if (shims_created) CHECK(RemoveDirectoryA(shims));
    if (root_created) CHECK(RemoveDirectoryA(root));
    if (failures) {
        fprintf(stderr, "test_windows_path: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("test_windows_path: OK");
    return EXIT_SUCCESS;
}
