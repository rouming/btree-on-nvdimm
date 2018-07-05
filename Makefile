# Makefile btree-on-nvdimm tool

CC = $(CROSS_COMPILE)gcc
DEFINES = -I3rdparty/pmdk/src/include -I3rdparty/pmdk/src/common \
          -I3rdparty/pmdk/src/ -I3rdparty/pmdk/src/examples

LIBDIR = 3rdparty/pmdk/src/nondebug
CFLAGS = -O2 -g -Wall
LFLAGS = $(LIBDIR)/libpmemobj.a $(LIBDIR)/libpmem.a -luuid -lm -lrt -lpthread -ldl 

SRC = *.c 3rdparty/pmdk/src/examples/libpmemobj/tree_map/btree_map.c

all: btree-on-nvdimm
btree-on-nvdimm: $(SRC) $(LIBDIR)/libpmemobj.a $(LIBDIR)/libpmem.a
	$(CC) $(DEFINES) $(CFLAGS) -o $@ $(SRC) $(LFLAGS)

3rdparty/pmdk/src/nondebug/libpmemobj.a:
	$(MAKE) -j8 -C 3rdparty/pmdk/src/libpmemobj
3rdparty/pmdk/src/nondebug/libpmem.a:
	$(MAKE) -j8 -C 3rdparty/pmdk/src/libpmem

.PHONY: clean

clean:
	$(RM) btree-on-nvdimm *~
