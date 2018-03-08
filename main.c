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

#define MAX_UUIDS_PER_MEASURE 1000000
#define MAX_UUIDS 300000000ull /* 300 mln */

static inline unsigned long long nsecs(void)
{
    struct timespec ts = {0, 0};

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((unsigned long long)ts.tv_sec * 1000000000ull) + ts.tv_nsec;
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

int main(int argc, char *argv[])
{
	unsigned long long ns, num, rss;
	struct btree_head128 btree;
	uuid_t *many_uuids;
	double thd_psec;
	int i, rc;

	btree_init128(&btree);

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
