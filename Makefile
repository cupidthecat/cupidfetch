CUPID_LIBS=-Ilibs
CUPID_DEV=-Wall -pedantic --std=c99 -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE
CUPID_OPT?=-O2 -DNDEBUG
SRC_FILES=$(shell find src -type f -name '*.c')
TEST_BIN_DIR=tests/bin
TEST_PARSERS_BIN=$(TEST_BIN_DIR)/test_parsers
TEST_CONFIG_BIN=$(TEST_BIN_DIR)/test_config
TEST_UNITS_BIN=$(TEST_BIN_DIR)/test_units
TEST_PERF_BIN=$(TEST_BIN_DIR)/test_perf

all: clean cupidfetch

cupidfetch: $(SRC_FILES) libs/cupidconf.c
	$(CC) -o cupidfetch $^ $(CUPID_OPT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

dev: $(SRC_FILES) libs/cupidconf.c
	$(CC) -o cupidfetch $^ $(CUPID_DEV) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

asan: $(SRC_FILES) libs/cupidconf.c
	$(CC) -o cupidfetch $^ -fsanitize=address $(CUPID_DEV) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

ubsan: $(SRC_FILES) libs/cupidconf.c
	$(CC) -o cupidfetch $^ -fsanitize=undefined $(CUPID_DEV) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_BIN_DIR):
	mkdir -p $(TEST_BIN_DIR)

$(TEST_PARSERS_BIN): $(TEST_BIN_DIR) tests/test_parsers.c src/modules/common/module_helpers.c
	$(CC) -o $@ tests/test_parsers.c src/modules/common/module_helpers.c $(CUPID_DEV) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_CONFIG_BIN): $(TEST_BIN_DIR) tests/test_config.c tests/test_stubs.c src/config.c libs/cupidconf.c
	$(CC) -o $@ tests/test_config.c tests/test_stubs.c src/config.c libs/cupidconf.c $(CUPID_DEV) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_UNITS_BIN): $(TEST_BIN_DIR) tests/test_units.c src/modules/common/module_helpers.c
	$(CC) -o $@ tests/test_units.c src/modules/common/module_helpers.c $(CUPID_DEV) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

$(TEST_PERF_BIN): $(TEST_BIN_DIR) tests/test_perf.c
	$(CC) -o $@ tests/test_perf.c $(CUPID_DEV) $(CUPID_OPT) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

test-parsers: $(TEST_PARSERS_BIN)
	./$(TEST_PARSERS_BIN)

test-config: $(TEST_CONFIG_BIN)
	./$(TEST_CONFIG_BIN)

test-units: $(TEST_UNITS_BIN)
	./$(TEST_UNITS_BIN)

test-perf: cupidfetch $(TEST_PERF_BIN)
	./$(TEST_PERF_BIN)

test: test-parsers test-config test-units

.PHONY: clean test test-parsers test-config test-units test-perf

clean:
	rm -f cupidfetch *.o $(TEST_PARSERS_BIN) $(TEST_CONFIG_BIN) $(TEST_UNITS_BIN) $(TEST_PERF_BIN)



