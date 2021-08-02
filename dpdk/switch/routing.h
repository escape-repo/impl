#ifndef _ROUTING_H_
#define _ROUTING_H_

static int
get_dest_port(const struct rte_mbuf *mbuf)
{
        int dst_port = -1;

        if (RTE_ETH_IS_IPV4_HDR(mbuf->packet_type)) {
                struct ipv4_hdr *ipv4_hdr;
                struct ether_hdr *eth_hdr;
                uint8_t subnet;

                eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
                ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);

                subnet = (uint8_t)(rte_be_to_cpu_32(ipv4_hdr->dst_addr) >> 8);
                if (APP_MAX_SUBNETS <= subnet)
                        rte_panic("Invalid subnet: %u\n", subnet);

                dst_port = app.routing_table[subnet];
        }

        return dst_port;
}

static uint16_t
forward_msg(uint16_t port, struct rte_mbuf *mbuf, uint16_t esc, uint16_t retry)
{
	int ret;
	int dst_port;
	uint16_t status, protocol;
	uint32_t flow_id;
	uint32_t src_addr, dst_addr;
	struct rte_ring *ring;

	status = 0;

	dst_port = get_dest_port(mbuf);
	if (dst_port == -1) {
		rte_pktmbuf_free(mbuf);
		status = 1;
	} else {
		flow_id = get_flow_id(mbuf);
		src_addr = get_src_addr(mbuf);
		dst_addr = get_dst_addr(mbuf);
		protocol = get_protocol(mbuf);
		
		if (esc /*&& rte_atomic16_read(&app.port_state[dst_port].pfc_enforced)*/)
			ring = app.rings_tx_esc[dst_port];
		else
			ring = app.rings_tx[dst_port];

		do {
			ret = rte_ring_mp_enqueue(ring, (void *)mbuf);
		} while ((ret == -ENOBUFS) && retry);
		
		if (ret == 0) { /* Enqueued */
			if (update_flow_table(flow_id, port, dst_port)) { /* New entry */
				print_pkt(port, flow_id, port, dst_port,
						protocol, src_addr, dst_addr);
			}
			status = 1;
		}
	}

	return status;
}

#endif /* _ROUTING_H_ */
