#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include "../src/modules/common/subprocess.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <time.h>
#include <unistd.h>
#endif

static uint64_t milliseconds(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}

static void hang(void) {
#ifdef _WIN32
    Sleep(INFINITE);
#else
    signal(SIGTERM, SIG_IGN);
    for (;;) pause();
#endif
}

static int child_mode(const char *self, const char *mode,
                      const char *identity, const char *owner) {
#ifndef _WIN32
    (void)owner;
#endif
#ifdef _WIN32
    assert(_setmode(_fileno(stdout), _O_BINARY) != -1);
#endif
    if (!strcmp(mode, "success")) {
        assert(fwrite("alpha", 1, 5, stdout) == 5);
        return 0;
    }
    if (!strcmp(mode, "empty")) return 0;
    if (!strcmp(mode, "stdin")) {
        if (getchar() != EOF || ferror(stdin)) return 2;
        fputs("eof", stdout);
        return 0;
    }
    if (!strcmp(mode, "failure")) {
        fputs("partial", stdout);
        fflush(stdout);
        return 7;
    }
    if (!strcmp(mode, "binary")) {
        unsigned char bytes[256];
        for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (unsigned char)i;
        for (int i = 0; i < 128; ++i)
            if (fwrite(bytes, 1, sizeof(bytes), stdout) != sizeof(bytes)) return 2;
        return 0;
    }
    if (!strcmp(mode, "flood")) {
        char bytes[8192];
        memset(bytes, 'x', sizeof(bytes));
        for (;;) if (fwrite(bytes, 1, sizeof(bytes), stdout) != sizeof(bytes)) return 2;
    }
    if (!strcmp(mode, "closed")) {
        fclose(stdout);
        hang();
        return 0;
    }
    if (!strcmp(mode, "partial-hang")) {
        fputs("partial", stdout);
        fflush(stdout);
        hang();
        return 0;
    }
    if (!strcmp(mode, "descendant")) {
#ifdef _WIN32
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        char command[8192];
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        int length = snprintf(command, sizeof(command), "\"%s\" --child hang", self);
        if (length < 0 || (size_t)length >= sizeof(command)) return 2;
        if (!CreateProcessA(self, command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                            NULL, NULL, &startup, &process)) return 2;
        if (identity) {
            HANDLE target = OpenProcess(PROCESS_DUP_HANDLE, FALSE,
                                        (DWORD)strtoul(owner, NULL, 10));
            HANDLE copied = NULL;
            if (!target || !DuplicateHandle(GetCurrentProcess(), process.hProcess,
                    target, &copied, SYNCHRONIZE, FALSE, 0)) return 2;
            HANDLE record = NULL;
            HANDLE source = (HANDLE)(uintptr_t)strtoull(identity, NULL, 10);
            if (!DuplicateHandle(target, source, GetCurrentProcess(), &record,
                                 0, FALSE, DUPLICATE_SAME_ACCESS)) return 2;
            CloseHandle(target);
            DWORD written = 0;
            uintptr_t value = (uintptr_t)copied;
            if (!WriteFile(record, &value, sizeof(value), &written, NULL) ||
                written != sizeof(value)) return 2;
            CloseHandle(record);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
#else
        (void)self;
        pid_t child = fork();
        if (child < 0) return 2;
        if (!child) { hang(); _exit(0); }
        if (identity) {
            int record = (int)strtol(identity, NULL, 10);
            if (write(record, &child, sizeof(child)) != sizeof(child)) return 2;
        }
#endif
        return 0;
    }
    if (!strcmp(mode, "hang")) { hang(); return 0; }
    return 2;
}

static void command_for(char *out, size_t capacity, const char *self,
                        const char *mode) {
#ifdef _WIN32
    int length = snprintf(out, capacity, "\"%s\" --child %s", self, mode);
    assert(length > 0 && (size_t)length < capacity);
#else
    size_t used = 0;
    assert(capacity > 2);
    out[used++] = '\'';
    for (const char *p = self; *p; ++p) {
        const char *piece = *p == '\'' ? "'\\''" : p;
        size_t count = *p == '\'' ? 4 : 1;
        assert(used + count + 1 < capacity);
        memcpy(out + used, piece, count);
        used += count;
    }
    int length = snprintf(out + used, capacity - used, "' --child %s", mode);
    assert(length > 0 && (size_t)length < capacity - used);
#endif
}

static void expect_failure(const char *command, unsigned timeout, size_t limit) {
    uint64_t before = milliseconds();
    FILE *out = cf_capture_command(command, timeout, limit);
    assert(out == NULL);
    assert(milliseconds() - before < 3000);
}


#if defined(_WIN32) || defined(__linux__)
static void expect_descendant_cleanup(const char *self) {
    char command[8192];
    command_for(command, sizeof(command), self, "descendant");
    size_t used = strlen(command);
#ifdef _WIN32
    HANDLE reader, writer;
    /* This side channel is duplicated explicitly, not inherited by capture. */
    assert(CreatePipe(&reader, &writer, NULL, 0));
    int length = snprintf(command + used, sizeof(command) - used, " %llu %lu",
                          (unsigned long long)(uintptr_t)writer,
                          (unsigned long)GetCurrentProcessId());
#else
    int previous, records[2];
    assert(prctl(PR_GET_CHILD_SUBREAPER, &previous) == 0);
    assert(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0);
    assert(pipe(records) == 0);
    assert(fcntl(records[0], F_SETFL, O_NONBLOCK) == 0);
    int length = snprintf(command + used, sizeof(command) - used, " %d 0", records[1]);
#endif
    assert(length > 0 && (size_t)length < sizeof(command) - used);
    expect_failure(command, 1000, 1024);
#ifdef _WIN32
    uintptr_t value = 0;
    DWORD count = 0, available = 0;
    assert(PeekNamedPipe(reader, NULL, 0, NULL, &available, NULL));
    assert(available == sizeof(value));
    assert(ReadFile(reader, &value, sizeof(value), &count, NULL));
    assert(count == sizeof(value));
    assert(CloseHandle(writer));
    HANDLE descendant = (HANDLE)value;
    /* A retained handle identifies this exact process even if its PID is reused. */
    assert(WaitForSingleObject(descendant, 2000) == WAIT_OBJECT_0);
    assert(CloseHandle(descendant));
    assert(CloseHandle(reader));
#else
    assert(close(records[1]) == 0);
    pid_t descendant = -1;
    assert(read(records[0], &descendant, sizeof(descendant)) == sizeof(descendant));
    assert(descendant > 0);
    assert(close(records[0]) == 0);
    uint64_t deadline = milliseconds() + 2000;
    int status = 0;
    pid_t result;
    for (;;) {
        result = waitpid(descendant, &status, WNOHANG);
        if (result == descendant) break;
        assert(result == 0 || (result < 0 && errno == EINTR));
        assert(milliseconds() < deadline);
        struct timespec pause_time = { 0, 1000000 };
        nanosleep(&pause_time, NULL);
    }
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    assert(prctl(PR_SET_CHILD_SUBREAPER, previous) == 0);
#endif
}
#endif

int main(int argc, char **argv) {
    char self[4096], command[8192], bytes[6];
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, self, sizeof(self));
    assert(length && length < sizeof(self));
#else
    assert(strlen(argv[0]) < sizeof(self));
    strcpy(self, argv[0]);
#endif
    if ((argc == 3 || argc == 5) && !strcmp(argv[1], "--child"))
        return child_mode(self, argv[2], argc == 5 ? argv[3] : NULL,
                          argc == 5 ? argv[4] : NULL);
    assert(cf_capture_command(NULL, 100, 10) == NULL);
    assert(cf_capture_command("", 100, 10) == NULL);
    assert(cf_capture_command("unused", 0, 10) == NULL);
    command_for(command, sizeof(command), self, "success");
    FILE *out = cf_capture_command(command, 5000, 5);
    assert(out);
    assert(fread(bytes, 1, sizeof(bytes), out) == 5);
    assert(!memcmp(bytes, "alpha", 5) && !ferror(out));
    assert(fclose(out) == 0);
    expect_failure(command, 1000, 4);
    command_for(command, sizeof(command), self, "empty");
    out = cf_capture_command(command, 5000, 0);
    assert(out && fgetc(out) == EOF && !ferror(out));
    assert(fclose(out) == 0);
    command_for(command, sizeof(command), self, "stdin");
    out = cf_capture_command(command, 5000, 3);
    assert(out && fread(bytes, 1, sizeof(bytes), out) == 3);
    assert(!memcmp(bytes, "eof", 3));
    assert(fclose(out) == 0);
    command_for(command, sizeof(command), self, "binary");
    out = cf_capture_command(command, 5000, 32768);
    assert(out);
    for (int i = 0; i < 32768; ++i) assert(fgetc(out) == i % 256);
    assert(fgetc(out) == EOF && !ferror(out));
    assert(fclose(out) == 0);
    const char *failures[] = { "failure", "hang", "partial-hang", "closed", "flood" };
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        command_for(command, sizeof(command), self, failures[i]);
        expect_failure(command, 200, 32768);
    }
#if defined(_WIN32) || defined(__linux__)
    expect_descendant_cleanup(self);
#else
    command_for(command, sizeof(command), self, "descendant");
    expect_failure(command, 200, 1024);
#endif
    expect_failure("cupidfetch_fixture_command_that_does_not_exist_31", 1000, 1024);
    /* Repetition catches leaked descriptors/handles and unreaped direct children. */
    for (int i = 0; i < 32; ++i) {
        command_for(command, sizeof(command), self, "success");
        out = cf_capture_command(command, 5000, 5);
        assert(out && fclose(out) == 0);
    }
    puts("subprocess tests passed");
    return 0;
}
