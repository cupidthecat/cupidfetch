// File: main.c
// -----------------------
#include <signal.h>
#include <sys/stat.h> // for mkdir
#include <stdio.h>    // for printf
#include <stdlib.h>   // for exit
#include <limits.h>   // For PATH_MAX
#include <string.h>
#include <errno.h>    // for errno, strerror
#include <stdbool.h>  // for bool type
// Local Includes
#include "cupidfetch.h"
#include "modules/common/module_helpers.h"

// Global Variables
FILE *g_log = NULL;
volatile sig_atomic_t resize_flag = 0; // Flag for window resize

// A small struct to hold each distro’s shortname, longname, pkg command
typedef struct {
    char shortname[64];
    char longname[128];
    char pkgcmd[128];
} distro_entry_t;

// We'll store them in a global or static array in memory at runtime
static distro_entry_t *g_knownDistros = NULL;
static size_t g_numKnown = 0;
static char g_forced_distro[128] = "";
static bool g_json_output = false;
static bool g_distros_loaded = false;
static char g_distro_cache[128] = "";
static bool g_distro_cached = false;

#ifdef _WIN32
typedef LONG(WINAPI *rtl_get_version_fn)(PRTL_OSVERSIONINFOW);

static const char *detect_windows_distro_name(void) {
    OSVERSIONINFOEXW osv;
    memset(&osv, 0, sizeof(osv));
    osv.dwOSVersionInfoSize = sizeof(osv);

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        rtl_get_version_fn rtl_get_version = (rtl_get_version_fn)GetProcAddress(ntdll, "RtlGetVersion");
        if (rtl_get_version && rtl_get_version((PRTL_OSVERSIONINFOW)&osv) == 0) {
            if (osv.dwMajorVersion == 10 && osv.dwBuildNumber >= 22000) return "Windows 11";
            if (osv.dwMajorVersion == 10) return "Windows 10";
            if (osv.dwMajorVersion == 6 && osv.dwMinorVersion >= 2) return "Windows 8";
            return "Windows";
        }
    }

    return "Windows";
}

static void ensure_parent_dir_exists(const char *path) {
    if (!path || !path[0]) return;

    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *last_sep = strrchr(tmp, '\\');
    if (!last_sep) last_sep = strrchr(tmp, '/');
    if (!last_sep) return;
    *last_sep = '\0';

    _mkdir(tmp);
}
#else
static char *path_dirname(char *path) {
    if (!path || !path[0]) return path;
    char *slash = strrchr(path, '/');
    if (!slash) return path;
    *slash = '\0';
    return path;
}
#endif

static void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [--force-distro <distroname>] [--json]\n", progname);
}

static bool parse_cli_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--force-distro") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') {
                fprintf(stderr, "Error: --force-distro requires a distro name\n");
                return false;
            }

            strncpy(g_forced_distro, argv[i + 1], sizeof(g_forced_distro) - 1);
            g_forced_distro[sizeof(g_forced_distro) - 1] = '\0';
            i++;
            continue;
        }

        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }

        if (strcmp(argv[i], "--json") == 0) {
            g_json_output = true;
            continue;
        }

        fprintf(stderr, "Error: unknown argument '%s'\n", argv[i]);
        return false;
    }

    return true;
}

