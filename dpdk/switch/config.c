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
#include <rte_lpm.h>
#include <rte_lpm6.h>
#include <rte_string_fns.h>

#include "main.h"

struct app_params app;

static const char usage[] = "\n";

void
app_print_usage(void)
{
	printf(usage);
}

static int
app_parse_port_mask(const char *arg)
{
	char *end = NULL;
	uint64_t port_mask;
	uint32_t i;

	if (arg[0] == '\0')
		return -1;

	port_mask = strtoul(arg, &end, 16);
	if ((end == NULL) || (*end != '\0'))
		return -2;

	if (port_mask == 0)
		return -3;

	app.n_ports = 0;
	for (i = 0; i < 64; i++) {
		if ((port_mask & (1LLU << i)) == 0)
			continue;

		if (app.n_ports >= APP_MAX_PORTS)
			return -4;

		app.ports[app.n_ports] = i;
		app.n_ports++;
	}

	return 0;
}

static int
parse_next_hop_eth_addr(char* cfg_file)
{
	FILE *fp;
	char buff[20];
	char *tok;
	struct ether_addr *eth_addr;
	uint8_t num;
	int i, j;

	fp = fopen(cfg_file, "r");
	if (!fp) {
		return -1;
	}

	i = 0;
	while (fgets(buff, 20, fp)) {
		/* a1:b1:c1:d1:e1:f1*/
		eth_addr = &app.next_hop_eth_addr[i];
		tok = strtok(buff, ":");
		j = 0;
		do {
			num = (uint8_t)strtol(tok, NULL, 16); // number base 16
			eth_addr->addr_bytes[j] = num;
			tok = strtok(NULL, ":");
			j++;
		} while (tok);
		i++;
	}

	fclose(fp);
	return 0;
}

static int
parse_routing_table(char* cfg_file)
{
	FILE *fp;
	char buff[20];
	char *tok;
	int subnet;
	int i; // port

	fp = fopen(cfg_file, "r");
	if (!fp) {
		return -1;
	}

	memset(app.routing_table, 0, sizeof(app.routing_table));

	i = 0;
	while (fgets(buff, 20, fp)) {
		tok = strtok(buff, ",");
		do {
			subnet = (int)atoi(tok);
			app.routing_table[subnet] = i;
			tok = strtok(NULL, ",");
			RTE_LOG(ERR, USER1, "Route: subnet %d -> port %d\n", subnet, i);
		} while (tok);
		i++;
	}

	fclose(fp);
	return 0;
}

int
app_parse_args(int argc, char **argv)
{
	int opt, ret;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];
	static struct option lgopts[] = {
		{NULL, 0, 0, 0}
	};
	uint16_t lcore_id;

	/* EAL args */
	app.n_lcores = 0;
	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;
		app.lcores[app.n_lcores] = lcore_id;
		app.n_lcores++;
		if (app.n_lcores == APP_MAX_PORTS) /* No more than num ports */
			break;
	}

	/* Non-EAL args */
	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, "p:n:r:",
			lgopts, &option_index)) != EOF) {
		switch (opt) {
		case 'p':
			if (app_parse_port_mask(optarg) < 0) {
				app_print_usage();
				return -1;
			}
			break;

		/* next hop ethernet address */
		case 'n':
			if (parse_next_hop_eth_addr(optarg) < 0) {
				app_print_usage();
				return -1;
			}
			break;

		/* routing table */
		case 'r':
			if (parse_routing_table(optarg) < 0) {
				app_print_usage();
				return -1;
			}
			break;

		case 0: /* long options */
			app_print_usage();
			return -1;

		default:
			return -1;
		}
	}
	
	if (app.n_lcores != app.n_ports) {
		RTE_LOG(ERR, USER1, "The number of cores must be equal to the number of ports\n");
		app_print_usage();
		return -1;
	}

	if (optind >= 0)
		argv[optind - 1] = prgname;

	ret = optind - 1;
	optind = 1; /* reset getopt lib */
	return ret;
}
