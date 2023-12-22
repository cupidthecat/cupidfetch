// Cupidfetch is a system information retrieval tool written in C for Linux systems. It's a beginner-friendly, work-in-progress hobby project aimed at learning and exploring programming concepts.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdarg.h>
#include "libs/ini.h"

#define LINUX_PROC_LINE_SZ 128
#define KILOBYTES 1024

int get_terminal_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

void print_info(const char *key, const char *format, int align_key, int align_value, ...) {
    va_list args;
    va_start(args, align_value);

    // Print the key with left alignment
    printf("%-*s: ", align_key, key);

    // Print the formatted value with specified alignment
    vprintf(format, args);

    va_end(args);

    // Add a newline at the end
    printf("\n");
}

void print_cat(const char* distro) {
    if (strcmp(distro, "Ubuntu") == 0 || strcmp(distro, "Debian") == 0 ||
        strcmp(distro, "Arch") == 0 || strcmp(distro, "CentOS") == 0) {
        printf(" /\\_/\\\n");
        printf("( o.o )\n");
        printf(" > ^ <  cupidfetch");
    } else {
        printf(" /\\_/\\\n");
        printf("( x.x )\n");
        printf(" > ^ <  cupidfetch");
    }
}

void get_host_name() {
    char host_name[256];
    if (gethostname(host_name, sizeof(host_name)) != 0) {
        fprintf(stderr, "Error getting host name\n");
        exit(EXIT_FAILURE);
    }
    print_info("Host Name", host_name, 20, 30);
}

void get_username() {
    char* username = getlogin();
    if (username != NULL) {
        print_info("Username", username, 20, 30);
    } else {
        printf("Error fetching username\n");
    }
}

// Define a structure to hold distro information
struct DistroInfo {
    const char* shortname;
    const char* longname;
};

const char* detect_linux_distro() {
    FILE* os_release = fopen("/etc/os-release", "r");
    if (os_release == NULL) {
        fprintf(stderr, "Error opening /etc/os-release\n");
        exit(EXIT_FAILURE);
    }

    char line[256];
    const char* distro = "Unknown";

    // Define an array of DistroInfo structs for supported distros
    struct DistroInfo supported_distros[] = {
            {"arch", "Arch Linux"},
            {"debian", "Debian"},
            {"ubuntu", "Ubuntu"},
            {"centos", "CentOS"},
            {"fedora", "Fedora"},
            {"redhat", "Red Hat"},
            {"opensuse", "openSUSE"},
            {"gentoo", "Gentoo"},
            {"alpine", "Alpine Linux"},
            {"slackware", "Slackware"},
            {"manjaro", "Manjaro"},
            {"elementary", "elementary OS"},
            {"mint", "Linux Mint"},
            {"kali", "Kali Linux"},
            {"zorin", "Zorin OS"},
            {"void", "Void Linux"},
            {"alma", "AlmaLinux"},
            {"artix", "Artix Linux"},
            {"endeavouros", "EndeavourOS"},
            {"mx", "MX Linux"},
            {"peppermint", "Peppermint OS"},
            {"pop", "Pop!_OS"},
            {"solus", "Solus"},
            {"antergos", "Antergos"},
            {"mageia", "Mageia"}
            // Add more distros as needed
    };

    while (fgets(line, sizeof(line), os_release)) {
        if (strstr(line, "ID=") != NULL) {
            char* distroId = strchr(line, '=') + 1;
            distroId[strcspn(distroId, "\n")] = '\0';

            // Convert to lowercase for case-insensitive comparison
            for (int i = 0; distroId[i]; i++) {
                distroId[i] = tolower(distroId[i]);
            }

            // Check if the distroId is in the list of supported distros
            int supported = 0;
            for (int i = 0; i < sizeof(supported_distros) / sizeof(supported_distros[0]); i++) {
                if (strstr(distroId, supported_distros[i].shortname) != NULL) {
                    supported = 1;
                    distro = supported_distros[i].longname;
                    break;
                }
            }

            if (!supported) {
                printf("Warning: Unknown distribution '%s'\n", distroId);
                break; // Continue without updating distro
            }

            break;
        }
    }

    fclose(os_release);
    return distro;
}

