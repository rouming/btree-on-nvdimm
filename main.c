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

#include "types.h"
#include "btree.h"
#include "libpmemobj/tree_map/btree_map.h"

//#define MAX_UUIDS_PER_MEASURE 1000000
//#define MAX_UUIDS 300000000ull /* 300 mln */
//#define MAX_UUIDS MAX_UUIDS_PER_MEASURE

#define MAX_UUIDS_PER_MEASURE 100
#define MAX_UUIDS 300


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

struct tree_ops {
	int (*init)(struct tree_ops *ops);
	void (*deinit)(struct tree_ops *ops);
	int (*insert)(struct tree_ops *ops, uint64_t keys[2], uint64_t val);
};

struct mem_btree {
	struct tree_ops ops;
	struct btree_head128 btree;
};

static int mem_btree_init(struct tree_ops *ops)
{
	struct mem_btree *btree;

	btree = container_of(ops, typeof(*btree), ops);
	btree_init128(&btree->btree);

	return 0;
}

static void mem_btree_deinit(struct tree_ops *ops)
{
}

static int mem_btree_insert(struct tree_ops *ops, uint64_t keys[2],
			    uint64_t val)
{
	struct mem_btree *btree;

	btree = container_of(ops, typeof(*btree), ops);
	return btree_insert128(&btree->btree, keys[0], keys[1], (void *)val, 0);
}

static struct tree_ops mem_btree_ops = {
	.init   = mem_btree_init,
	.deinit = mem_btree_deinit,
	.insert = mem_btree_insert
};

POBJ_LAYOUT_BEGIN(pmem_btree_root);
POBJ_LAYOUT_ROOT(pmem_btree_root, struct pmem_btree_root)
POBJ_LAYOUT_END(pmem_btree_root);

struct pmem_btree_root {
	TOID(struct btree_map) btree;
};

struct pmem_btree {
	struct tree_ops ops;
	PMEMobjpool *pop;
	TOID(struct pmem_btree_root) root;
};

static int pmem_count(uint64_t key, PMEMoid value, void *arg)
{
	unsigned *cnt = arg;

	++*cnt;

	return 0;
}

static int pmem_btree_init(struct tree_ops *ops)
{
	struct pmem_btree *btree;
	const char *path = "./mem";
	PMEMobjpool *pop;
	int rc;

	//XXX
//	unlink(path);

	/* Do not msync() */
	setenv("PMEM_IS_PMEM_FORCE", "1", 1);

	btree = container_of(ops, typeof(*btree), ops);

	if (access(path, F_OK) != 0) {
		if ((pop = pmemobj_create(path,
					  POBJ_LAYOUT_NAME(pmem_btree_root),
					  PMEMOBJ_MIN_POOL, 0666)) == NULL) {
			perror("failed to create pool\n");
			return -1;
		}
	} else {
		if ((pop = pmemobj_open(path,
					POBJ_LAYOUT_NAME(pmem_btree_root))) == NULL) {
			perror("failed to open pool\n");
			return -1;
		}
	}

	btree->pop  = pop;
	btree->root = POBJ_ROOT(pop, struct pmem_btree_root);

	rc = btree_map_check(btree->pop, D_RO(btree->root)->btree);
	if (rc)
		rc = btree_map_create(btree->pop, &D_RW(btree->root)->btree, NULL);
	else {
		unsigned cnt = 0;
		btree_map_foreach(btree->pop, D_RO(btree->root)->btree,
				  pmem_count, &cnt);
		printf(">>> count=%d\n", cnt);
	}

	return rc;
}

static void pmem_btree_deinit(struct tree_ops *ops)
{
	struct pmem_btree *btree;

	btree = container_of(ops, typeof(*btree), ops);
	pmemobj_close(btree->pop);
}

static int pmem_btree_insert(struct tree_ops *ops, uint64_t keys[2],
			     uint64_t val)
{
	struct pmem_btree *btree;
	int rc;

	btree = container_of(ops, typeof(*btree), ops);

	rc = btree_map_lookup(btree->pop, D_RO(btree->root)->btree, keys[0]);
	if (rc) {
		printf(">>>> exists: %lx\n", keys[0]);
		assert(0);
	}

	return btree_map_insert(btree->pop, D_RO(btree->root)->btree,
				keys[0], OID_NULL);
}

static struct tree_ops pmem_btree_ops = {
	.init   = pmem_btree_init,
	.deinit = pmem_btree_deinit,
	.insert = pmem_btree_insert
};

