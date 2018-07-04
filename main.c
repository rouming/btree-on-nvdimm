#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "btree.h"
#include "libpmemobj/tree_map/btree_map.h"

#define MAX_UUIDS_PER_MEASURE 1000000
//#define MAX_UUIDS 300000000ull /* 300 mln */
#define MAX_UUIDS MAX_UUIDS_PER_MEASURE

static double clk_per_nsec;

static inline unsigned long long nsecs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((unsigned long long)ts.tv_sec * 1000000000ull) + ts.tv_nsec;
}

static inline unsigned long long cpu_clock(void)
{
	unsigned int lo, hi;

	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return ((unsigned long long) hi << 32ULL) | lo;
}

static void calibrate_cpu_clock(void)
{
	unsigned long long ns, clk;

	ns = nsecs();
	clk = cpu_clock();
	usleep(100000);
	clk = cpu_clock() - clk;
	ns = nsecs() - ns;

	clk_per_nsec = (double)clk / ns;
}

__attribute__((unused))
static unsigned long long clock_to_nsecs(unsigned long long clk)
{
	return (double)clk / clk_per_nsec;
}

static unsigned long long task_get_rss(void)
{
	int rss_rev_field = 52-24+1;
	unsigned long long rss;
	char buff[1024];
	size_t rd;
	int fd;

	const char *str = buff;

	snprintf(buff, sizeof(buff), "/proc/self/stat");
	fd = open(buff, O_RDONLY);
	if (fd < 0)
		return 0;

	rd = read(fd, buff, sizeof(buff) - 1);
	close(fd);
	if (rd < 2)
		return 0;

	buff[rd] = '\0';

	/* We do reverse search to avoid complicated parsing of the second
	   field, which is a process name, which obviously can contain spaces */
	for (; rd; rd--) {
		if (str[rd] == ' ')
			if (!--rss_rev_field)
				break;
	}
	if (!rd)
		return 0;

	if (1 != sscanf(str + rd, "%llu", &rss))
		return 0;

	/* Convert pages to bytes, assume normal pages */
	rss *= 4096;

	return rss;
}

extern void dump_btree(struct btree_head *head);

static int cmp(int v1, int v2)
{
	if (v1 < v2)
		return -1;
	if (v1 > v2)
		return 1;
	return 0;
}

int FFF_main(int argc, char *argv[])
{
	unsigned long long ns, num, rss;
	struct btree_head128 btree;
	uuid_t *many_uuids;
	double thd_psec;
	int i, rc;

	calibrate_cpu_clock();

	btree_init128(&btree);

	if (0)
	{
		unsigned vals[] = {10, 5};
		unsigned num = sizeof(vals)/sizeof(vals[0]);
		int l, r, m;

		unsigned key = 4;

		l = 0;
		r = num - 1;

		while (l <= r) {
			m = l + (r - l) / 2;
			if (cmp(vals[m], key) > 0)
				l = m + 1;
			else
				r = m - 1;
		}
		if (cmp(vals[m], key) > 0)
			m += 1;

		printf("%d\n", m);

		return 0;
	}

	if (0)
	{
		unsigned long long keys[2];

		keys[0] = 10;
		keys[1] = 10;

		rc = btree_insert128(&btree, keys[0], keys[1], (void *)0xaaaa, 0);
		assert(rc == 0);


		keys[0] = 5;
		keys[1] = 5;

		rc = btree_insert128(&btree, keys[0], keys[1], (void *)0xaaaa, 0);
		assert(rc == 0);


		keys[0] = 4;
		keys[1] = 4;

		rc = btree_insert128(&btree, keys[0], keys[1], (void *)0xaaaa, 0);
		assert(rc == 0);


		dump_btree(&btree.h);

		return 0;
	}


	if (0)
	{
		unsigned long long keys[2];
		int i;

		for (i = 1; i <= 6; i++) {
			keys[0] = i;
			keys[1] = i;

			rc = btree_insert128(&btree, keys[0], keys[1], (void *)0xaaaa, 0);
			assert(rc == 0);
		}

		dump_btree(&btree.h);

		return 0;
	}

	many_uuids = calloc(MAX_UUIDS_PER_MEASURE, sizeof(uuid_t));
	assert(many_uuids);

	for (num = 0; num < MAX_UUIDS; num += MAX_UUIDS_PER_MEASURE) {
		for (i = 0; i < MAX_UUIDS_PER_MEASURE; i++) {
			uuid_generate(many_uuids[i]);
		}

		ns = nsecs();
		for (i = 0; i < MAX_UUIDS_PER_MEASURE; i++) {
			u64 *keys = (void *)many_uuids[i];
			rc = btree_insert128(&btree, keys[0], keys[1], (void *)666, 0);
			assert(rc == 0);
		}
		ns = nsecs() - ns;

		thd_psec = (double)(MAX_UUIDS_PER_MEASURE/1000.0)/
			((double)ns / 1000000000.0);
		rss = task_get_rss();

		printf("%4.0f mln: %lld ms\t%4.1f thd/sec\t%lld mb rss\n",
		       (num + MAX_UUIDS_PER_MEASURE) / 1000000.0,
		       ns/1000/1000, thd_psec, rss >> 20);
	}


	return 0;
}

POBJ_LAYOUT_BEGIN(btree_on_nvdimm);
POBJ_LAYOUT_ROOT(two_lists, struct btree_on_nvdimm_root);
POBJ_LAYOUT_END(btree_on_nvdimm);

struct btree_on_nvdimm_root {
	TOID(struct btree_map) btree;
};

int main(int argc, char *argv[])
{
	const char *path = "./mem";
	PMEMobjpool *pop;

	if (access(path, F_OK) != 0) {
		if ((pop = pmemobj_create(path,
					  POBJ_LAYOUT_NAME(btree_on_nvdimm),
					  PMEMOBJ_MIN_POOL, 0666)) == NULL) {
			perror("failed to create pool\n");
			return 1;
		}
	} else {
		if ((pop = pmemobj_open(path,
					POBJ_LAYOUT_NAME(btree_on_nvdimm))) == NULL) {
			perror("failed to open pool\n");
			return 1;
		}
	}

	TOID(struct btree_on_nvdimm_root) r =
		POBJ_ROOT(pop, struct btree_on_nvdimm_root);

	printf("%p, oid.pool_uuid_lo=%lx, oid.off=%lx\n",
	       D_RW(r), r.oid.pool_uuid_lo, r.oid.off);

	return 0;
}