void get_linux_kernel() {
    struct utsname uname_data;
    if (uname(&uname_data) != 0) {
        fprintf(stderr, "Error getting Linux kernel information\n");
        exit(EXIT_FAILURE);
    }

    print_info("Linux Kernel", uname_data.release, 20, 30);
}

void get_uptime() {
    FILE* uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file == NULL) {
        fprintf(stderr, "Error fetching uptime\n");
        exit(EXIT_FAILURE);
    }

    double uptime;
    if (fscanf(uptime_file, "%lf", &uptime) != 1) {
        fprintf(stderr, "Error reading uptime\n");
        exit(EXIT_FAILURE);
    }
    fclose(uptime_file);

    int days = (int)uptime / (60 * 60 * 24);
    int hours = ((int)uptime % (60 * 60 * 24)) / (60 * 60);
    int minutes = ((int)uptime % (60 * 60)) / 60;

    // Corrected usage of print_info
    print_info("Uptime", "%d days, %02d:%02d", 20, 30, days, hours, minutes);
}

// Define a structure to hold distro-specific information
struct DistroPackageInfo {
    const char* distro;
    const char* package_command;
};

void get_package_count(const char* distro) {
    const char* package_command = NULL;

    // List of supported distros and their package commands
    struct DistroPackageInfo supported_distros[] = {
            {"Ubuntu", "dpkg -l | tail -n+6 | wc -l"},
            {"Debian", "dpkg -l | tail -n+6 | wc -l"},
            {"elementary OS", "dpkg -l | tail -n+6 | wc -l"},
            {"Zorin OS", "dpkg -l | tail -n+6 | wc -l"},
            {"Pop!_OS", "dpkg -l | tail -n+6 | wc -l"},
            {"MX Linux", "dpkg -l | tail -n+6 | wc -l"},
            {"Kali Linux", "dpkg -l | tail -n+6 | wc -l"},
            {"Parrot OS", "dpkg -l | tail -n+6 | wc -l"},
            {"Raspbian", "dpkg -l | tail -n+6 | wc -l"},
            {"Raspberry Pi OS", "dpkg -l | tail -n+6 | wc -l"},
            {"Arch Linux", "pacman -Q | wc -l"},
            {"Manjaro", "pacman -Q | wc -l"},
            {"Artix Linux", "pacman -Q | wc -l"},
            {"BlueStar", "pacman -Q | wc -l"},
            {"EndeavourOS", "pacman -Q | wc -l"},
            {"CentOS", "rpm -qa | wc -l"},
            {"Fedora", "rpm -qa | wc -l"},
            {"openSUSE", "rpm -qa | wc -l"},
            {"Mageia", "rpm -qa | wc -l"},
            {"AlmaLinux", "rpm -qa | wc -l"},
            {"Rocky Linux", "rpm -qa | wc -l"},
            {"Amazon Linux", "rpm -qa | wc -l"},
            {"Gentoo", "equery -q list '*' | wc -l"},
            {"Alpine Linux", "apk info | wc -l"},
            {"Void Linux", "xbps-query -l | wc -l"},
            {"Slackware", "ls /var/log/packages/ | wc -l"},
            {"Solus", "eopkg list-installed | wc -l"},
            {"NixOS", "nix-env --list-generations | wc -l"},
            {"Antergos", "pacman -Q | wc -l"},
            {"Linux Mint", "dpkg -l | tail -n+6 | wc -l"},
            {"Peppermint OS", "dpkg -l | tail -n+6 | wc -l"}
            // Add more distros as needed
    };

    // Find the package command for the provided distro
    for (int i = 0; i < sizeof(supported_distros) / sizeof(supported_distros[0]); i++) {
        if (strcmp(distro, supported_distros[i].distro) == 0) {
            package_command = supported_distros[i].package_command;
            break;
        }
    }

    if (package_command == NULL) {
        printf("Error: Unsupported distribution '%s'\n", distro);
        return; // Return without running the command
    }

    // Run the package command and display the result
    FILE* fp = popen(package_command, "r");
    if (fp == NULL) {
        printf("Failed to run command\n");
        exit(EXIT_FAILURE);
    }

    char output[100];
    if (fgets(output, sizeof(output), fp) != NULL) {
        output[strcspn(output, "\n")] = 0; // Remove newline character if present
        print_info("Package Count", "%s", 20, 30, output); // Corrected usage
    } else {
        printf("Error reading package count\n");
    }

    pclose(fp);
}


