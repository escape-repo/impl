/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_string_fns.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_lpm.h>
#include <rte_lpm6.h>

#include "main.h"

struct app_params app = {
	/* lcores */
	.n_lcores = APP_MAX_PORTS,

	/* Ports*/
	.n_ports = APP_MAX_PORTS,

	/* Rings */
	.ring_rx_size = 1024,
	.ring_tx_size = 1024,

	/* Buffer pool */
	.pool_buffer_size = 2048 + RTE_PKTMBUF_HEADROOM,
	.pool_size = 32 * 1024,
	.pool_cache_size = 256,

	/* Burst sizes */
	.burst_size_rx_read = 4,
	.burst_size_wk_read = 1,
	.burst_size_tx_read = 1,
};

static struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode = ETH_MQ_RX_NONE,
		.max_rx_pkt_len = ETHER_MAX_LEN,
		.split_hdr_size = 0,
		.offloads = DEV_RX_OFFLOAD_CHECKSUM,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_hf = ETH_RSS_IP,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

static struct rte_eth_rxconf rx_conf = {
	.rx_thresh = {
		.pthresh = 8,
		.hthresh = 8,
		.wthresh = 4,
	},
	.rx_free_thresh = 64,
	.rx_drop_en = 0,
};

static struct rte_eth_txconf tx_conf = {
	.tx_thresh = {
		.pthresh = 36,
		.hthresh = 0,
		.wthresh = 0,
	},
	.tx_free_thresh = 0,
	.tx_rs_thresh = 0,
};

static void
app_init_escape(uint16_t port, struct port_state *port_state)
{
	char fname[32];

	port_state->pfc_enabled = 1;
	rte_atomic16_init(&port_state->pfc_requested);
	rte_atomic16_set(&port_state->pfc_requested, 0);
	rte_atomic16_init(&port_state->pfc_enforced);
	rte_atomic16_set(&port_state->pfc_enforced, 0);
	rte_atomic16_init(&port_state->lock_in.mutex);
	rte_atomic16_set(&port_state->lock_in.mutex, 0);
	rte_atomic16_init(&port_state->lock_out.mutex);
	rte_atomic16_set(&port_state->lock_out.mutex, 0);
	port_state->lock_in.locked = 0;
	port_state->lock_in.ext_req = 0;
	port_state->lock_in.flow_id = 0;
	port_state->lock_out.locked = 0;
	port_state->lock_out.ext_req = 0;
	port_state->lock_out.flow_id = 0;
	/* Throughput in */
	port_state->tp_in.enabled = 0;
	port_state->tp_in.intvl_num_pkts = 0;
        port_state->tp_in.intvl_num_bits = 0;
        port_state->tp_in.intvl_start_time = 0;
	memset(fname, 0, sizeof(fname));
	sprintf(fname, "tp-in-port-%u.dat", port);
	port_state->tp_in.data = fopen(fname, "w");
	if (port_state->tp_in.data == NULL)
		rte_panic("Cannot create data file: %s\n", fname);
	/* Throughput out */
	port_state->tp_out.enabled = 0;
       	port_state->tp_out.intvl_num_pkts = 0;
        port_state->tp_out.intvl_num_bits = 0;
        port_state->tp_out.intvl_start_time = 0;
	memset(fname, 0, sizeof(fname));
	sprintf(fname, "tp-out-port-%u.dat", port);
	port_state->tp_out.data = fopen(fname, "w");
	if (port_state->tp_out.data == NULL)
		rte_panic("Cannot create data file: %s\n", fname);
	/* Queue size in */
	port_state->qs_in.enabled = 1;
	memset(fname, 0, sizeof(fname));
	sprintf(fname, "qs-in-port-%u.dat", port);
	port_state->qs_in.data = fopen(fname, "w");
	if (port_state->qs_in.data == NULL)
		rte_panic("Cannot create data file: %s\n", fname);
	/* Queue size out */
	port_state->qs_out.enabled = 0;
	memset(fname, 0, sizeof(fname));
	sprintf(fname, "qs-out-port-%u.dat", port);
	port_state->qs_out.data = fopen(fname, "w");
	if (port_state->qs_out.data == NULL)
		rte_panic("Cannot create data file: %s\n", fname);
	/* Link pause */
	port_state->pause.enabled = 1;
	memset(fname, 0, sizeof(fname));
	sprintf(fname, "pause-port-%u.dat", port);
	port_state->pause.data = fopen(fname, "w");
	if (port_state->pause.data == NULL)
		rte_panic("Cannot create data file: %s\n", fname);
	port_state->start_time = rte_rdtsc();
	port_state->qlen_in = 0;
}

static void
app_init_params(void)
{
	uint16_t i;

	for (i = 0; i < APP_MAX_PORTS; i++) {
		app.mbuf_rx[i].n_mbufs =
			app.mbuf_wk[i].n_mbufs =
			app.mbuf_tx[i].n_mbufs = 0;
		app_init_escape(i, &app.port_state[i]);
	}
	memset(app.flow_tables, 0, sizeof(app.flow_tables));
}

static void
app_init_mbuf_pools(void)
{
	/* Init the buffer pool */
	RTE_LOG(INFO, USER1, "Creating the mbuf pool ...\n");
	app.pool = rte_pktmbuf_pool_create("mempool", app.pool_size,
		app.pool_cache_size, 0, app.pool_buffer_size, rte_socket_id());
	if (app.pool == NULL)
		rte_panic("Cannot create mbuf pool\n");
}

