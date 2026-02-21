#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PERF_WARMUP_RUNS 3
#define PERF_MEASURE_RUNS 20
#define PERF_DEFAULT_MAX_MEAN_MS 150.0
#define PERF_MAX_SAMPLES 256

static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static double timespec_diff_ms(const struct timespec *start, const struct timespec *end) {
    long sec = end->tv_sec - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;
    return (double)sec * 1000.0 + (double)nsec / 1000000.0;
}

static bool parse_env_positive_int(const char *name, int default_value, int max_value, int *out) {
    if (!out || default_value <= 0 || max_value <= 0 || max_value < default_value) return false;

    const char *raw = getenv(name);
    if (!raw || !raw[0]) {
        *out = default_value;
        return true;
    }

    char *endptr = NULL;
    errno = 0;
    long parsed = strtol(raw, &endptr, 10);
    if (errno != 0 || endptr == raw || *endptr != '\0' || parsed <= 0 || parsed > max_value) {
        return false;
    }

    *out = (int)parsed;
    return true;
}

static bool parse_env_positive_double(const char *name, double default_value, double *out) {
    if (!out || default_value <= 0.0) return false;

    const char *raw = getenv(name);
    if (!raw || !raw[0]) {
        *out = default_value;
        return true;
    }

    char *endptr = NULL;
    errno = 0;
    double parsed = strtod(raw, &endptr);
    if (errno != 0 || endptr == raw || *endptr != '\0' || parsed <= 0.0 || parsed > DBL_MAX) {
        return false;
    }

    *out = parsed;
    return true;
}

static bool ensure_dir(const char *path) {
    if (!path || !path[0]) return false;
    if (mkdir(path, 0700) == 0) return true;
    return errno == EEXIST;
}

static bool write_perf_config(const char *xdg_dir) {
    if (!xdg_dir || !xdg_dir[0]) return false;

    char cupid_dir[512];
    snprintf(cupid_dir, sizeof(cupid_dir), "%s/cupidfetch", xdg_dir);

    if (!ensure_dir(cupid_dir)) {
        fprintf(stderr, "Failed to create config dir '%s': %s\n", cupid_dir, strerror(errno));
        return false;
    }

    char conf_path[640];
    snprintf(conf_path, sizeof(conf_path), "%s/cupidfetch.conf", cupid_dir);

    FILE *fp = fopen(conf_path, "w");
    if (!fp) {
        fprintf(stderr, "Failed to write config file '%s': %s\n", conf_path, strerror(errno));
        return false;
    }

    fputs("modules = hostname username distro uptime shell\n", fp);
    fputs("memory.unit-str = MiB\n", fp);
    fputs("memory.unit-size = 1048576\n", fp);

    fclose(fp);
    return true;
}

static bool run_one_sample(const char *binary_path, double *elapsed_ms_out) {
    if (!binary_path || !elapsed_ms_out) return false;

    struct timespec t0;
    struct timespec t1;

    if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
        fprintf(stderr, "clock_gettime start failed: %s\n", strerror(errno));
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return false;
    }

    if (pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }

        execl(binary_path, binary_path, "--json", "--force-distro", "Ubuntu", (char *)NULL);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
        return false;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) {
        fprintf(stderr, "clock_gettime end failed: %s\n", strerror(errno));
        return false;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "cupidfetch exited with status %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return false;
    }

    *elapsed_ms_out = timespec_diff_ms(&t0, &t1);
    return true;
}

int main(void) {
    int warmup_runs = PERF_WARMUP_RUNS;
    int measure_runs = PERF_MEASURE_RUNS;
    double max_mean_ms = PERF_DEFAULT_MAX_MEAN_MS;

    if (!parse_env_positive_int("CUPIDFETCH_PERF_WARMUP", PERF_WARMUP_RUNS, PERF_MAX_SAMPLES, &warmup_runs)) {
        fprintf(stderr, "Invalid CUPIDFETCH_PERF_WARMUP; expected integer 1..%d\n", PERF_MAX_SAMPLES);
        return 1;
    }

    if (!parse_env_positive_int("CUPIDFETCH_PERF_RUNS", PERF_MEASURE_RUNS, PERF_MAX_SAMPLES, &measure_runs)) {
        fprintf(stderr, "Invalid CUPIDFETCH_PERF_RUNS; expected integer 1..%d\n", PERF_MAX_SAMPLES);
        return 1;
    }

    if (!parse_env_positive_double("CUPIDFETCH_PERF_MAX_MEAN_MS", PERF_DEFAULT_MAX_MEAN_MS, &max_mean_ms)) {
        fprintf(stderr, "Invalid CUPIDFETCH_PERF_MAX_MEAN_MS; expected positive number\n");
        return 1;
    }

    char tmp_template[] = "/tmp/cupidfetch-perf-XXXXXX";
    char *tmp_dir = mkdtemp(tmp_template);
    if (!tmp_dir) {
        fprintf(stderr, "mkdtemp failed: %s\n", strerror(errno));
        return 1;
    }

    if (!write_perf_config(tmp_dir)) {
        return 1;
    }

    if (setenv("XDG_CONFIG_HOME", tmp_dir, 1) != 0) {
        fprintf(stderr, "setenv XDG_CONFIG_HOME failed: %s\n", strerror(errno));
        return 1;
    }

    const char *binary = "./cupidfetch";
    if (access(binary, X_OK) != 0) {
        fprintf(stderr, "Binary '%s' is missing or not executable\n", binary);
        return 1;
    }

    for (int i = 0; i < warmup_runs; i++) {
        double ignored_ms = 0.0;
        if (!run_one_sample(binary, &ignored_ms)) {
            fprintf(stderr, "Warmup run %d failed\n", i + 1);
            return 1;
        }
    }

    double samples[PERF_MAX_SAMPLES];
    double total_ms = 0.0;
    double min_ms = 1e18;
    double max_ms = 0.0;

    for (int i = 0; i < measure_runs; i++) {
        double elapsed_ms = 0.0;
        if (!run_one_sample(binary, &elapsed_ms)) {
            fprintf(stderr, "Measured run %d failed\n", i + 1);
            return 1;
        }

        samples[i] = elapsed_ms;
        total_ms += elapsed_ms;
        if (elapsed_ms < min_ms) min_ms = elapsed_ms;
        if (elapsed_ms > max_ms) max_ms = elapsed_ms;
    }

    double sorted[PERF_MAX_SAMPLES];
    memcpy(sorted, samples, (size_t)measure_runs * sizeof(double));
    qsort(sorted, (size_t)measure_runs, sizeof(double), compare_double);

    int p50_index = (measure_runs - 1) / 2;
    int p95_index = ((measure_runs * 95) + 99) / 100 - 1;
    if (p95_index < 0) p95_index = 0;
    if (p95_index >= measure_runs) p95_index = measure_runs - 1;

    double mean_ms = total_ms / (double)measure_runs;
    double p50_ms = sorted[p50_index];
    double p95_ms = sorted[p95_index];

    printf("test_perf: runs=%d warmup=%d\n", measure_runs, warmup_runs);
    printf("test_perf: mean=%.2f ms p50=%.2f ms p95=%.2f ms min=%.2f ms max=%.2f ms\n",
           mean_ms, p50_ms, p95_ms, min_ms, max_ms);
    printf("test_perf: budget mean <= %.2f ms\n", max_mean_ms);

    if (mean_ms > max_mean_ms) {
        fprintf(stderr, "test_perf: FAILED (mean %.2f ms > budget %.2f ms)\n", mean_ms, max_mean_ms);
        return 1;
    }

    printf("test_perf: OK\n");
    return 0;
}
