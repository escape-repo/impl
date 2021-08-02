#ifndef _TX_H_
#define _TX_H_

static void
update_mac(struct rte_mbuf *mbuf,
		uint16_t port)
{
        struct ether_hdr *eth;

        eth = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);

        /* s_addr = MAC of the dest port */
        ether_addr_copy(&app.port_eth_addr[port], &eth->s_addr);

        /* d_addr = MAC of the next hop */
        ether_addr_copy(&app.next_hop_eth_addr[port], &eth->d_addr);
}

static void
eth_tx_pkt(uint16_t port, struct rte_mbuf *mbuf)
{
	int16_t ret;

	update_mac(mbuf, port);

	//print_iperf_seq(port, mbuf);

	do {
		ret = rte_eth_tx_burst(app.ports[port], 0, &mbuf, 1);
	} while (ret == 0);
}

#endif /* _TX_H_ */