void get_shell() {
    FILE *fp;
    char path[1035];

    fp = popen("basename $SHELL", "r");
    if (fp == NULL) {
        printf("Failed to run command\n");
        exit(EXIT_FAILURE);
    }

    fgets(path, sizeof(path) - 1, fp);
    if (strlen(path) > 0) {
        path[strlen(path) - 1] = '\0'; // Remove newline character
    }

    print_info("Shell", path, 20, 30);
    pclose(fp);
}

const char* get_terminal() {
    const char* term_program = getenv("TERM");

    if (term_program != NULL) {
        // Get the terminal type from the TERM environment variable
        return term_program;
    } else {
        if (isatty(STDOUT_FILENO)) {
            // Check if stdout is a terminal
            struct winsize w;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
                return "Unknown (Interactive)";
            } else {
                return "Unknown (Non-Interactive)";
            }
        } else {
            return "Not a terminal";
        }
    }
}

void get_desktop_environment() {
    FILE *fp;
    char path[1035];

    // Try to get desktop environment using $XDG_CURRENT_DESKTOP
    fp = popen("echo $XDG_CURRENT_DESKTOP", "r");
    if (fp != NULL) {
        fgets(path, sizeof(path) - 1, fp);
        // Check if the string contains non-whitespace characters
        if (strspn(path, " \t\n") < strlen(path)) {
            path[strlen(path) - 1] = '\0'; // Remove newline character
            print_info("DE", path, 20, 30);
            pclose(fp);
            return; // Successfully retrieved desktop environment
        }
        pclose(fp);
    }

    // If $XDG_CURRENT_DESKTOP is empty, try other methods

    if (getenv("GNOME_DESKTOP_SESSION_ID") != NULL || getenv("XDG_CURRENT_DESKTOP") != NULL) {
        print_info("DE", "GNOME", 20, 30);
        return; // Successfully detected GNOME
    }

    if (getenv("KDE_FULL_SESSION") != NULL || getenv("KDE_SESSION_VERSION") != NULL) {
        print_info("DE", "KDE", 20, 30);
        return; // Successfully detected KDE
    }

    if (getenv("XFCE_SESSION") != NULL || getenv("XDG_CURRENT_DESKTOP") != NULL) {
        print_info("DE", "Xfce", 20, 30);
        return; // Successfully detected Xfce
    }

    // Method 4: Check for FVWM using ps command
    fp = popen("ps -e | grep fvwm | grep -v grep", "r");
    if (fp != NULL && fgets(path, sizeof(path) - 1, fp) != NULL) {
        print_info("DE", "FVWM", 20, 30);
        pclose(fp);
        return; // Successfully detected FVWM
    }
    if (fp != NULL) {
        pclose(fp);
    }

    if (getenv("LXQT_SESSION") != NULL || getenv("XDG_CURRENT_DESKTOP") != NULL) {
        print_info("DE", "LXQt", 20, 30);
        return; // Successfully detected LXQt
    }

    if (getenv("MATE_DESKTOP_SESSION_ID") != NULL || getenv("XDG_CURRENT_DESKTOP") != NULL) {
        print_info("DE", "MATE", 20, 30);
        return; // Successfully detected MATE
    }

    if (getenv("DESKTOP_SESSION") != NULL && strstr(getenv("DESKTOP_SESSION"), "cinnamon") != NULL) {
        print_info("DE", "Cinnamon", 20, 30);
        return; // Successfully detected Cinnamon
    }

    if (getenv("PANTHEON_DESKTOP_SESSION") != NULL || getenv("XDG_CURRENT_DESKTOP") != NULL) {
        print_info("DE", "Pantheon", 20, 30);
        return; // Successfully detected Pantheon
    }


    // Fallback: If all methods fail, print Unknown
    print_info("DE", "Unknown", 20, 30);
}

