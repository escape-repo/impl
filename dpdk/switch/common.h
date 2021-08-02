#ifndef _COMMON_H_ 
#define _COMMON_H_

static void
update_throughput(struct tp_meter *tp_meter,
                struct rte_mbuf **array,
		uint16_t n_mbufs,
		const uint64_t start_time,
                const uint64_t sec_tsc)
{
        uint64_t cur_tsc;
        double interval, cur_time;
        uint16_t i;

        if (!tp_meter->enabled)
                return;

        cur_tsc = rte_rdtsc();
        interval = (cur_tsc - tp_meter->intvl_start_time) / (double)sec_tsc;

        for (i = 0; i < n_mbufs; i++) {
                if (100 <= tp_meter->intvl_num_pkts ||
                                sec_tsc <= cur_tsc - tp_meter->intvl_start_time) {
                        cur_time = (cur_tsc - start_time) / (double)sec_tsc;
                        fprintf(tp_meter->data, "%f %f\n", cur_time,
                                        (tp_meter->intvl_num_bits / interval) / 1e6);
                        tp_meter->intvl_start_time = cur_tsc;
                        tp_meter->intvl_num_bits = 0;
                        tp_meter->intvl_num_pkts = 0;
                }
                tp_meter->intvl_num_bits += array[i]->pkt_len * 8;
                tp_meter->intvl_num_pkts++;
        }
}

static uint32_t
get_flow_id(const struct rte_mbuf *mbuf)
{
        uint32_t id;
        struct ether_hdr *eth_hdr;
        struct ipv4_hdr *ipv4_hdr;
        struct udp_hdr *udp_hdr;

        id = 0;
        if (!RTE_ETH_IS_IPV4_HDR(mbuf->packet_type))
                return id;
        eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
        ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
        id += rte_be_to_cpu_32(ipv4_hdr->src_addr);
        id += rte_be_to_cpu_32(ipv4_hdr->dst_addr);
        if (ipv4_hdr->next_proto_id != IPPROTO_UDP)
                return id;
        id += IPPROTO_UDP;
        udp_hdr = (struct udp_hdr *)(ipv4_hdr + 1);
        id += rte_be_to_cpu_16(udp_hdr->src_port);
        id += rte_be_to_cpu_16(udp_hdr->dst_port);

        return id;
}

static uint16_t
update_flow_table(uint32_t flow_id,
		uint16_t iport, uint16_t eport)
{
        uint16_t i, ret;
        struct flow *flow;

	ret = 0;

	for (i = 0; i < FLOW_TABLE_SIZE; i++) {
		flow = &app.flow_tables[iport][i];
		if (flow->id == flow_id) /* Flow already exists */
			break;
		if (flow->id != 0) /* Find next empty slot */
			continue;
		flow->id = flow_id;
		flow->in_port = iport;
		flow->out_port = eport;
		ret = 1;
		break;
        }

	return ret;
}

static uint16_t
get_flow_iport(uint32_t flow_id)
{
	uint16_t i, j;

	for (i = 0; i < APP_MAX_PORTS; i++) {
		for (j = 0; j < FLOW_TABLE_SIZE; j++) {
			if (app.flow_tables[i][j].id == flow_id)
				return app.flow_tables[i][j].in_port;
		}
	}

	rte_panic("Flow %u not found in table\n", flow_id);
}

static uint16_t
get_flow_eport(uint32_t flow_id)
{
	uint16_t i, j;

	for (i = 0; i < APP_MAX_PORTS; i++) {
		for (j = 0; j < FLOW_TABLE_SIZE; j++) {
			if (app.flow_tables[i][j].id == flow_id)
				return app.flow_tables[i][j].out_port;
		}
	}

	rte_panic("Flow %u not found in table\n", flow_id);
}

static uint64_t
get_data_size(struct rte_mbuf **array, const uint16_t n_mbufs)
{
	uint16_t i;
	uint64_t size;
	
	size = 0;
	for (i = 0; i < n_mbufs; i++)
		size += array[i]->pkt_len; // bytes
	
	return size;
}

static uint32_t
get_src_addr(const struct rte_mbuf *mbuf)
{
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;
	uint32_t src_addr;

	src_addr = 0;

	if (RTE_ETH_IS_IPV4_HDR(mbuf->packet_type)) {
                eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
                ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
		src_addr = rte_be_to_cpu_32(ipv4_hdr->src_addr);
	}

	return src_addr;
}

static uint32_t
get_dst_addr(const struct rte_mbuf *mbuf)
{
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;
	uint32_t dst_addr;

	dst_addr = 0;

	if (RTE_ETH_IS_IPV4_HDR(mbuf->packet_type)) {
                eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
                ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
		dst_addr = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
	}

	return dst_addr;
}

static uint16_t
get_protocol(const struct rte_mbuf *mbuf)
{
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;
	uint16_t protocol;

	protocol = 0;

	if (RTE_ETH_IS_IPV4_HDR(mbuf->packet_type)) {
                eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
                ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
		protocol = ipv4_hdr->next_proto_id;
	}

	return protocol;
}

static void
print_pkt(uint16_t port, uint32_t flow_id, uint16_t iport, uint16_t eport,
		uint16_t protocol, uint32_t src_addr,
		uint32_t dst_addr)
{
	RTE_LOG(INFO, USER1, "Port %u flow:%u, iport:%u, eport:%u,"
			" protocol:%u, src:%u.%u.%u.%u, dst:%u.%u.%u.%u\n",
			port, flow_id, iport, eport, protocol,
			(uint8_t)(src_addr >> 24),
			(uint8_t)(src_addr >> 16),
			(uint8_t)(src_addr >> 8),
			(uint8_t)src_addr,
			(uint8_t)(dst_addr >> 24),
			(uint8_t)(dst_addr >> 16),
			(uint8_t)(dst_addr >> 8),
			(uint8_t)dst_addr);
}

static void
print_iperf_seq(uint16_t port, const struct rte_mbuf *mbuf) __attribute__((unused));
static void
print_iperf_seq(uint16_t port, const struct rte_mbuf *mbuf)
{
        struct ether_hdr *eth_hdr;
        struct ipv4_hdr *ipv4_hdr;
        struct udp_hdr *udp_hdr;
        uint32_t seq_no;

        if (!RTE_ETH_IS_IPV4_HDR(mbuf->packet_type))
                return;
        eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
        ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
        if (ipv4_hdr->next_proto_id != IPPROTO_UDP)
                return;
        udp_hdr = (struct udp_hdr *)(ipv4_hdr + 1);
        seq_no = rte_be_to_cpu_32(*(uint32_t *)(udp_hdr + 1));

        RTE_LOG(INFO, USER1, "Port %u: flow:%u seq:%u\n",
                        port, get_flow_id(mbuf), seq_no);
}

#endif /* _COMMON_H_ */