static void
app_init_rings(void)
{
	uint16_t i;

	for (i = 0; i < app.n_ports; i++) {
		char name[32];

		snprintf(name, sizeof(name), "app_ring_rx_%u", i);

		app.rings_rx[i] = rte_ring_create(
			name,
			app.ring_rx_size,
			rte_socket_id(),
			RING_F_SP_ENQ | RING_F_SC_DEQ);
		
		if (app.rings_rx[i] == NULL)
			rte_panic("Cannot create RX ring %u\n", i);
	}

	for (i = 0; i < app.n_ports; i++) {
		char name[32];

		snprintf(name, sizeof(name), "app_ring_tx_%u", i);

		app.rings_tx[i] = rte_ring_create(
			name,
			app.ring_tx_size,
			rte_socket_id(),
			RING_F_SP_ENQ | RING_F_SC_DEQ);
		
		if (app.rings_tx[i] == NULL)
			rte_panic("Cannot create TX ring %u\n", i);
	}

	for (i = 0; i < app.n_ports; i++) {
		char name[32];

		snprintf(name, sizeof(name), "app_ring_tx_esc_%u", i);

		app.rings_tx_esc[i] = rte_ring_create(
				name,
				app.ring_tx_size, /* TODO: 1 is sufficient */
				rte_socket_id(),
				RING_F_SP_ENQ | RING_F_SC_DEQ); /* TODO: this queue is single prod */

		if (app.rings_tx_esc[i] == NULL)
			rte_panic("Cannot create Escape TX ring %u\n", i);
	}
}

static void
app_ports_check_link(void)
{
	uint16_t all_ports_up, i;

	all_ports_up = 1;

	for (i = 0; i < app.n_ports; i++) {
		struct rte_eth_link link;
		uint16_t port;

		port = app.ports[i];
		memset(&link, 0, sizeof(link));
		rte_eth_link_get_nowait(port, &link);
		RTE_LOG(INFO, USER1, "Port %u (%u Gbps) %s\n",
			port,
			link.link_speed / 1000,
			link.link_status ? "UP" : "DOWN");

		if (link.link_status == ETH_LINK_DOWN)
			all_ports_up = 0;
	}

	if (all_ports_up == 0)
		rte_panic("Some NIC ports are DOWN\n");
}

static void
app_init_ports(void)
{
	uint16_t i;

	/* Init NIC ports, then start the ports */
	for (i = 0; i < app.n_ports; i++) {
		uint16_t port;
		int ret;
		struct rte_eth_dev_info dev_info;
		struct rte_eth_conf local_port_conf = port_conf;

		port = app.ports[i];
		RTE_LOG(INFO, USER1, "Initializing NIC port %u ...\n", port);

		/* Modify RSS hash function */
		rte_eth_dev_info_get(port, &dev_info);

		local_port_conf.rx_adv_conf.rss_conf.rss_hf &= 
			dev_info.flow_type_rss_offloads;
		if (local_port_conf.rx_adv_conf.rss_conf.rss_hf != 
				port_conf.rx_adv_conf.rss_conf.rss_hf) {
			printf("Port %u modified RSS hash function based on hardware support,"
				"requested:%#"PRIx64" configured:%#"PRIx64"\n",
				port,
				port_conf.rx_adv_conf.rss_conf.rss_hf,
				local_port_conf.rx_adv_conf.rss_conf.rss_hf);
		}

		/* Init port */
		ret = rte_eth_dev_configure(
			port,
			1,
			1,
			&local_port_conf);
		if (ret < 0)
			rte_panic("Cannot init NIC port %u (%d)\n", port, ret);

		//rte_eth_promiscuous_enable(port);
		rte_eth_promiscuous_disable(port);

		rte_eth_macaddr_get(port, &app.port_eth_addr[port]);

		/* Init RX queues */
		ret = rte_eth_rx_queue_setup(
			port,
			0,
			/*app.ring_rx_size*/96,
			rte_eth_dev_socket_id(port),
			&rx_conf,
			app.pool);
		if (ret < 0)
			rte_panic("Cannot init RX queue for port %u (%d)\n",
					port, ret);

		/* Init TX queues */
		ret = rte_eth_tx_queue_setup(
			port,
			0,
			/*app.ring_tx_size*/96,
			rte_eth_dev_socket_id(port),
			&tx_conf);
		if (ret < 0)
			rte_panic("Cannot init TX queue for port %u (%d)\n",
					port, ret);

		/* Start port */
		rte_eth_dev_stop(port); /* in case it's not closed properly */
		ret = rte_eth_dev_start(port);
		if (ret < 0)
			rte_panic("Cannot start port %u (%d)\n", port, ret);

		printf("Port %u, MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
				port,
				app.port_eth_addr[port].addr_bytes[0],
				app.port_eth_addr[port].addr_bytes[1],
				app.port_eth_addr[port].addr_bytes[2],
				app.port_eth_addr[port].addr_bytes[3],
				app.port_eth_addr[port].addr_bytes[4],
				app.port_eth_addr[port].addr_bytes[5]);
		printf("Port %u, next hop MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
				port,
				app.next_hop_eth_addr[port].addr_bytes[0],
				app.next_hop_eth_addr[port].addr_bytes[1],
				app.next_hop_eth_addr[port].addr_bytes[2],
				app.next_hop_eth_addr[port].addr_bytes[3],
				app.next_hop_eth_addr[port].addr_bytes[4],
				app.next_hop_eth_addr[port].addr_bytes[5]);
	}

	sleep(5);

	app_ports_check_link();
}

void
app_init(void)
{
	app_init_params();
	app_init_mbuf_pools();
	app_init_rings();
	app_init_ports();

	RTE_LOG(INFO, USER1, "Initialization completed\n");
}
