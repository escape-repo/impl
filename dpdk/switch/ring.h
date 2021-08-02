#ifndef _RING_H_
#define _RING_H_

typedef uint16_t (*check_obj)(const void *obj1, const void *obj2);

static __rte_always_inline uint16_t
rte_ring_sc_find(struct rte_ring *ring,
		check_obj func, const void * obj2)
{
	const void **table = (const void **)&ring[1];
	uint32_t num_obj, cons_head, counter, index;

	rte_smp_rmb();

	cons_head = ring->cons.head;

	num_obj = rte_ring_count(ring);

	for (counter = 0; counter < num_obj; counter++) {
		index = (cons_head + counter) & ring->mask;
		if (func(table[index], obj2))
			return 1;
	}

	return 0; 
}

static __rte_always_inline void *
rte_ring_sc_jump(struct rte_ring *ring,
		check_obj func, const void *obj2)
{
	void **table = (void **)&ring[1];
	void *obj = NULL;
	uint32_t cons_head, cons_tail, num_obj;
	uint32_t counter, index;
	uint32_t shift_from, shift_to;

	rte_smp_wmb();

	cons_head = ring->cons.head;
	cons_tail = ring->cons.tail;

	num_obj = rte_ring_count(ring);

	for (counter = 0; counter < num_obj; counter++) {
		index = (cons_head + counter) & ring->mask;
		if (!func(table[index], obj2))
			continue;
		obj = table[index];
		shift_to = index;
		//RTE_LOG(INFO, USER1, "found msg at index %u\n", index);
		while (shift_to != (cons_tail & ring->mask)) {
                        shift_from = (shift_to - 1) & ring->mask;
                        table[shift_to] = table[shift_from];
                        shift_to = shift_from;
                }
		ring->cons.head = ring->cons.tail = cons_tail + (uint32_t)1;
		break;
	}

	return obj;
}

#endif /* _RING_H_ */
