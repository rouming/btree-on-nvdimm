extern struct pmem_btree_geo pmem_btree_geo128;

struct pmem_btree_head128 { struct pmem_btree_head h; };

/*
  static inline void pmem_btree_init_mempool128(struct pmem_btree_head128 *head,
  mempool_t *mempool)
  {
  pmem_btree_init_mempool(&head->h, mempool);
  }
*/

static inline int pmem_btree_init128(PMEMobjpool *pop,
				     struct pmem_btree_head128 *head)
{
	return pmem_btree_init(pop, &head->h);
}

static inline void pmem_btree_destroy128(struct pmem_btree_head128 *head)
{
	pmem_btree_destroy(&head->h);
}

static inline void *pmem_btree_lookup128(struct pmem_btree_head128 *head,
					 u64 k1, u64 k2)
{
	u64 key[2] = {k1, k2};
	return pmem_btree_lookup(&head->h, &pmem_btree_geo128, (unsigned long *)&key);
}

static inline void *pmem_btree_get_prev128(struct pmem_btree_head128 *head,
					   u64 *k1, u64 *k2)
{
	u64 key[2] = {*k1, *k2};
	void *val;

	val = pmem_btree_get_prev(&head->h, &pmem_btree_geo128,
				  (unsigned long *)&key);
	*k1 = key[0];
	*k2 = key[1];
	return val;
}

static inline int pmem_btree_insert128(struct pmem_btree_head128 *head,
				       u64 k1, u64 k2,
				       void *val, gfp_t gfp)
{
	u64 key[2] = {k1, k2};
	return pmem_btree_insert(&head->h, &pmem_btree_geo128,
				 (unsigned long *)&key, val, gfp);
}

static inline int pmem_btree_update128(struct pmem_btree_head128 *head,
				       u64 k1, u64 k2, void *val)
{
	u64 key[2] = {k1, k2};
	return pmem_btree_update(&head->h, &pmem_btree_geo128,
				 (unsigned long *)&key, val);
}

static inline void *pmem_btree_remove128(struct pmem_btree_head128 *head,
					 u64 k1, u64 k2)
{
	u64 key[2] = {k1, k2};
	return pmem_btree_remove(&head->h, &pmem_btree_geo128,
				 (unsigned long *)&key);
}

static inline void *pmem_btree_last128(struct pmem_btree_head128 *head,
				       u64 *k1, u64 *k2)
{
	u64 key[2];
	void *val;

	val = pmem_btree_last(&head->h, &pmem_btree_geo128,
			      (unsigned long *)&key[0]);
	if (val) {
		*k1 = key[0];
		*k2 = key[1];
	}

	return val;
}

static inline void pmem_dump_btree128(struct pmem_btree_head128 *head)
{
	pmem_dump_btree(&head->h);
}

static inline int pmem_btree_merge128(struct pmem_btree_head128 *target,
				      struct pmem_btree_head128 *victim,
				      gfp_t gfp)
{
	return pmem_btree_merge(&target->h, &victim->h, &pmem_btree_geo128, gfp);
}

void visitor128(void *elem, unsigned long opaque, unsigned long *__key,
		size_t index, void *__func);

typedef void (*visitor128_t)(void *elem, unsigned long opaque,
			     u64 key1, u64 key2, size_t index);

static inline size_t pmem_btree_visitor128(struct pmem_btree_head128 *head,
					   unsigned long opaque,
					   visitor128_t func2)
{
	return pmem_btree_visitor(&head->h, &pmem_btree_geo128, opaque,
				  visitor128, func2);
}

static inline size_t pmem_btree_grim_visitor128(struct pmem_btree_head128 *head,
						unsigned long opaque,
						visitor128_t func2)
{
	return pmem_btree_grim_visitor(&head->h, &pmem_btree_geo128, opaque,
				       visitor128, func2);
}

#define pmem_btree_for_each_safe128(head, k1, k2, val)		\
	for (val = pmem_btree_last128(head, &k1, &k2);		\
	     val;						\
	     val = pmem_btree_get_prev128(head, &k1, &k2))
