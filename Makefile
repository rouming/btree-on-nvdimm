# Makefile btree-on-nvdimm tool

CC = $(CROSS_COMPILE)gcc
DEFINES = -I3rdparty/pmdk/src/include -I3rdparty/pmdk/src/examples

CFLAGS = -O2 -g -Wall -luuid -lm -lrt -Wl,-rpath 3rdparty/pmdk/src/nondebug -L 3rdparty/pmdk/src/nondebug -lpmemobj -lpmem

all: btree-on-nvdimm pmem
btree-on-nvdimm: *.c 3rdparty/pmdk/src/examples/libpmemobj/tree_map/btree_map.c
	$(CC) $(DEFINES) $(CFLAGS) -o $@ $^

pmem: 3rdparty/pmdk/src/nondebug/libpmemobj.a
3rdparty/pmdk/src/nondebug/libpmemobj.a:
	$(MAKE) -j8 -C 3rdparty/pmdk/src/libpmemobj

.PHONY: clean

clean:
	$(RM) btree-on-nvdimm *~