int main(int argc, char *argv[])
{
	unsigned long long ns, num, rss;
	uuid_t *many_uuids;
	double thd_psec;
	int i, rc;

	__attribute__((unused))
	struct mem_btree btree1 = {
		.ops = mem_btree_ops
	};

	struct pmem_btree btree = {
		.ops = pmem_btree_ops
	};

	calibrate_cpu_clock();

	rc = btree.ops.init(&btree.ops);
	assert(rc == 0);

	many_uuids = calloc(MAX_UUIDS_PER_MEASURE, sizeof(uuid_t));
	assert(many_uuids);

	for (num = 0; num < MAX_UUIDS; num += MAX_UUIDS_PER_MEASURE) {
		for (i = 0; i < MAX_UUIDS_PER_MEASURE; i++) {
			uuid_generate(many_uuids[i]);
		}

		ns = nsecs();
		for (i = 0; i < MAX_UUIDS_PER_MEASURE; i++) {
			u64 *keys = (void *)many_uuids[i];
			rc = btree.ops.insert(&btree.ops, keys, 666);
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

	btree.ops.deinit(&btree.ops);

	return 0;
}


struct big_struct {
	int v1;
	int v2;
};

struct small_struct {
	double a;
	double b;
};

POBJ_LAYOUT_BEGIN(test_root);
POBJ_LAYOUT_ROOT(test_root, struct test_root);
POBJ_LAYOUT_TOID(test_root, struct big_struct);
POBJ_LAYOUT_TOID(test_root, struct small_struct);
POBJ_LAYOUT_END(test_root);

struct test_root {
	TOID(struct big_struct) big;
	TOID(struct small_struct) small;
	int done;
};

int pmem_experiments_main(int argc, char *argv[])
{
	TOID(struct test_root) root_toid;
	struct test_root *root;
	const struct small_struct *small;
	const struct big_struct *big;

	const char *path = "./mem";
	PMEMobjpool *pop;

	if (access(path, F_OK) != 0) {
		if ((pop = pmemobj_create(path,
					  POBJ_LAYOUT_NAME(btree_on_nvdimm),
					  PMEMOBJ_MIN_POOL, 0666)) == NULL) {
			perror("failed to create pool\n");
			return -1;
		}
		printf(">> created\n");
	} else {
		if ((pop = pmemobj_open(path,
					POBJ_LAYOUT_NAME(btree_on_nvdimm))) == NULL) {
			perror("failed to open pool\n");
			return -1;
		}
		printf(">> opened\n");
	}

	root_toid = POBJ_ROOT(pop, struct test_root);
	root = D_RW(root_toid);

	printf(">>> small=%p\n", D_RO(root->small));
	printf(">>> big=%p\n", D_RO(root->big));

	TX_BEGIN(pop) {
		TX_BEGIN(pop) {
			pmemobj_tx_add_range_direct(root, sizeof(*root));
			if (TOID_IS_NULL(root->small)) {
				struct small_struct *small;
				printf(">> small is invalid\n");
				root->small = TX_NEW(struct small_struct);
				small = D_RW(root->small);
				small->a = 666.666;
				small->b = 11.22;
			}
			if (TOID_IS_NULL(root->big)) {
				struct big_struct *big;

				printf(">> big is invalid\n");
				root->big = TX_NEW(struct big_struct);
				big = D_RW(root->big);
				big->v1 = 11;
				big->v2 = 22;

			}

			root->done = 6666;

		} TX_ONCOMMIT {
			printf(">>>>>>>> COMMIT\n");
		} TX_ONABORT {
			printf(">>>>>>>> ABORT\n");
		} TX_FINALLY {
			printf(">>>>>>>> FINALLY\n");
		} TX_END;

//XXX		pmemobj_tx_abort(-1);
	}
	TX_ONCOMMIT {
		printf(">>>>>>>> COMMIT outer\n");
	} TX_ONABORT {
		printf(">>>>>>>> ABORT outer\n");
	} TX_FINALLY {
		printf(">>>>>>>> FINALLY outer\n");
	} TX_END;

	small = D_RO(root->small);
	big   = D_RO(root->big);

	printf(">>> small=%p\n", small);
	printf(">>> big  =%p\n", big);

	printf(">>> done=%d, v1=%d, v2=%d, a=%f, b=%f\n",
	       root->done,
	       big->v1, big->v2, small->a, small->b);

	return 0;
}

int experiments_main()
{
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
		struct btree_head128 btree;
		unsigned long long keys[2];
		int rc;

		btree_init128(&btree);

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
		struct btree_head128 btree;
		unsigned long long keys[2];
		int i, rc;

		for (i = 1; i <= 6; i++) {
			keys[0] = i;
			keys[1] = i;

			rc = btree_insert128(&btree, keys[0], keys[1], (void *)0xaaaa, 0);
			assert(rc == 0);
		}

		dump_btree(&btree.h);

		return 0;
	}

	return 0;
}