static void parse_distros_def(const char *path)
{
    free(g_knownDistros);
    g_knownDistros = NULL;
    g_numKnown = 0;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        // If no distros.def yet, that's okay; we can create it later.
        g_distros_loaded = true;
        return;
    }

    size_t known_capacity = 0;

    // We'll store lines in a dynamic array of distro_entry_t
    char line[512];

    // We could do a simple naive parse, or a small regex. Let's do naive:
    // DISTRO("short","long","cmd")
    //
    // We'll do:   if (strncmp(line, "DISTRO(", 7)==0) then parse inside parentheses
    // You can refine this as needed.

    while (fgets(line, sizeof(line), fp)) {
        // Skip leading spaces
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (strncmp(p, "DISTRO(", 7) != 0) {
            continue; // not a line that starts with DISTRO(
        }

        // We expect a line like:
        //   DISTRO("arch", "Arch", "pacman -Q | wc -l")
        //
        // Let's parse out the 3 strings in quotes.

        // We'll do a super naive parse for demonstration:
        char shortname[64], longname[128], pkgcmd[128];
        shortname[0] = longname[0] = pkgcmd[0] = '\0';

        // This works if the line is well-formed.  
        // We look for exactly 3 pairs of double-quotes with commas in between.
        // Something like:  DISTRO("short", "long", "cmd")
        // We can do `sscanf` with a suitable format:
        //   DISTRO("%63[^\"]", "%127[^\"]", "%127[^\"]") 
        // plus optional spaces and punctuation. 
        //
        // In real code you'd want more robust error-checking.

        if (cf_parse_distro_def_line(
                p,
                shortname,
                sizeof(shortname),
                longname,
                sizeof(longname),
                pkgcmd,
                sizeof(pkgcmd)
        )) {
            // We got a valid parse! Let's store it.
            // Reallocate global array in chunks to reduce realloc churn
            if (g_numKnown == known_capacity) {
                size_t new_capacity = (known_capacity == 0) ? 32 : known_capacity * 2;
                distro_entry_t *tmp = realloc(g_knownDistros, new_capacity * sizeof(distro_entry_t));
                if (!tmp) {
                    fclose(fp);
                    free(g_knownDistros);
                    g_knownDistros = NULL;
                    g_numKnown = 0;
                    g_distros_loaded = true;
                    return;
                }
                g_knownDistros = tmp;
                known_capacity = new_capacity;
            }

            strncpy(g_knownDistros[g_numKnown].shortname, shortname, sizeof(g_knownDistros[g_numKnown].shortname));
            g_knownDistros[g_numKnown].shortname[sizeof(g_knownDistros[g_numKnown].shortname) - 1] = '\0';
            strncpy(g_knownDistros[g_numKnown].longname, longname, sizeof(g_knownDistros[g_numKnown].longname));
            g_knownDistros[g_numKnown].longname[sizeof(g_knownDistros[g_numKnown].longname) - 1] = '\0';
            strncpy(g_knownDistros[g_numKnown].pkgcmd, pkgcmd, sizeof(g_knownDistros[g_numKnown].pkgcmd));
            g_knownDistros[g_numKnown].pkgcmd[sizeof(g_knownDistros[g_numKnown].pkgcmd) - 1] = '\0';
            g_numKnown++;
        }
    }

    fclose(fp);
    g_distros_loaded = true;
}

