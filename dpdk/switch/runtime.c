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
#include <signal.h>
#include <sys/time.h>
#include <math.h>

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
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_icmp.h>
#include <rte_udp.h>
#include <rte_malloc.h>

#include "main.h"
#include "common.h"
#include "ring.h"
#include "rx.h"
#include "routing.h"
#include "tx.h"
#include "escape.h"

static uint16_t
consume_control_frames(uint16_t port,
		struct port_state *port_state,
		struct rte_mbuf **array,
		uint16_t n_mbufs)
{
        char found;
        uint16_t index1, index2, total;
        struct rte_mbuf *mbuf;
        struct ether_hdr *eth_hdr;
        struct rte_mbuf *array_tmp[APP_MBUF_ARRAY_SIZE];

        if (n_mbufs) {
                rte_memcpy((void*)array_tmp, (const void*)array,
                                sizeof(struct rte_mbuf *) * n_mbufs);
                total = n_mbufs;
                index2 = 0;
                for (index1 = 0; index1 < total; index1++) {
                        found = 0;
                        mbuf = array_tmp[index1];
                        eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
                        if (rte_be_to_cpu_16(eth_hdr->ether_type) ==
					ETHER_TYPE_FLOW_CONTROL) {
				if (consume_pause_frame(port_state, eth_hdr) ||
						consume_ecf_frame(port, port_state, eth_hdr))
					found = 1;
                        }
                        if (found) {
                                rte_pktmbuf_free(mbuf);
                                n_mbufs--;
                        } else
                                array[index2++] = mbuf;
                }
        }

        return n_mbufs;
}

void
app_main_loop(uint16_t port) {
	const uint64_t SEC_TSC = rte_get_tsc_hz();
	const uint64_t ESC_TSC = (SEC_TSC / US_PER_S) * ESCAPE_T;
	
	uint64_t prev_tsc = 0;
	
	RTE_LOG(INFO, USER1, "Core %u is receiving from port %u\n",
			rte_lcore_id(), port);
	
	while (1) {
		uint16_t n_batch, n_read;
		unsigned n_write;
		struct port_state *port_state;

		port_state = &app.port_state[port];
		
		/* Read from port */
		n_batch = rte_ring_free_count(app.rings_rx[port]);
                if (app.burst_size_rx_read < n_batch)
                        n_batch = app.burst_size_rx_read;
		n_read = rte_eth_rx_burst(app.ports[port], 0,
				app.mbuf_rx[port].array, n_batch);

		//uint16_t x;
                //for (x = 0; x < n_read; x++)
                //       print_iperf_seq(port, app.mbuf_rx[port].array[x], "ingress");
		
		/* Consume PAUSE and ECF frames */
		n_read = consume_control_frames(port, port_state,
				app.mbuf_rx[port].array, n_read);

		/* Update in throughput */
		update_throughput(&port_state->tp_in,
				app.mbuf_rx[port].array,
				n_read,
				port_state->start_time, SEC_TSC);

		/* Handle escaping pkt */
		n_read = handle_esc_pkt(port, port_state,
				app.mbuf_rx[port].array, n_read);

		/* Add to RX ring */
		n_write = rte_ring_sp_enqueue_burst(app.rings_rx[port],
				(void **)app.mbuf_rx[port].array, n_read, NULL);
		if (unlikely(n_write != n_read))
			rte_panic("Failed to write all pkts to RX ring!");
		port_state->qlen_in += get_data_size(app.mbuf_rx[port].array, n_write);

		/* Handle PFC XOFF */
		handle_pfc_xoff(port, port_state);

		/* Handle routing */
		handle_routing(port);
		
		/* Handle Escape */
		app_process_esc_timer(port, &prev_tsc, ESC_TSC);
		handle_tunnel_ext_req(port, port_state);
		
		/* Handle PFC XON */
		handle_pfc_xon(port, port_state);

		/* Handle Tx */
		handle_tx_esc(port);
		handle_tx(port);

		/* Log queue size */
		if (port_state->qs_in.enabled) {
			uint64_t cur_tsc;
			double cur_time;

			cur_tsc = rte_rdtsc();
			cur_time = (cur_tsc - port_state->start_time) / (double)SEC_TSC;
                	fprintf(port_state->qs_in.data, "%f %f\n",
					cur_time, port_state->qlen_in / 1000.0);
		}

		usleep(1);
	}
}

