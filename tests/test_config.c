#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/cupidfetch.h"
#include "../libs/cupidconf.h"

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

static int load_text(const char *text, struct CupidConfig *cfg) {
    char path[256];
    if (write_temp_config(path, sizeof(path), text)) return 1;
    load_config_file(path, cfg);
    unlink(path);
    return 0;
}

static int test_shipped_configs(void) {
    init_g_config();
    struct CupidConfig cfg = g_userConfig;
    /* Start with different values so the shipped defaults must be parsed. */
    cfg.memory_unit_size = cfg.storage_unit_size = 1;
    cfg.network_show_full_public_ip = true;
    load_config_file("data/cupidfetch.ini", &cfg);
    if (strcmp(cfg.memory_unit, "MB") || cfg.memory_unit_size != 1000000UL ||
        strcmp(cfg.storage_unit, "MB") || cfg.storage_unit_size != 1000000UL ||
        cfg.network_show_full_public_ip) {
        fprintf(stderr, "shipped default INI parse failed\n");
        return 1;
    }
    load_config_file("examples/minimal.ini", &cfg);
    if (strcmp(cfg.memory_unit, "MiB") || cfg.memory_unit_size != 1048576UL ||
        cfg.modules[0] != get_distro || cfg.modules[1] != get_package_count ||
        cfg.modules[2] != get_shell || cfg.modules[3] != get_available_memory ||
        cfg.modules[4] != NULL) {
        fprintf(stderr, "shipped minimal INI parse failed\n");
        return 1;
    }
    return 0;
}

static int test_mixed_sections(void) {
    init_g_config();
    struct CupidConfig cfg = g_userConfig;
    if (load_text(" [ memory ] ; units\r\nunit-size = 1024\r\n"
                  "storage.unit-size = 2048\n"
                  "[storage]\nunit-size = 4096\n"
                  "memory.unit-size = 512\n"
                  "[network] # privacy\nshow-full-public-ip = yes\n"
                  "[]\nmodules = cpu", &cfg)) return 1;
    if (cfg.memory_unit_size != 512 || cfg.storage_unit_size != 4096 ||
        !cfg.network_show_full_public_ip || cfg.modules[0] != get_cpu ||
        cfg.modules[1] != NULL) {
        fprintf(stderr, "mixed sections/dotted keys parse failed\n");
        return 1;
    }
    return 0;
}

static int test_invalid_sizes(void) {
    const char *invalid[] = { "", "0", "-1", "+1", "garbage", "1024bytes",
                              "1.5", "1 2", "999999999999999999999999999999999" };
    init_g_config();
    struct CupidConfig cfg = g_userConfig;
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char text[256];
        snprintf(text, sizeof(text), "memory.unit-size = %s\nstorage.unit-size = %s\n",
                 invalid[i], invalid[i]);
        if (load_text(text, &cfg)) return 1;
        if (cfg.memory_unit_size != 1000000UL || cfg.storage_unit_size != 1000000000UL) {
            fprintf(stderr, "invalid unit size accepted: %s\n", invalid[i]);
            return 1;
        }
    }
    char text[256];
    snprintf(text, sizeof(text), "memory.unit-size = %lu\nstorage.unit-size = 1\n", ULONG_MAX);
    if (load_text(text, &cfg)) return 1;
    if (cfg.memory_unit_size != ULONG_MAX || cfg.storage_unit_size != 1) {
        fprintf(stderr, "valid unit size bounds rejected\n");
        return 1;
    }
    return 0;
}

static int test_embedded_nul(void) {
    char path[] = "/tmp/cupidfetch-test-nul-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 1;
    FILE *fp = fdopen(fd, "wb");
    if (!fp) { close(fd); unlink(path); return 1; }
    const char bytes[] = "# comment\0network.show-full-public-ip = true\n"
                         "storage.unit-size = 2048\n";
    int failed = fwrite(bytes, 1, sizeof(bytes) - 1, fp) != sizeof(bytes) - 1;
    if (fclose(fp) != 0) failed = 1;
    init_g_config();
    struct CupidConfig cfg = g_userConfig;
    if (!failed) load_config_file(path, &cfg);
    unlink(path);
    if (failed || cfg.network_show_full_public_ip || cfg.storage_unit_size != 2048) {
        fprintf(stderr, "embedded NUL line handling failed\n");
        return 1;
    }
    return 0;
}

static int test_long_lines(void) {
    /* Align a setting with the old fgets chunk boundary, inside one line. */
    const char *prefixes[] = { "#", ";", "memory.unit-size = 2", "modules = cpu " };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        char text[4096];
        memset(text, ' ', 1023);
        memcpy(text, prefixes[i], strlen(prefixes[i]));
        strcpy(text + 1023, "network.show-full-public-ip = true\n"
                            "storage.unit-size = 2048\n");
        init_g_config();
        struct CupidConfig cfg = g_userConfig;
        if (load_text(text, &cfg)) return 1;
        if (cfg.network_show_full_public_ip || cfg.memory_unit_size != 1000000UL ||
            cfg.modules[0] != get_hostname || cfg.storage_unit_size != 2048) {
            fprintf(stderr, "overlong logical line was partially applied\n");
            return 1;
        }
        /* Also reject an overlong last line without a newline. */
        memset(text, 'x', 3072);
        memcpy(text, "memory.unit-size = ", strlen("memory.unit-size = "));
        text[3072] = '\0';
        if (load_text(text, &cfg) || cfg.memory_unit_size != 1000000UL) return 1;
    }

    /* A complete line of exactly 1023 bytes is valid, with or without LF. */
    for (int newline = 0; newline <= 1; newline++) {
        char text[1025];
        memcpy(text, "key=", 4);
        memset(text + 4, 'v', 1019);
        text[1023] = newline ? '\n' : '\0';
        text[1024] = '\0';
        char path[256];
        if (write_temp_config(path, sizeof(path), text)) return 1;
        cupidconf_t *conf = cupidconf_load(path);
        unlink(path);
        const char *value = cupidconf_get(conf, "key");
        int failed = !value || strlen(value) != 1019;
        cupidconf_free(conf);
        if (failed) {
            fprintf(stderr, "complete maximum-length line rejected\n");
            return 1;
        }
    }
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
    if (test_shipped_configs() || test_mixed_sections() || test_invalid_sizes() ||
        test_long_lines() || test_embedded_nul()) return 1;
    printf("test_config: OK\n");
    return 0;
}