static bool insert_auto_added_distro(const char* defPath, 
                                     const char* distroId, 
                                     const char* capitalized)
{
    // We'll need a "was anything inserted?" flag
    bool insertedSomething = false;

    // 1) Try to open existing file. If it doesn't exist, we’ll create it.
    FILE* inFile = fopen(defPath, "r");
    bool fileExisted = (inFile != NULL);

    // We'll store lines here.
    // If your file is extremely large, consider a streaming approach,
    // but for typical usage, reading everything into memory is fine.
    char** lines = NULL;
    size_t numLines = 0, capacity = 0;

    if (fileExisted) {
        // 2) Read the entire file line-by-line into memory.
        char lineBuf[1024];
        while (fgets(lineBuf, sizeof(lineBuf), inFile)) {
            // Strip trailing newlines to make searching simpler
            lineBuf[strcspn(lineBuf, "\r\n")] = '\0';

            // Grow array if needed
            if (numLines + 1 >= capacity) {
                capacity = (capacity == 0) ? 16 : capacity * 2;
                lines = realloc(lines, capacity * sizeof(*lines));
            }

            // Copy the line into lines[numLines]
            lines[numLines] = strdup(lineBuf);
            numLines++;
        }
        fclose(inFile);
    } 
    else {
        // The file didn't exist, so we start fresh.
        capacity = 16;
        lines = malloc(capacity * sizeof(*lines));
        numLines = 0;

        // Optionally, if you want to seed it with your original block:
        lines[numLines++] = strdup("/* dpkg */");
        lines[numLines++] = strdup("DISTRO(\"ubuntu\", \"Ubuntu\", \"\")");
        lines[numLines++] = strdup("// ... etc ...");
        lines[numLines++] = strdup("/* please don't remove this */");
        lines[numLines++] = strdup("/* auto added */");
    }

    // 3) Find the index of "/* auto added */"
    long autoAddedIndex = -1;
    for (size_t i = 0; i < numLines; i++) {
        if (strstr(lines[i], "/* auto added */")) {
            autoAddedIndex = (long)i;
            break;
        }
    }

    // If not found, we append the marker at the end
    if (autoAddedIndex == -1) {
        // Grow if needed
        if (numLines + 1 >= capacity) {
            capacity *= 2;
            lines = realloc(lines, capacity * sizeof(*lines));
        }
        lines[numLines++] = strdup("/* auto added */");
        autoAddedIndex = numLines - 1;
    }

    // 4) Prepare the new line
    char newLine[256];
    snprintf(newLine, sizeof(newLine),
             "DISTRO(\"%s\", \"%s\", \"\")",
             distroId, capitalized);

    // 5) Check if newLine already exists in the "auto added" section.
    //
    //    We'll define the “auto added” section as everything from
    //    `autoAddedIndex+1` to the end of the file (or until a next special
    //    comment if you wanted to end the section).
    //
    //    For simplicity, let's just scan from `autoAddedIndex+1` to the end:
    bool alreadyExists = false;
    for (size_t i = (size_t)autoAddedIndex + 1; i < numLines; i++) {
        // If we hit a line that starts with "/* " (a new comment block),
        // we could break if we wanted to end the “auto added” block there.
        // But if you want to keep auto added going to the file's end,
        // just skip that logic.
        //
        // Now check for an exact match:
        if (strcmp(lines[i], newLine) == 0) {
            alreadyExists = true;
            break;
        }
    }

    if (!alreadyExists) {
        // We actually want to insert the new distro line

        // Grow if needed
        if (numLines + 1 >= capacity) {
            capacity *= 2;
            lines = realloc(lines, capacity * sizeof(*lines));
        }
        // Shift everything after autoAddedIndex by one slot
        for (size_t i = numLines; i > (size_t)autoAddedIndex + 1; i--) {
            lines[i] = lines[i - 1];
        }
        // Put new line right after autoAddedIndex
        lines[autoAddedIndex + 1] = strdup(newLine);
        numLines++;

        // We'll rewrite the file at the end
        insertedSomething = true;
    }

    // 6) Rewrite the entire file only if we inserted something
    if (insertedSomething) {
        FILE *outFile = fopen(defPath, "w");
        if (!outFile) {
            fprintf(stderr, "Failed to rewrite %s: %s\n", defPath, strerror(errno));
            // Clean up
            for (size_t i = 0; i < numLines; i++) {
                free(lines[i]);
            }
            free(lines);
            return false;
        }
        for (size_t i = 0; i < numLines; i++) {
            fprintf(outFile, "%s\n", lines[i]);
            free(lines[i]);
        }
        fclose(outFile);
    }
    else {
        // Even if we didn't insert, we should free the lines
        for (size_t i = 0; i < numLines; i++) {
            free(lines[i]);
        }
        free(lines);
    }

    return insertedSomething;
}

static void get_definitions_file_path(char *resolvedBuf, size_t size) {
#ifdef _WIN32
    char exePath[PATH_MAX];
    DWORD len = GetModuleFileNameA(NULL, exePath, (DWORD)(sizeof(exePath) - 1));
    if (len == 0 || len >= sizeof(exePath)) {
        snprintf(resolvedBuf, size, "data\\distros.def");
        return;
    }
    exePath[len] = '\0';

    char *last_sep = strrchr(exePath, '\\');
    if (!last_sep) last_sep = strrchr(exePath, '/');
    if (last_sep) {
        *last_sep = '\0';
    }

    snprintf(resolvedBuf, size, "%s\\data\\distros.def", exePath);
    ensure_parent_dir_exists(resolvedBuf);
    return;
#else
    // 1. Read the path of the running executable.
    char exePath[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len == -1) {
        fprintf(stderr, "Failed to read /proc/self/exe\n");
        // Fallback
        snprintf(resolvedBuf, size, "data/distros.def");
        return;
    }
    exePath[len] = '\0';

    // 2. Extract the directory part (of the current executable).
    //    e.g. if exePath == "/home/frank/cupidfetch/cupidfetch",
    //    then dir == "/home/frank/cupidfetch"
    char *dir = path_dirname(exePath);

    // 3. Build a path to distros.def (NO ../ now).
    //    e.g. "/home/frank/cupidfetch/data/distros.def"
    char tmpBuf[PATH_MAX];
    snprintf(tmpBuf, sizeof(tmpBuf), "%s/data/distros.def", dir);

    // 4. Make sure the data directory exists.
    {
        char tmpDir[PATH_MAX];
        strncpy(tmpDir, tmpBuf, sizeof(tmpDir));
        tmpDir[PATH_MAX - 1] = '\0';
        char *dataDir = path_dirname(tmpDir);
        mkdir(dataDir, 0755);
    }

    // 5. realpath() that full path to distros.def.
    //    If the file doesn't exist yet, realpath() can fail;
    //    so we fall back to tmpBuf in that case.
    if (!realpath(tmpBuf, resolvedBuf)) {
        snprintf(resolvedBuf, size, "%s", tmpBuf);
    }
#endif
}


