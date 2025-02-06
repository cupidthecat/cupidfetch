CUPID_LIBS=-Ilibs
CUPID_DEV=-Wall -pedantic --std=c99 -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE

all: clean cupidfetch

cupidfetch: src/*.c libs/cupidconf.c
	$(CC) -o cupidfetch $^ $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

dev: src/*.c libs/cupidconf.c
	$(CC) -o cupidfetch $^ $(CUPID_DEV) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

asan: src/*.c libs/cupidconf.c
	$(CC) -o cupidfetch $^ -fsanitize=address $(CUPID_DEV) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

ubsan: src/*.c libs/cupidconf.c
	$(CC) -o cupidfetch $^ -fsanitize=undefined $(CUPID_DEV) $(CFLAGS) $(LDFLAGS) $(CUPID_LIBS) $(LIBS) $(LD_LIBS)

.PHONY: clean

clean:
	rm -f cupidfetch *.o