void display_local_ip() {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        printf("Error retrieving IP address.\n");
        return;
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr != NULL && ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char ip_addr[16]; // Allocate enough space for an IPv6 address
            snprintf(ip_addr, sizeof(ip_addr), "%s", inet_ntoa(addr->sin_addr));

            // Check if loopback interface and skip if needed
            if (strcmp(ifa->ifa_name, "lo") == 0) {
                continue;
            }

            // Print with dynamic padding for right alignment
            print_info("Local IP", "%s", 20, 30, ip_addr);
            break;
        }
    }

    freeifaddrs(ifaddr);
}

void get_available_memory() {
    // Linux-specific implementation
    // Source: https://github.com/KittyKatt/screenFetch/issues/386#issuecomment-249312716
    // Also used in neofetch
    ssize_t mem_avail = -1, mem_total = -1;
    long mem_used = 0;
    FILE* meminfo;
    char line[LINUX_PROC_LINE_SZ];

    meminfo = fopen("/proc/meminfo", "r");

    if (meminfo == NULL) {
        fprintf(stderr, "Error: Failed to open /proc/meminfo\n");
        return;
    }

    while (fgets(line, sizeof line, meminfo)) {
        char *value = NULL;
        size_t vnum;
        size_t i, len = 0, vlen = 0;

        for (i = 0; line[i]; i++) {
            if (!len && line[i] == ':')
                len = i;
            if (len && !value && isdigit(line[i]))
                value = &line[i];
            if (len && value && isdigit(line[i]))
                vlen = 1 + &line[i] - value;
        }
        if (!len || !vlen || !value)
            continue;

        line[len] = '\0';
        value[vlen] = '\0';

        if (1 != sscanf(value, "%zu", &vnum))
            continue;

        if (0 == strcmp("MemTotal", line)) {
            mem_total = vnum;
            mem_used += vnum;
        } else if (0 == strcmp("MemAvailable", line))
            mem_avail = vnum;
        else if (0 == strcmp("Shmem", line))
            mem_used += vnum;
        else if (0 == strcmp("MemFree", line))
            mem_used -= vnum;
        else if (0 == strcmp("Buffers", line))
            mem_used -= vnum;
        else if (0 == strcmp("Cached", line))
            mem_used -= vnum;
        else if (0 == strcmp("SReclaimable", line))
            mem_used -= vnum;

        if (mem_total != -1 && mem_avail != -1)
            break;
    }

    fclose(meminfo);

    if (mem_total == -1) {
        fprintf(stderr, "Error: Failed to retrieve total memory.\n");
        return;
    }

    if (mem_avail != -1) {
        mem_used = mem_total - mem_avail;
    }

    print_info(
            "Memory", "%ld MiB / %ld MiB", 20, 30,
            mem_used / KILOBYTES, mem_total / KILOBYTES
    );
}

const char* get_home_directory() {
    const char* homeDir;
    if ((homeDir = getenv("HOME")) == NULL) {
        struct passwd* pw = getpwuid(getuid());
        if (pw != NULL) {
            homeDir = pw->pw_dir;
        }
    }
    return homeDir;
}

void display_host_name() {
    get_host_name();
}

void display_shell() {
    get_shell();
}

void display_desktop_environment() {
    get_desktop_environment();
}

void display_package_count(const char* distro) {
    get_package_count(distro);
}

void display_linux_kernel() {
    get_linux_kernel();
}

