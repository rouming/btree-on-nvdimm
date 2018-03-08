# Makefile btree-on-nvdimm tool

CC = $(CROSS_COMPILE)gcc
DEFINES=

CFLAGS = -O2 -g -Wall -luuid -lm -lrt

all: btree-on-nvdimm
btree-on-nvdimm: *.c
	$(CC) $(DEFINES) $(CFLAGS) -o $@ $^

clean:
	$(RM) btree-on-nvdimm *~
