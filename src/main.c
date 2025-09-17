// File: main.c
// -----------------------
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h> // for mkdir
#include <unistd.h>   // for pause()
#include <stdio.h>    // for printf
#include <stdlib.h>   // for exit
#include <limits.h>   // For PATH_MAX
#include <libgen.h>   // For dirname()
#include <string.h>
#include <errno.h>    // for errno, strerror
#include <stdbool.h>  // for bool type
#include <regex.h>    // or just do manual parsing
#include <pwd.h> 
// Local Includes
#include "cupidfetch.h"

// Global Variables
FILE *g_log = NULL;
volatile sig_atomic_t resize_flag = 0; // Flag for window resize


void display_fetch() {
    // Fetch system information
    char hostname[256];
    const char *detectedDistro = detect_linux_distro();
    char *username = getlogin();

    if (username == NULL) {
        // Fallback: try to retrieve the username using getpwuid
        struct passwd *pw = getpwuid(geteuid());
        if (pw != NULL) {
            username = pw->pw_name;
        } else {
            cupid_log(LogType_ERROR, "couldn't get username");
            username = "";
        }
    }
    
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        cupid_log(LogType_ERROR, "couldn't get hostname");
        hostname[0] = '\0';
    }

    // Construct the username@hostname string
    char user_host[512];
    snprintf(user_host, sizeof(user_host), "%s@%s", username, hostname);

    // Calculate terminal width and center position
    int totalWidth = get_terminal_width();
    int textWidth = strlen(user_host);
    int spaces = (totalWidth > textWidth) ? (totalWidth - textWidth) / 2 : 0;

    // Clear screen and display username@hostname in the center
    printf("\033[H\033[J"); // Clear screen for a clean redraw
    printf("\n%*s%s\n", spaces + textWidth, "", user_host);

    // Display cat ASCII art based on the detected distribution
    print_cat(detectedDistro);

    // Display system information based on user configuration
    printf("\n");
    printf("-----------------------------------------\n");

    for (size_t i = 0; g_userConfig.modules[i]; i++) {
        g_userConfig.modules[i]();
    }
    fflush(stdout); // Ensure the buffer is flushed after each draw
}

void handle_sigwinch(int sig) {
	resize_flag = 1; // Set the flag to indicate resize
}

void setup_signal_handlers() {
	// Register SIGWINCH handler
	struct sigaction sa;
	sa.sa_handler = handle_sigwinch;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART; // Restart interrupted system calls
	if (sigaction(SIGWINCH, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
}

void epitaph() {
    if (g_log != stderr) fclose(g_log);
}

int main() {
    // Initialize configuration with defaults.
    init_g_config();
    g_log = NULL;

    char defPath[PATH_MAX];
    get_definitions_file_path(defPath, sizeof(defPath));

    // Set up signal handlers.
    setup_signal_handlers();

    // Set up logging.
    if (!isatty(STDERR_FILENO))
        g_log = stderr;

    if (g_log == NULL) {
        char log_path[CONFIG_PATH_SIZE];
        char *config_dir = getenv("XDG_CONFIG_HOME");
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

    parse_distros_def(defPath);

    // Display system information initially.
    display_fetch();

    // Main loop.
    while (1) {
        pause(); // Wait for a signal.

        if (resize_flag) {
            printf("\033[H\033[J");
            display_fetch();
            resize_flag = 0;
        }
    }

    epitaph();
    return 0;
}