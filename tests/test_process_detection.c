#include "../src/modules/common/module_helpers.h"
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

static const char *keys[] = {"XDG_SESSION_ID", "DISPLAY", "WAYLAND_DISPLAY"};
static char executable[4096];
static char needle[32];
static const char *fixture_session_id;

static void clear_session(void) {
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) unsetenv(keys[i]);
}

/* Exec makes /proc/PID/environ reflect the fixture's selected environment.
 * The pipe acknowledges readiness after exec and comm setup; no timing sleeps. */
static pid_t start_fixture(const char *argv0, const char *comm, bool separate_sid,
                           const char *key, const char *value, bool other_user) {
    int ready[2];
    assert(pipe(ready) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(ready[0]);
        alarm(15);
        if (separate_sid && setsid() < 0) _exit(90);
        clear_session();
        if (key && setenv(key, value, 1) != 0) _exit(91);
        if (fixture_session_id && setenv("XDG_SESSION_ID", fixture_session_id, 1) != 0) _exit(91);
        if (other_user) {
#ifdef __linux__
            /* Keep the already-loaded fixture usable in private build paths. */
            if (setuid(65534) != 0 || prctl(PR_SET_NAME, needle, 0, 0, 0) != 0) _exit(92);
            if (write(ready[1], "R", 1) != 1) _exit(95);
            close(ready[1]);
            for (;;) pause();
#else
            _exit(92);
#endif
        }
        char fd[32];
        snprintf(fd, sizeof(fd), "%d", ready[1]);
        execl(executable, argv0, "--fixture", fd, comm, needle, (char *)NULL);
        _exit(93);
    }
    close(ready[1]);
    char status = 0;
    ssize_t count = read(ready[0], &status, 1);
    close(ready[0]);
    assert(count == 1 && status == 'R');
    return pid;
}

static void check(const char *name, const char *argv0, const char *comm,
                  bool separate_sid, const char *key, const char *value,
                  bool other_user, bool expected) {
    pid_t pid = start_fixture(argv0, comm, separate_sid, key, value, other_user);
    struct process_match candidate = {name, "fixture"};
    bool detected = cf_detect_process_label(&candidate, 1) != NULL;
    assert(kill(pid, SIGTERM) == 0);
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    if (detected != expected) {
        fprintf(stderr, "Unexpected detection: name=%s argv0=%s comm=%s key=%s sid=%d\n",
                name, argv0, comm, key ? key : "none", separate_sid);
        abort();
    }
}

int main(int argc, char **argv) {
#ifndef __linux__
    (void)argc; (void)argv;
    puts("Process detection tests require Linux procfs; skipped.");
#else
    if (argc > 1 && strcmp(argv[1], "--fixture") == 0) {
        if (argc != 5) return 96;
        if (prctl(PR_SET_NAME, argv[3], 0, 0, 0) != 0) return 94;
        int fd = atoi(argv[2]);
        if (write(fd, "R", 1) != 1) return 95;
        close(fd);
        for (;;) pause();
    }
    /* Isolate SID fallback from unrelated processes in the test runner. */
    if (getsid(0) != getpid()) assert(setsid() >= 0);
    ssize_t len = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    assert(len > 0 && (size_t)len < sizeof(executable) - 1);
    executable[len] = '\0';
    snprintf(needle, sizeof(needle), "cf%ld", (long)getpid());
    clear_session();
    char path[256], backup[128], upper[32];
    snprintf(path, sizeof(path), "/opt/test/%s", needle);
    snprintf(backup, sizeof(backup), "ordinary-%s-backup", needle);
    snprintf(upper, sizeof(upper), "%s", needle);
    for (char *p = upper; *p; ++p) *p = (char)toupper((unsigned char)*p);

    check("i3", "ordinary-i3-backup", "cf-fixture", false, NULL, NULL, false, false);
    check("i3", "i3", "cf-fixture", false, NULL, NULL, false, true);
    check(needle, needle, "cf-fixture", false, NULL, NULL, false, true);
    check(needle, path, "cf-fixture", false, NULL, NULL, false, true);
    check(needle, upper, "cf-fixture", false, NULL, NULL, false, true);
    check(needle, "ordinary", needle, false, NULL, NULL, false, true);
    check(needle, backup, "cf-fixture", false, NULL, NULL, false, false);
    check(needle, "ordinary", backup, false, NULL, NULL, false, false);
    /* The candidate also appears as a later argument in every fixture. */
    check(needle, "ordinary", "cf-fixture", false, NULL, NULL, false, false);
    snprintf(path, sizeof(path), "/opt/%s/ordinary", needle);
    check(needle, path, "cf-fixture", false, NULL, NULL, false, false);
    check(needle, needle, "cf-fixture", true, NULL, NULL, false, false);

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        clear_session();
        assert(setenv(keys[i], "cupidfetch-session-a", 1) == 0);
        /* Graphical/login evidence survives terminal shells' separate SIDs. */
        check(needle, needle, "cf-fixture", true, keys[i], "cupidfetch-session-a", false, true);
        check(needle, needle, "cf-fixture", false, keys[i], "cupidfetch-session-b", false, false);
        check(needle, needle, "cf-fixture", true, keys[i], "cupidfetch-session-b", false, false);
    }
    clear_session();
    fixture_session_id = "cupidfetch-shared-login";
    assert(setenv("XDG_SESSION_ID", fixture_session_id, 1) == 0);
    assert(setenv("DISPLAY", "cupidfetch-display-a", 1) == 0);
    check(needle, needle, "cf-fixture", false, "DISPLAY", "cupidfetch-display-b", false, false);
    fixture_session_id = NULL;
    clear_session();
    if (geteuid() == 0) {
        check(needle, needle, "cf-fixture", false, NULL, NULL, true, false);
    } else {
        puts("Different-UID fixture skipped (requires root).");
    }
    assert(cf_detect_process_label(NULL, 0) == NULL);
    puts("Process detection tests passed.");
#endif
    return 0;
}