const char* detect_linux_distro()
{
    if (g_forced_distro[0] != '\0') {
        return g_forced_distro;
    }

    if (g_distro_cached && g_distro_cache[0] != '\0') {
        return g_distro_cache;
    }

    if (!g_distros_loaded) {
        char defPath[PATH_MAX];
        get_definitions_file_path(defPath, sizeof(defPath));
        parse_distros_def(defPath);
    }

#ifdef _WIN32
    snprintf(g_distro_cache, sizeof(g_distro_cache), "%s", detect_windows_distro_name());
    g_distro_cached = true;
    return g_distro_cache;
#else
    // 1) Read /etc/os-release
    FILE* os_release = fopen("/etc/os-release", "r");
    if (!os_release) {
        fprintf(stderr, "Error opening /etc/os-release\n");
        exit(EXIT_FAILURE);
    }

    char line[256];
    char distroId[128] = "unknown";
    while (fgets(line, sizeof(line), os_release)) {
        if (cf_parse_os_release_id_line(line, distroId, sizeof(distroId))) {
            break;
        }
    }
    fclose(os_release);

    // 2) Check if known in g_knownDistros
    for (size_t i = 0; i < g_numKnown; i++) {
        if (strcmp(distroId, g_knownDistros[i].shortname) == 0) {
            // Found it => return the "long name"
            snprintf(g_distro_cache, sizeof(g_distro_cache), "%s", g_knownDistros[i].longname);
            g_distro_cached = true;
            return g_distro_cache;
        }
    }

    // Not found => unknown
    fprintf(stderr, "Warning: Unknown distribution '%s'\n", distroId);

    // 3) Auto-add it to distros.def
    char defPath[PATH_MAX];
    get_definitions_file_path(defPath, sizeof(defPath));

    // CHANGED: Create a separate capitalized name
    char capitalized[128];
    strncpy(capitalized, distroId, sizeof(capitalized)-1);
    capitalized[sizeof(capitalized)-1] = '\0';

    if (capitalized[0]) {
        capitalized[0] = (char)toupper((unsigned char)capitalized[0]);
    }

    // CHANGED: Pass the lower distroId as shortname, capitalized as longname
    bool inserted = insert_auto_added_distro(defPath, 
                                             distroId,         // shortname, e.g. "arch"
                                             capitalized);     // longname,  e.g. "Arch"
    if (inserted) {
        printf("Auto-updated %s with a new entry for '%s'\n", defPath, distroId);
        // Also, re-parse so that a subsequent call sees it immediately (optional)
        parse_distros_def(defPath);
    }

    // CHANGED: Return the capitalized version as "Distro"
    snprintf(g_distro_cache, sizeof(g_distro_cache), "%s", capitalized);
    g_distro_cached = true;
    return g_distro_cache;
#endif
}

