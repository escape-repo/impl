/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2016 Intel Corporation
 */

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
#include <unistd.h>
#include <signal.h>

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
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "main.h"

static void
signal_handler(int signum)
{
	uint32_t i;
	
	if (signum != SIGINT && signum != SIGTERM)
		return;
	
	printf("\nSignal %d received, preparing to exit...\n", 
				signum);
	
	for (i = 0; i < APP_MAX_PORTS; i++) {
		fclose(app.port_state[i].tp_in.data);
		fclose(app.port_state[i].tp_out.data);
		fclose(app.port_state[i].qs_in.data);
		fclose(app.port_state[i].qs_out.data);
        }
	
	exit(0);
}

int
main(int argc, char **argv)
{
	uint32_t lcore;
	int ret;

	/* Init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		return -1;
	argc -= ret;
	argv += ret;

	signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

	/* Parse application arguments (after the EAL ones) */
	ret = app_parse_args(argc, argv);
	if (ret < 0) {
		app_print_usage();
		return -1;
	}

	/* Init */
	app_init();

	/* Launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(app_lcore_main_loop, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore) {
		if (rte_eal_wait_lcore(lcore) < 0)
			return -1;
	}

	return 0;
}

int
app_lcore_main_loop(__attribute__((unused)) void *arg)
{
	unsigned lcore, i;

	lcore = rte_lcore_id();

	for (i = 0; i < app.n_lcores; i++) {
		if (app.lcores[i] == lcore) {
			app_main_loop(i); /* n_ports = n_lcores */
			return 0;
		}
	}
	
	return 0;
}
