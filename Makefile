CUPID_LIBS=-Ilibs
CUPID_DEV=-Wall -pedantic --std=c99 -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE
CUPID_OPT?=-O2 -DNDEBUG
CUPID_FORMAT=-Wformat -Werror=format-security
SRC_FILES=$(shell find src -type f -name '*.c')
HEADER_FILES=$(shell find src libs -type f -name '*.h')
TEST_BIN_DIR=tests/bin
TEST_PARSERS_BIN=$(TEST_BIN_DIR)/test_parsers
TEST_CONFIG_BIN=$(TEST_BIN_DIR)/test_config
TEST_UNITS_BIN=$(TEST_BIN_DIR)/test_units
TEST_PERF_BIN=$(TEST_BIN_DIR)/test_perf
TEST_SUBPROCESS_BIN=$(TEST_BIN_DIR)/test_subprocess
TEST_DISTRO_FILE_BIN=$(TEST_BIN_DIR)/test_distro_file
TEST_PATH_BIN=$(TEST_BIN_DIR)/test_posix_path
TEST_PACKAGES_BIN=$(TEST_BIN_DIR)/test_packages
TEST_PROCESS_BIN=$(TEST_BIN_DIR)/test_process_detection
TEST_NETWORK_BIN=$(TEST_BIN_DIR)/test_network
TEST_BATTERY_BIN=$(TEST_BIN_DIR)/test_battery
TEST_BATTERY_SYSFS_BIN=$(TEST_BIN_DIR)/test_battery_sysfs
TEST_WINDOWS_HARDWARE_BIN=$(TEST_BIN_DIR)/test_windows_hardware

BIN_NAME=cupidfetch
ifeq ($(OS),Windows_NT)
BIN_NAME=cupidfetch.exe
endif

all: $(BIN_NAME)

$(BIN_NAME): $(SRC_FILES) libs/cupidconf.c $(HEADER_FILES)
	$(CC) -o $(BIN_NAME) $(filter %.c,$^) $(CUPID_OPT) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

dev: $(SRC_FILES) libs/cupidconf.c $(HEADER_FILES)
	$(CC) -o $(BIN_NAME) $(filter %.c,$^) $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

asan: $(SRC_FILES) libs/cupidconf.c $(HEADER_FILES)
	$(CC) -o $(BIN_NAME) $(filter %.c,$^) -fsanitize=address $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

ubsan: $(SRC_FILES) libs/cupidconf.c $(HEADER_FILES)
	$(CC) -o $(BIN_NAME) $(filter %.c,$^) -fsanitize=undefined $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

windows:
	$(MAKE) BIN_NAME=cupidfetch.exe CFLAGS="$(CFLAGS) -D_WIN32_WINNT=0x0601" LIBS="$(LIBS) -lws2_32 -liphlpapi"

$(TEST_BIN_DIR):
	mkdir -p $(TEST_BIN_DIR)

$(TEST_PARSERS_BIN): tests/test_parsers.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_parsers.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_CONFIG_BIN): tests/test_config.c tests/test_stubs.c src/config.c libs/cupidconf.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_config.c tests/test_stubs.c src/config.c libs/cupidconf.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_UNITS_BIN): tests/test_units.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_units.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_PERF_BIN): tests/test_perf.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_perf.c $(CUPID_DEV) $(CUPID_OPT) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_PACKAGES_BIN): tests/test_packages.c src/modules/common/package_helpers.c src/modules/common/subprocess.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_packages.c src/modules/common/package_helpers.c src/modules/common/subprocess.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_PROCESS_BIN): tests/test_process_detection.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_process_detection.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test-process: $(TEST_PROCESS_BIN)
	./$(TEST_PROCESS_BIN)
$(TEST_NETWORK_BIN): tests/test_network.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_network.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test-network: $(TEST_NETWORK_BIN)
	./$(TEST_NETWORK_BIN)

test-packages: $(TEST_PACKAGES_BIN)
	./$(TEST_PACKAGES_BIN)

$(TEST_PATH_BIN): tests/test_posix_path.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_posix_path.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test-path: $(TEST_PATH_BIN)
	./$(TEST_PATH_BIN)

$(TEST_DISTRO_FILE_BIN): tests/test_distro_file.c src/modules/common/distro_file.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_distro_file.c src/modules/common/distro_file.c -DCF_DISTRO_FILE_TESTING $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test-distro-file: $(TEST_DISTRO_FILE_BIN)
	./$(TEST_DISTRO_FILE_BIN)