void display_fetch() {
	// Fetch system information
	char hostname[256];
	const char *detectedDistro = NULL;
    const char *username = getenv("USER");
    char *login_name = NULL;

#ifdef _WIN32
    if (username == NULL || username[0] == '\0') {
        username = getenv("USERNAME");
    }
#endif

#ifndef _WIN32
    if (username == NULL || username[0] == '\0') {
        login_name = getlogin();
        if (login_name != NULL && login_name[0] != '\0') {
            username = login_name;
        }
    }

    if (username == NULL || username[0] == '\0') {
	    // Fallback: try to retrieve the username using getpwuid
	    struct passwd *pw = getpwuid(geteuid());
	    if (pw != NULL) {
	        username = pw->pw_name;
	    } else {
	        cupid_log(LogType_ERROR, "couldn't get username");
	        username = "";
	    }
	}
#endif

    if (username == NULL || username[0] == '\0') {
        username = "unknown";
    }
	
#ifdef _WIN32
    const char *computer_name = getenv("COMPUTERNAME");
    if (computer_name && computer_name[0]) {
        strncpy(hostname, computer_name, sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    } else {
        hostname[0] = '\0';
    }
#else
    if (gethostname(hostname, sizeof(hostname)) != 0) {
		cupid_log(LogType_ERROR, "couldn't get hostname");
		hostname[0] = '\0';
	}
#endif

    // Construct the username@hostname string
	char user_host[512];
	snprintf(user_host, sizeof(user_host), "%s@%s", username, hostname);

    begin_info_capture();

	for (size_t i = 0; g_userConfig.modules[i]; i++) {
		g_userConfig.modules[i]();
	}

    end_info_capture();

    if (g_json_output) {
        render_json_output(user_host);
        fflush(stdout);
        return;
    }

	detectedDistro = detect_linux_distro();

    // Clear screen for a clean redraw
    printf("\033[H\033[J");

    render_fetch_panel(detectedDistro, user_host);
	fflush(stdout); // Ensure the buffer is flushed after each draw
}

void handle_sigwinch(int sig) {
	resize_flag = 1; // Set the flag to indicate resize
}

void setup_signal_handlers() {
#ifdef _WIN32
    return;
#else
	// Register SIGWINCH handler
	struct sigaction sa;
	sa.sa_handler = handle_sigwinch;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART; // Restart interrupted system calls
	if (sigaction(SIGWINCH, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
#endif
}

int main(int argc, char **argv) {
    if (!parse_cli_args(argc, argv)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Initialize configuration with defaults.
    init_g_config();
    g_log = NULL;

    if (!g_json_output) {
        // Set up signal handlers.
        setup_signal_handlers();
    }

    // Set up logging.
    if (!isatty(STDERR_FILENO))
        g_log = stderr;

    if (g_log == NULL) {
        char log_path[CONFIG_PATH_SIZE];
        char *config_dir = getenv("XDG_CONFIG_HOME");
#ifdef _WIN32
        if (!config_dir || !config_dir[0]) {
            config_dir = getenv("APPDATA");
        }
#endif
        if (config_dir) {
            snprintf(log_path, sizeof(log_path), "%s/cupidfetch/log.txt", config_dir);
        } else {
            const char* home = get_home_directory();
            snprintf(log_path, sizeof(log_path), "%s/.config/cupidfetch/log.txt", home);
        }
        g_log = fopen(log_path, "w");
        if (g_log == NULL) {
            g_log = stderr;
            cupid_log(LogType_ERROR, "Couldn't open log file, logging to stderr");
        }
    }

    // Determine the configuration file path.
    char config_path[CONFIG_PATH_SIZE];
    char *config_dir = getenv("XDG_CONFIG_HOME");
#ifdef _WIN32
    if (!config_dir || !config_dir[0]) {
        config_dir = getenv("APPDATA");
    }
#endif
    if (config_dir) {
        snprintf(config_path, sizeof(config_path), "%s/cupidfetch/cupidfetch.conf", config_dir);
    } else {
        const char* home = get_home_directory();
        snprintf(config_path, sizeof(config_path), "%s/.config/cupidfetch/cupidfetch.conf", home);
    }
    if (access(config_path, F_OK) == -1) {
        cupid_log(LogType_WARNING, "Couldn't open config file: %s. Using default config.", config_path);
    } else {
        load_config_file(config_path, &g_userConfig);
    }

    // Display system information initially.
    display_fetch();

    if (g_json_output) {
        epitaph();
        return EXIT_SUCCESS;
    }

#ifndef _WIN32
    // Main loop.
    while (1) {
        pause(); // Wait for a signal.

        if (resize_flag) {
            printf("\033[H\033[J");
            display_fetch();
            resize_flag = 0;
        }
    }
#endif

    epitaph();
    return 0;
}

void epitaph() {
    if (g_log != stderr) fclose(g_log);
}
