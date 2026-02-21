#include "../../cupidfetch.h"

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
    const char *term_program = getenv("TERM");
#endif
    if (term_program == NULL) {
        cupid_log(LogType_ERROR, "Failed to retrieve terminal program information");
        return;
    }

    print_info("Terminal", "%s", 20, 30, term_program);
}
