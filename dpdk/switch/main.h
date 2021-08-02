/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2016 Intel Corporation
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#ifndef APP_MBUF_ARRAY_SIZE
#define APP_MBUF_ARRAY_SIZE 1024
#endif

struct app_mbuf_array {
	struct rte_mbuf *array[APP_MBUF_ARRAY_SIZE];
	uint16_t n_mbufs;
};

#ifndef APP_MAX_PORTS
#define APP_MAX_PORTS 3
#endif

#ifndef APP_MAX_SUBNETS
#define APP_MAX_SUBNETS 10
#endif

#define FLOW_TABLE_SIZE 3

struct tp_meter {
	uint16_t enabled;
	uint64_t intvl_num_pkts;
        uint64_t intvl_num_bits;
        uint64_t intvl_start_time;
	FILE *data;
};

struct qs_meter {
	uint16_t enabled;
	FILE *data;
};

struct pause_meter {
	uint16_t enabled;
	FILE *data;
};

struct port_lock {
	rte_atomic16_t mutex;
	uint16_t locked;
	uint16_t ext_req; /* iport only */
	uint32_t flow_id;
};

struct port_state {
	uint16_t pfc_enabled;
	rte_atomic16_t pfc_requested;
	rte_atomic16_t pfc_enforced;
	struct port_lock lock_in;
	struct port_lock lock_out;
	struct tp_meter tp_in;
	struct tp_meter tp_out;
	struct qs_meter qs_in;
	struct qs_meter qs_out;
	struct pause_meter pause;
	uint64_t start_time;
	uint64_t qlen_in;
};

struct flow {
	uint32_t id;
	uint16_t in_port;
	uint16_t out_port;
};

struct app_params {
	/* CPU cores */
	uint16_t lcores[APP_MAX_PORTS]; /* lcore per port */
	uint16_t n_lcores;

	/* Ports*/
	uint16_t ports[APP_MAX_PORTS];
	uint16_t n_ports;

	/* Rings */
	struct rte_ring *rings_rx[APP_MAX_PORTS];
	struct rte_ring *rings_tx[APP_MAX_PORTS];
	struct rte_ring *rings_tx_esc[APP_MAX_PORTS];
	uint16_t ring_rx_size;
	uint16_t ring_tx_size;

	/* Internal buffers */
	struct app_mbuf_array mbuf_rx[APP_MAX_PORTS];
	struct app_mbuf_array mbuf_wk[APP_MAX_PORTS];
	struct app_mbuf_array mbuf_tx[APP_MAX_PORTS];

	/* Buffer pool */
	struct rte_mempool *pool;
	uint16_t pool_buffer_size;
	uint16_t pool_size;
	uint16_t pool_cache_size;

	/* Burst sizes */
	uint16_t burst_size_rx_read;
	unsigned burst_size_wk_read;
	unsigned burst_size_tx_read;

	/* Ethernet addresses */
	struct ether_addr port_eth_addr[APP_MAX_PORTS];
	struct ether_addr next_hop_eth_addr[APP_MAX_PORTS];

	/* Port state */
	struct port_state port_state[APP_MAX_PORTS];

	/* Routing table */
	int routing_table[APP_MAX_SUBNETS];

	/* Flow tables */
	struct flow flow_tables[APP_MAX_PORTS][FLOW_TABLE_SIZE];
} __rte_cache_aligned;

extern struct app_params app;

int app_parse_args(int argc, char **argv);
void app_print_usage(void);
void app_init(void);
int app_lcore_main_loop(void *arg);

void app_main_loop(uint16_t port);
void handle_routing(uint16_t port);
void handle_tx(uint16_t port);
void handle_tx_esc(uint16_t port);

#define APP_FLUSH 0
#ifndef APP_FLUSH
#define APP_FLUSH 0x3FF
#endif

#define APP_METADATA_OFFSET(offset) (sizeof(struct rte_mbuf) + (offset))

#endif /* _MAIN_H_ */
