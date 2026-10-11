#ifndef _WIN32
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include "subprocess.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

static int capture(FILE *out, const char *command, unsigned timeout_ms,
                   size_t output_limit) {
    HANDLE reader = NULL, writer = NULL, null_device = INVALID_HANDLE_VALUE;
    HANDLE job = NULL;
    PROCESS_INFORMATION process;
    STARTUPINFOEXA startup;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    SECURITY_ATTRIBUTES security = { sizeof(security), NULL, TRUE };
    char shell[MAX_PATH], *line = NULL;
    SIZE_T attributes_size = 0;
    int started = 0, ok = 0, eof = 0, exited = 0;
    size_t total = 0, command_length = strlen(command);
    ULONGLONG deadline = GetTickCount64() + timeout_ms;
    memset(&process, 0, sizeof(process));
    memset(&startup, 0, sizeof(startup));
    memset(&limits, 0, sizeof(limits));
    startup.StartupInfo.cb = sizeof(startup);
    UINT length = GetSystemDirectoryA(shell, sizeof(shell));
    if (!length || length >= sizeof(shell) - sizeof("\\cmd.exe")) goto done;
    memcpy(shell + length, "\\cmd.exe", sizeof("\\cmd.exe"));
    if (command_length > SIZE_MAX - strlen(shell) - 32) goto done;
    line = malloc(command_length + strlen(shell) + 32);
    if (!line) goto done;
    snprintf(line, command_length + strlen(shell) + 32,
             "\"%s\" /d /s /c \"%s\"", shell, command);
    if (!CreatePipe(&reader, &writer, &security, 0) ||
        !SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0)) goto done;
    null_device = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (null_device == INVALID_HANDLE_VALUE) goto done;
    job = CreateJobObjectA(NULL, NULL);
    if (!job) goto done;
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) goto done;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
    startup.lpAttributeList = malloc(attributes_size);
    if (!startup.lpAttributeList) goto done;
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
                                          &attributes_size)) {
        free(startup.lpAttributeList);
        startup.lpAttributeList = NULL;
        goto done;
    }
    HANDLE inherited[2] = { writer, null_device };
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited),
            NULL, NULL)) goto done;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = null_device;
    startup.StartupInfo.hStdOutput = writer;
    startup.StartupInfo.hStdError = null_device;
    if (!CreateProcessA(shell, line, NULL, NULL, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW |
                        EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                        &startup.StartupInfo, &process)) goto done;
    started = 1;
    if (!AssignProcessToJobObject(job, process.hProcess) ||
        ResumeThread(process.hThread) == (DWORD)-1) goto done;
    CloseHandle(writer);
    writer = NULL;
    while (!(eof && exited)) {
        char buffer[8192];
        DWORD available = 0, count = 0, status;
        if (GetTickCount64() >= deadline) goto done;
        if (!eof) {
            if (!PeekNamedPipe(reader, NULL, 0, NULL, &available, NULL)) {
                if (GetLastError() != ERROR_BROKEN_PIPE) goto done;
                eof = 1;
            } else if (available) {
                DWORD wanted = available < sizeof(buffer) ? available : sizeof(buffer);
                if (!ReadFile(reader, buffer, wanted, &count, NULL) || !count)
                    goto done;
                if ((size_t)count > output_limit - total ||
                    fwrite(buffer, 1, count, out) != count) goto done;
                total += count;
            }
        }
        if (!exited) {
            DWORD wait = WaitForSingleObject(process.hProcess, 0);
            if (wait == WAIT_FAILED) goto done;
            if (wait == WAIT_OBJECT_0) {
                if (!GetExitCodeProcess(process.hProcess, &status) || status != 0)
                    goto done;
                exited = 1;
            }
        }
        if (!(eof && exited) && !available) {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline) goto done;
            DWORD pause = deadline - now < 10 ? (DWORD)(deadline - now) : 10;
            Sleep(pause);
        }
    }
    ok = 1;
done:
    if (started) {
        /* Also covers assignment failure: the suspended child must not live. */
        if (job) TerminateJobObject(job, 1);
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (job) CloseHandle(job);
    if (reader) CloseHandle(reader);
    if (writer) CloseHandle(writer);
    if (null_device != INVALID_HANDLE_VALUE) CloseHandle(null_device);
    if (startup.lpAttributeList) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        free(startup.lpAttributeList);
    }
    free(line);
    return ok;
}
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int monotonic_ms(uint64_t *value) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    *value = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    return 1;
}