$(TEST_SUBPROCESS_BIN): tests/test_subprocess.c src/modules/common/subprocess.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_subprocess.c src/modules/common/subprocess.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test-subprocess: $(TEST_SUBPROCESS_BIN)
	./$(TEST_SUBPROCESS_BIN)

TEST_WINDOWS_PACKAGES_BIN = $(TEST_BIN_DIR)/test_windows_packages

$(TEST_WINDOWS_PACKAGES_BIN): tests/test_windows_packages.c src/modules/common/windows_packages.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_windows_packages.c src/modules/common/windows_packages.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test-windows-packages: $(TEST_WINDOWS_PACKAGES_BIN)
	./$(TEST_WINDOWS_PACKAGES_BIN)

test-parsers: $(TEST_PARSERS_BIN)
	./$(TEST_PARSERS_BIN)

test-config: $(TEST_CONFIG_BIN)
	./$(TEST_CONFIG_BIN)

test-units: $(TEST_UNITS_BIN)
	./$(TEST_UNITS_BIN)

test-perf: $(BIN_NAME) $(TEST_PERF_BIN)
	./$(TEST_PERF_BIN)

test-cli: $(BIN_NAME) $(TEST_PERF_BIN)
	python3 tests/test_cli.py

test-install:
	sh tests/test_install.sh

test: test-windows-memory $(BIN_NAME) test-process test-hardware test-network test-windows-packages test-subprocess test-distro-file test-path test-packages test-parsers test-config test-units test-cli test-install

.PHONY: test-process test-network all dev asan ubsan windows clean test test-parsers test-config test-units test-perf test-cli test-install test-packages test-path test-distro-file test-subprocess test-windows-packages

clean:
	rm -f $(TEST_WINDOWS_MEMORY_BIN) cupidfetch cupidfetch.exe *.o $(TEST_PARSERS_BIN) $(TEST_CONFIG_BIN) $(TEST_UNITS_BIN) $(TEST_PERF_BIN) $(TEST_PACKAGES_BIN) $(TEST_PATH_BIN) $(TEST_DISTRO_FILE_BIN) $(TEST_SUBPROCESS_BIN) $(TEST_WINDOWS_PACKAGES_BIN) $(TEST_PROCESS_BIN) $(TEST_NETWORK_BIN) $(TEST_BATTERY_BIN) $(TEST_BATTERY_SYSFS_BIN) $(TEST_WINDOWS_HARDWARE_BIN)



$(TEST_BATTERY_BIN): tests/test_battery.c src/modules/common/battery_helpers.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_battery.c src/modules/common/battery_helpers.c $(CUPID_DEV) $(CUPID_OPT) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_BATTERY_SYSFS_BIN): tests/test_battery_sysfs.c src/modules/hardware/battery.c src/modules/common/battery_helpers.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_battery_sysfs.c src/modules/common/battery_helpers.c $(CUPID_DEV) $(CUPID_OPT) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_WINDOWS_HARDWARE_BIN): tests/test_windows_hardware.c src/modules/hardware/cpu.c src/modules/hardware/battery.c src/modules/common/battery_helpers.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_windows_hardware.c src/modules/common/battery_helpers.c $(CUPID_DEV) $(CUPID_OPT) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test-hardware: $(TEST_BATTERY_BIN) $(TEST_BATTERY_SYSFS_BIN) $(TEST_WINDOWS_HARDWARE_BIN)
	./$(TEST_BATTERY_BIN)
	./$(TEST_BATTERY_SYSFS_BIN)
	./$(TEST_WINDOWS_HARDWARE_BIN)

.PHONY: test-hardware

TEST_WINDOWS_MEMORY_BIN=$(TEST_BIN_DIR)/test_windows_memory
$(TEST_WINDOWS_MEMORY_BIN): tests/test_windows_memory.c src/modules/hardware/memory.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(HEADER_FILES) | $(TEST_BIN_DIR)
	$(CC) -o $@ tests/test_windows_memory.c src/modules/common/module_helpers.c src/modules/common/subprocess.c $(CUPID_DEV) $(CUPID_FORMAT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test-windows-memory: $(TEST_WINDOWS_MEMORY_BIN)
	./$(TEST_WINDOWS_MEMORY_BIN)

.PHONY: test-windows-memory
