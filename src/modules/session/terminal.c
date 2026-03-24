#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

#ifndef _WIN32
static bool is_likely_shell_process(const char *name) {
    if (!name || !name[0]) return false;

    static const char *shells[] = {
        "sh", "bash", "zsh", "fish", "dash", "ksh", "mksh", "ash", "csh", "tcsh", "nu", "xonsh"
    };

    for (size_t i = 0; i < sizeof(shells) / sizeof(shells[0]); i++) {
        if (strcasecmp(name, shells[i]) == 0) return true;
    }

    return false;
}

static bool is_likely_terminal_process(const char *name) {
    if (!name || !name[0]) return false;

    if (cf_contains_icase(name, "terminal") || cf_contains_icase(name, "term")) return true;

    static const char *known_terminals[] = {
        "xterm", "rxvt", "urxvt", "kitty", "alacritty", "wezterm", "konsole", "tilix", "foot", "ghostty",
        "st", "rio", "warp", "tabby", "hyper"
    };

    for (size_t i = 0; i < sizeof(known_terminals) / sizeof(known_terminals[0]); i++) {
        if (strcasecmp(name, known_terminals[i]) == 0) return true;
    }

    return false;
}

static bool read_proc_comm(pid_t pid, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return false;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);

    if (!cf_read_first_line(path, buffer, buffer_size)) return false;
    return buffer[0] != '\0';
}

static bool read_parent_pid(pid_t pid, pid_t *ppid_out) {
    if (!ppid_out) return false;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char stat_line[1024];
    if (!fgets(stat_line, sizeof(stat_line), fp)) {
        fclose(fp);
        return false;
    }
    fclose(fp);

    char *rparen = strrchr(stat_line, ')');
    if (!rparen || rparen[1] != ' ') return false;

    char state = '\0';
    long ppid_long = 0;
    if (sscanf(rparen + 2, "%c %ld", &state, &ppid_long) != 2) return false;
    if (ppid_long <= 0) return false;

    *ppid_out = (pid_t)ppid_long;
    return true;
}

static bool detect_terminal_from_process_tree(char *terminal_out, size_t terminal_out_size) {
    if (!terminal_out || terminal_out_size == 0) return false;

    pid_t pid = getppid();
    for (int depth = 0; depth < 32 && pid > 1; depth++) {
        char comm[128] = {0};
        if (!read_proc_comm(pid, comm, sizeof(comm))) break;

        if (strcasecmp(comm, "cupidfetch") != 0 && !is_likely_shell_process(comm) && is_likely_terminal_process(comm)) {
            strncpy(terminal_out, comm, terminal_out_size - 1);
            terminal_out[terminal_out_size - 1] = '\0';
            return true;
        }

        pid_t next_pid = 0;
        if (!read_parent_pid(pid, &next_pid) || next_pid == pid) break;
        pid = next_pid;
    }

    return false;
}
#endif

void get_terminal() {
    if (!isatty(STDOUT_FILENO)) {
#ifdef _WIN32
        return;
#else
        cupid_log(LogType_ERROR, "Not running in a terminal");
        return;
#endif
    }

#ifdef _WIN32
    const char *term_program = getenv("WT_SESSION");
    if (term_program && term_program[0]) {
        print_info("Terminal", "%s", 20, 30, "Windows Terminal");
        return;
    }
    term_program = getenv("TERM_PROGRAM");
    if (term_program == NULL) term_program = getenv("TERM");
    if (term_program == NULL) term_program = getenv("ComSpec");
#else
    char detected_terminal[128] = {0};
    const char *term_program = NULL;

    if (detect_terminal_from_process_tree(detected_terminal, sizeof(detected_terminal))) {
        term_program = detected_terminal;
    } else {
        term_program = getenv("TERM_PROGRAM");
        if (term_program == NULL || term_program[0] == '\0') term_program = getenv("LC_TERMINAL");
        if (term_program == NULL || term_program[0] == '\0') term_program = getenv("TERMINAL_EMULATOR");
        if (term_program == NULL || term_program[0] == '\0') term_program = getenv("TERM");
    }
#endif
    if (term_program == NULL || term_program[0] == '\0') {
        cupid_log(LogType_ERROR, "Failed to retrieve terminal program information");
        return;
    }

    print_info("Terminal", "%s", 20, 30, term_program);
}