static int capture(FILE *out, const char *command, unsigned timeout_ms,
                   size_t output_limit) {
    int pipes[2] = { -1, -1 }, null_device = -1;
    int ok = 0, status = 0, exited = 0, eof = 0;
    size_t total = 0;
    uint64_t now, deadline;
    pid_t child = -1;
    if (!monotonic_ms(&now)) return 0;
    deadline = now + timeout_ms;
    if (pipe(pipes) != 0) goto done;
    if (fcntl(pipes[0], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(pipes[1], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(pipes[0], F_SETFL, O_NONBLOCK) < 0) goto done;
    null_device = open("/dev/null", O_RDWR);
    if (null_device < 0) goto done;
    child = fork();
    if (child < 0) goto done;
    if (child == 0) {
        if (setpgid(0, 0) != 0 ||
            dup2(null_device, STDIN_FILENO) < 0 ||
            dup2(pipes[1], STDOUT_FILENO) < 0 ||
            dup2(null_device, STDERR_FILENO) < 0 ||
            fcntl(STDOUT_FILENO, F_SETFD, 0) < 0) _exit(127);
        if (pipes[0] > STDERR_FILENO) close(pipes[0]);
        if (pipes[1] > STDERR_FILENO) close(pipes[1]);
        if (null_device > STDERR_FILENO) close(null_device);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    /* Either side can win the race to establish ownership before exec. */
    if (setpgid(child, child) != 0 && errno != EACCES && errno != ESRCH)
        goto done;
    close(pipes[1]);
    pipes[1] = -1;
    close(null_device);
    null_device = -1;
    while (!(eof && exited)) {
        if (!monotonic_ms(&now) || now >= deadline) goto done;
        if (!eof) {
            char buffer[8192];
            ssize_t count = read(pipes[0], buffer, sizeof(buffer));
            if (count > 0) {
                if ((size_t)count > output_limit - total ||
                    fwrite(buffer, 1, (size_t)count, out) != (size_t)count)
                    goto done;
                total += (size_t)count;
            } else if (count == 0) eof = 1;
            else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                goto done;
            /* Keep draining without waiting, but check status and deadline. */
            if (count > 0) continue;
        }
        if (!exited) {
            siginfo_t info;
            memset(&info, 0, sizeof(info));
            /* Retain the child until group cleanup so its PID cannot be reused. */
            if (waitid(P_PID, (id_t)child, &info, WEXITED | WNOHANG | WNOWAIT) < 0) {
                if (errno == EINTR) continue;
                goto done;
            }
            if (info.si_pid == child) {
                exited = 1;
                if (info.si_code != CLD_EXITED || info.si_status != 0) goto done;
            }
        }
        if (eof && exited) break;
        if (!monotonic_ms(&now) || now >= deadline) goto done;
        int delay = deadline - now < 10 ? (int)(deadline - now) : 10;
        struct pollfd fd = { pipes[0], POLLIN, 0 };
        if (poll(eof ? NULL : &fd, eof ? 0 : 1, delay) < 0 && errno != EINTR)
            goto done;
    }
    ok = 1;
done:
    if (child > 0) {
        /* Kill the entire owned group, including a writer whose parent exited. */
        kill(-child, SIGKILL);
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }
    if (pipes[0] >= 0) close(pipes[0]);
    if (pipes[1] >= 0) close(pipes[1]);
    if (null_device >= 0) close(null_device);
    return ok;
}
#endif

FILE *cf_capture_command(const char *command, unsigned timeout_ms,
                         size_t output_limit) {
    if (!command || !*command || !timeout_ms) return NULL;
    FILE *out = tmpfile();
    if (!out) return NULL;
#ifndef _WIN32
    if (fcntl(fileno(out), F_SETFD, FD_CLOEXEC) < 0) {
        fclose(out);
        return NULL;
    }
#endif
    if (!capture(out, command, timeout_ms, output_limit) ||
        fflush(out) != 0 || fseek(out, 0, SEEK_SET) != 0) {
        fclose(out);
        return NULL;
    }
    return out;
}