void display_uptime() {
    get_uptime();
}

void display_available_memory() {
    get_available_memory();
}

struct CupidConfig {
    int display_host_name;
    int display_username;
    int display_distro;
    int display_linux_kernel;
    int display_uptime;
    int display_package_count;
    int display_shell;
    int display_terminal;
    int display_desktop_environment;
    int display_local_ip;
    int display_available_memory;
};

void create_default_config(const char* config_path, const struct CupidConfig* default_config) {
    FILE* config_file = fopen(config_path, "w");
    if (config_file == NULL) {
        fprintf(stderr, "Error creating config file: %s\n", config_path);
        exit(EXIT_FAILURE);
    }

    fprintf(config_file, "[DisplayOptions]\n");
    fprintf(config_file, "host_name = %d\n", default_config->display_host_name);
    fprintf(config_file, "username = %d\n", default_config->display_username);
    fprintf(config_file, "distro = %d\n", default_config->display_distro);
    fprintf(config_file, "linux_kernel = %d\n", default_config->display_linux_kernel);
    fprintf(config_file, "uptime = %d\n", default_config->display_uptime);
    fprintf(config_file, "package_count = %d\n", default_config->display_package_count);
    fprintf(config_file, "shell = %d\n", default_config->display_shell);
    fprintf(config_file, "desktop_environment = %d\n", default_config->display_desktop_environment);
    fprintf(config_file, "local_ip = %d\n", default_config->display_local_ip);
    fprintf(config_file, "available_memory = %d\n", default_config->display_available_memory);

    fclose(config_file);
}

void write_config_to_file(const char* config_path, const struct CupidConfig* user_config) {
    FILE* config_file = fopen(config_path, "w");
    if (config_file == NULL) {
        fprintf(stderr, "Error writing to config file: %s\n", config_path);
        exit(EXIT_FAILURE);
    }

    fprintf(config_file, "[DisplayOptions]\n");
    fprintf(config_file, "host_name = %d\n", user_config->display_host_name);
    fprintf(config_file, "username = %d\n", user_config->display_username);
    fprintf(config_file, "distro = %d\n", user_config->display_distro);
    fprintf(config_file, "linux_kernel = %d\n", user_config->display_linux_kernel);
    fprintf(config_file, "uptime = %d\n", user_config->display_uptime);
    fprintf(config_file, "package_count = %d\n", user_config->display_package_count);
    fprintf(config_file, "shell = %d\n", user_config->display_shell);
    fprintf(config_file, "terminal = %d\n", user_config->display_terminal);
    fprintf(config_file, "desktop_environment = %d\n", user_config->display_desktop_environment);
    fprintf(config_file, "local_ip = %d\n", user_config->display_local_ip);
    fprintf(config_file, "available_memory = %d\n", user_config->display_available_memory);

    fclose(config_file);
}

// INI handler function
int iniHandler(void* user, const char* section, const char* name, const char* value) {
    struct CupidConfig* config = (struct CupidConfig*)user;

    // Check the section and update corresponding display options
    if (strcmp(section, "DisplayOptions") == 0) {
        if (strcmp(name, "host_name") == 0) {
            config->display_host_name = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "username") == 0) {
            config->display_username = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "distro") == 0) {
            config->display_distro = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "linux_kernel") == 0) {
            config->display_linux_kernel = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "uptime") == 0) {
            config->display_uptime = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "package_count") == 0) {
            config->display_package_count = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "shell") == 0) {
            config->display_shell = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "terminal") == 0) {
            config->display_terminal = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "desktop_environment") == 0) {
            config->display_desktop_environment = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "local_ip") == 0) {
            config->display_local_ip = (value != NULL) ? atoi(value) : 1;
        } else if (strcmp(name, "available_memory") == 0) {
            config->display_available_memory = (value!= NULL)? atoi(value) : 1;
        }
    }

    return 1;
}