void
handle_routing(uint16_t port) {
	unsigned n_batch, n_read;
	uint16_t n_mbufs, n_route, i;
	struct port_state *port_state;
	
	port_state = &app.port_state[port];

	/* Read */
	n_mbufs = app.mbuf_wk[port].n_mbufs;
	n_batch = app.burst_size_wk_read - n_mbufs;
	n_read = rte_ring_sc_dequeue_burst(app.rings_rx[port],
			(void **)&app.mbuf_wk[port].array[n_mbufs], n_batch, NULL);
	n_mbufs += n_read;
	
	/* Route messages */
	n_route = 0;
	for (i = 0; i < n_mbufs; i++) {
		uint64_t pkt_len = app.mbuf_wk[port].array[i]->pkt_len;
		if (!forward_msg(port, app.mbuf_wk[port].array[i], 0, 0))
			break;
		port_state->qlen_in -= pkt_len;
		n_route++;
	}
	rte_memcpy((void *)app.mbuf_wk[port].array,
			(const void *)&app.mbuf_wk[port].array[n_route],
			sizeof(struct rte_mbuf *) * (n_mbufs - n_route));
	app.mbuf_wk[port].n_mbufs = n_mbufs - n_route;
}

void
handle_tx(uint16_t port) {
	const uint64_t SEC_TSC = rte_get_tsc_hz();

	unsigned n_read;
	uint16_t i;
	struct port_state *port_state;

	port_state = &app.port_state[port];
	if (rte_atomic16_read(&port_state->pfc_enforced))
		return;

	/* Read */
	n_read = rte_ring_sc_dequeue_burst(app.rings_tx[port],
			(void **)app.mbuf_tx[port].array,
			app.burst_size_tx_read, NULL);
	if (n_read == 0)
		return;

	/* Update out throughput */
	update_throughput(&port_state->tp_out,
			app.mbuf_tx[port].array,
			n_read,
			port_state->start_time, SEC_TSC);
	
	/* Send */
	for (i = 0; i < n_read; i++)
		eth_tx_pkt(port, app.mbuf_tx[port].array[i]);
}

void
handle_tx_esc(uint16_t port) {
	const uint64_t SEC_TSC = rte_get_tsc_hz();

	unsigned n_read;
	uint16_t i;
	struct port_state *port_state;

	port_state = &app.port_state[port];

	/* Read */
	n_read = rte_ring_sc_dequeue_burst(app.rings_tx_esc[port],
			(void **)app.mbuf_tx[port].array,
			app.burst_size_tx_read, NULL);
	if (n_read == 0)
		return;

	if (n_read != 1)
		rte_panic("Port %u: tx more than one escaping pkts!\n", port);

	/*if (!rte_atomic16_read(&port_state->lock_out.locked)) {
		uint16_t protocol;
        	uint32_t flow_id;
        	uint32_t src_addr, dst_addr;
		struct rte_mbuf *mbuf = app.mbuf_tx[port].array[0];
		
		flow_id = get_flow_id(mbuf);
                src_addr = get_src_addr(mbuf);
                dst_addr = get_dst_addr(mbuf);
                protocol = get_protocol(mbuf);
		print_pkt(port, flow_id, 9999, port, protocol, src_addr, dst_addr);
		rte_panic("Port %u received %u escaping pkts when the port is not locked!\n",
				port, n_read);
	}*/

	/* Update out throughput */
	update_throughput(&port_state->tp_out,
			app.mbuf_tx[port].array,
			n_read,
			port_state->start_time, SEC_TSC);
	
	/* Send */
	for (i = 0; i < n_read; i++)
		eth_tx_pkt(port, app.mbuf_tx[port].array[i]);
}
