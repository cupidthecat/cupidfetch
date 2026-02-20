#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/cupidfetch.h"

static int write_temp_config(char *path_out, size_t path_out_size, const char *content) {
    char tmpl[] = "/tmp/cupidfetch-test-config-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }

    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        perror("fdopen");
        close(fd);
        unlink(tmpl);
        return 1;
    }

    if (fputs(content, fp) == EOF) {
        perror("fputs");
        fclose(fp);
        unlink(tmpl);
        return 1;
    }

    if (fclose(fp) != 0) {
        perror("fclose");
        unlink(tmpl);
        return 1;
    }

    strncpy(path_out, tmpl, path_out_size - 1);
    path_out[path_out_size - 1] = '\0';
    return 0;
}

int main(void) {
    const char *cfg_text =
        "modules = hostname memory cpu\n"
        "memory.unit-str = KiB\n"
        "memory.unit-size = 1024\n"
        "storage.unit-str = MiB\n"
        "storage.unit-size = 1048576\n"
        "network.show-full-public-ip = true\n";

    char cfg_path[256];
    if (write_temp_config(cfg_path, sizeof(cfg_path), cfg_text) != 0) return 1;

    struct CupidConfig cfg;
    init_g_config();
    cfg = g_userConfig;

    load_config_file(cfg_path, &cfg);

    if (strcmp(cfg.memory_unit, "KiB") != 0 || cfg.memory_unit_size != 1024UL) {
        fprintf(stderr, "memory config parse failed\n");
        unlink(cfg_path);
        return 1;
    }

    if (strcmp(cfg.storage_unit, "MiB") != 0 || cfg.storage_unit_size != 1048576UL) {
        fprintf(stderr, "storage config parse failed\n");
        unlink(cfg_path);
        return 1;
    }

    if (!cfg.network_show_full_public_ip) {
        fprintf(stderr, "boolean config parse failed\n");
        unlink(cfg_path);
        return 1;
    }

    if (cfg.modules[0] != get_hostname || cfg.modules[1] != get_available_memory || cfg.modules[2] != get_cpu || cfg.modules[3] != NULL) {
        fprintf(stderr, "modules list parse failed\n");
        unlink(cfg_path);
        return 1;
    }

    unlink(cfg_path);
    printf("test_config: OK\n");
    return 0;
}
