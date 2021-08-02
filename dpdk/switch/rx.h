#ifndef _RX_H_
#define _RX_H_

#define ETHER_TYPE_FLOW_CONTROL 0x8800
#define PFC_OP_CODE 0x0101

struct pause_hdr {
        uint16_t opcode;
        uint16_t xoff;
} __attribute__((__packed__));

static void
send_pause_frame(uint16_t port,
		uint16_t xoff)
{
	struct rte_mbuf *mbuf;
        struct ether_hdr *hdr;
        struct pause_hdr *pause_hdr;

        mbuf = rte_pktmbuf_alloc(app.pool);
        if (unlikely(mbuf == NULL))
                return;

        mbuf->pkt_len = mbuf->data_len = 
		sizeof(struct ether_hdr) + sizeof(struct pause_hdr);

        hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
        pause_hdr = (struct pause_hdr *)&hdr[1];

        hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_FLOW_CONTROL);
        ether_addr_copy(&app.port_eth_addr[port], &hdr->s_addr);
        ether_addr_copy(&app.next_hop_eth_addr[port], &hdr->d_addr);

        pause_hdr->opcode = rte_cpu_to_be_16(PFC_OP_CODE);
        pause_hdr->xoff = rte_cpu_to_be_16(xoff);

        rte_eth_tx_burst(port, 0, &mbuf, 1);
}

static uint16_t
consume_pause_frame(struct port_state *port_state,
		struct ether_hdr *eth_hdr)
{
	const uint64_t SEC_TSC = rte_get_tsc_hz();

	struct pause_hdr *pause_hdr;
	uint64_t cur_tsc;
	double cur_time;
	
	pause_hdr = (struct pause_hdr *)&eth_hdr[1];
	if (rte_be_to_cpu_16(pause_hdr->opcode) == PFC_OP_CODE) {
		rte_atomic16_set(&port_state->pfc_enforced,
				rte_be_to_cpu_16(pause_hdr->xoff));
		if (port_state->pause.enabled) {
			cur_tsc = rte_rdtsc();
			cur_time = (cur_tsc - port_state->start_time) / (double)SEC_TSC;
			fprintf(port_state->pause.data, "%f %d\n",
					cur_time, rte_be_to_cpu_16(pause_hdr->xoff));
		}
		return 1;
	}
	
	return 0;
}

#define PFC_XOFF_THR	125000	/* Bytes */
#define PFC_XON_THR	10000	/* Bytes */

static uint16_t
absorb_pfc_delay(uint16_t port, struct port_state *port_state)
{
	uint16_t n_read, n_total;
	struct rte_mbuf *mbuf;
	
	n_total = 0;
	do {
		n_read = rte_eth_rx_burst(app.ports[port], 0, &mbuf, 1);
		if (n_read) {
			rte_ring_sp_enqueue_burst(app.rings_rx[port],
					(void **)&mbuf, n_read, NULL);
			port_state->qlen_in += mbuf->pkt_len;
		}
		n_total += n_read;
	} while (n_read);

	return n_total;
}

static void
handle_pfc_xoff(uint16_t port, struct port_state *port_state)
{
	if (!port_state->pfc_enabled)
		return;

	if (!rte_atomic16_read(&port_state->pfc_requested) &&
			PFC_XOFF_THR <= port_state->qlen_in) {
		send_pause_frame(port, 1);
		rte_atomic16_set(&port_state->pfc_requested, 1);
		absorb_pfc_delay(port, port_state);
		RTE_LOG(INFO, USER1, "Port %u requested pause (qlen = %f)\n",
				port, port_state->qlen_in / 1000.0);
	}
}

static void
handle_pfc_xon(uint16_t port, struct port_state *port_state)
{
	if (!port_state->pfc_enabled)
		return;

	if (rte_atomic16_read(&port_state->pfc_requested) &&
			port_state->qlen_in < PFC_XON_THR) {
		send_pause_frame(port, 0);
		rte_atomic16_set(&port_state->pfc_requested, 0);
		RTE_LOG(INFO, USER1, "Port %u requested unpause (qlen = %f)\n",
				port, port_state->qlen_in / 1000.0);
	}
}

static uint16_t
no_data_traffic(struct rte_mbuf **array, uint16_t n_mbufs)__attribute__((unused));
static uint16_t
no_data_traffic(struct rte_mbuf **array, uint16_t n_mbufs)
{
	uint16_t i, no_data;
	struct rte_mbuf *mbuf;
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;

	no_data = 1;
	
	for (i = 0; i < n_mbufs; i++) {
		mbuf = array[i];
		if (!RTE_ETH_IS_IPV4_HDR(mbuf->packet_type))
			continue;
		eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
		ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
		if (ipv4_hdr->next_proto_id != IPPROTO_UDP)
			continue;
		no_data = 0;
		break;
	}

	return no_data;
}

#endif /* _RX_H_ */