int main() {
    // Declare and initialize the configuration
    struct CupidConfig userConfig = {
            .display_host_name = 1,
            .display_username = 1,
            .display_distro = 1,
            .display_linux_kernel = 1,
            .display_uptime = 1,
            .display_package_count = 1,
            .display_shell = 1,
            .display_terminal = 1,
            .display_desktop_environment = 1,
            .display_local_ip = 1,
            .display_available_memory = 1,
    };

    // Determine the home directory of the current user
    const char* homeDir = get_home_directory();

    // Construct the path for the config file
    char configPath[256];
    snprintf(configPath, sizeof(configPath), "%s/.config/cupidfetch/cupidfetch.ini", homeDir);

    // Check if the config directory exists, if not, create it
    const char* configDir = ".config/cupidfetch";
    char configDirPath[256];
    snprintf(configDirPath, sizeof(configDirPath), "%s/%s", homeDir, configDir);

    if (mkdir(configDirPath, 0700) != 0 && errno != EEXIST) {
        // If mkdir failed and the error is not EEXIST, try creating parent directories
        char parentDirPath[256];
        snprintf(parentDirPath, sizeof(parentDirPath), "%s/%s", homeDir, ".config");
        if (mkdir(parentDirPath, 0700) != 0 && errno != EEXIST) {
            fprintf(stderr, "Error creating config directory: %s\n", configDirPath);
            perror("mkdir");
            exit(EXIT_FAILURE);
        }

        // Retry creating the config directory
        if (mkdir(configDirPath, 0700) != 0 && errno != EEXIST) {
            fprintf(stderr, "Error creating config directory: %s\n", configDirPath);
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    }

    // Fetch system information
    const char* detectedDistro = detect_linux_distro();
    char hostName[256];
    char* username = getlogin();

    // Check for errors getting host name
    if (gethostname(hostName, sizeof(hostName)) != 0) {
        fprintf(stderr, "Error getting host name\n");
        exit(EXIT_FAILURE);
    }

    // Calculate alignment spaces
    int artWidth = 18;
    int totalWidth = 50;
    int textWidth = strlen(username) + strlen(hostName) + 1;
    int spaces = totalWidth - artWidth - textWidth;

    // Display username@hostname
    printf("\n%*s%s@%s\n", spaces, "", username, hostName);

    // Display cat ASCII art based on the detected distribution
    print_cat(detectedDistro);

    // Display system information based on user configuration
    printf("\n");
    printf("-----------------------------------------\n");

    // Check if the config file exists
    if (access(configPath, F_OK) != -1) {
        // Config file exists, load configuration from the file
        int parse_result = ini_parse(configPath, iniHandler, &userConfig);
        if (parse_result < 0) {
            fprintf(stderr, "Error parsing INI file: %s\n", strerror(parse_result));
            exit(EXIT_FAILURE);
        }
    } else {
        // Config file doesn't exist, create the default configuration file
        create_default_config(configPath, &userConfig);
    }

    // Display system information based on loaded or default user configuration
    if (userConfig.display_host_name) {
        display_host_name();
    }
    if (userConfig.display_username) {
        get_username();
    }
    if (userConfig.display_distro) {
        print_info("Distro", detectedDistro, 20, 30);
    }
    if (userConfig.display_linux_kernel) {
        display_linux_kernel();
    }
    if (userConfig.display_uptime) {
        display_uptime();
    }
    if (userConfig.display_package_count) {
        display_package_count(detectedDistro);
    }
    if (userConfig.display_shell) {
        display_shell();
    }
    if (userConfig.display_terminal) {
        const char* terminal_program = get_terminal();
        print_info("Terminal", "%s", 20, 30, terminal_program);
    }
    if (userConfig.display_desktop_environment) {
        get_desktop_environment();
    }
    if (userConfig.display_local_ip) {
        display_local_ip();
    }
    if (userConfig.display_available_memory) {
        get_available_memory();  // Call get_available_memory only if the option is set to 1
    }

    return 0;
}   



// Path: libs/ini.h
