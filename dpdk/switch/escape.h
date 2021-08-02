#ifndef _ESCAPE_H_
#define _ESCAPE_H_

#define ETHER_TYPE_FLOW_CONTROL	0x8800
#define ECF_OP_CODE 		0x0102
#define ECF_NACK_OP_CODE 	0x0103

struct ecf_hdr {
        uint16_t opcode;
        uint32_t flow_id;
} __attribute__((__packed__));

static uint16_t
check_eg_port(const void *obj, const void *prt)
{
	const struct rte_mbuf *pkt = (const struct rte_mbuf *)obj;
	const uint16_t *port = (const uint16_t *)prt;
	int dst_port;

	if (!pkt || !port)
		return 0;

	dst_port = get_dest_port(pkt);
	if (dst_port == -1)
		return 0;

	return *port == dst_port ? 1 : 0;
}

static uint16_t
check_flow_id(const void *obj, const void *fid)
{
	const struct rte_mbuf *pkt = (const struct rte_mbuf *)obj;
	const uint32_t *flow_id = (const uint32_t *)fid;

	if (!pkt || !flow_id)
		return 0;

	return get_flow_id(pkt) == *flow_id ? 1 : 0;
}

static void
port_lock_grab(struct port_lock *lock)
{
	int ret;
	do {
		ret = rte_atomic16_test_and_set(&lock->mutex);
	} while (ret == 0);
}

static void
port_lock_release(struct port_lock *lock)
{
	rte_atomic16_clear(&lock->mutex);
}

static uint16_t
port_locked(struct port_lock *lock)
{
	uint16_t locked;
	port_lock_grab(lock);
	{
		locked = lock->locked;
	}
	port_lock_release(lock);
	return locked;
}

static uint16_t
lock_iport(struct port_lock *lock, uint32_t flow_id, uint16_t ext_req)
{
	uint16_t success = 0;
	port_lock_grab(lock);
	{
		if (!lock->locked) {
			lock->locked = 1;
			lock->flow_id = flow_id;
			lock->ext_req = ext_req;
			success = 1;
		}
	}
	port_lock_release(lock);
	return success;
}

static uint32_t
accept_ext_req(struct port_lock *lock)
{
	uint32_t flow_id = 0;
	port_lock_grab(lock);
	{
		if (lock->locked && lock->ext_req) {
			flow_id = lock->flow_id;
			lock->ext_req = 0;
		}
	}
	port_lock_release(lock);
	return flow_id;

}

static void
unlock_iport(struct port_lock *lock, uint32_t flow_id)
{
	port_lock_grab(lock);
	{
		if (lock->locked && lock->flow_id == flow_id) {
			lock->locked = 0;
			lock->flow_id = 0;
		}
	}
	port_lock_release(lock);
}

static uint16_t
lock_eport(struct port_lock *lock, uint32_t flow_id)
{
	uint16_t success = 0;
	port_lock_grab(lock);
	{
		if (!lock->locked) {
			lock->locked = 1;
			lock->flow_id = flow_id;
			success = 1;
		}
	}
	port_lock_release(lock);
	return success;
}

static void
unlock_eport(struct port_lock *lock, uint32_t flow_id)
{
	port_lock_grab(lock);
	{
		if (lock->locked && lock->flow_id == flow_id) {
			lock->locked = 0;
			lock->flow_id = 0;
		}
	}
	port_lock_release(lock);
}

static void
send_ecf(uint32_t port, uint16_t opcode, uint32_t flow_id)
{
	struct rte_mbuf *mbuf;
        struct ether_hdr *eth_hdr;
        struct ecf_hdr *ecf_hdr;

        mbuf = rte_pktmbuf_alloc(app.pool);
        if (unlikely(mbuf == NULL))
                return;

        mbuf->pkt_len = mbuf->data_len =
                sizeof(struct ether_hdr) + sizeof(struct ecf_hdr);

        eth_hdr = rte_pktmbuf_mtod(mbuf, struct ether_hdr *);
        ecf_hdr = (struct ecf_hdr *)&eth_hdr[1];

        eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_FLOW_CONTROL);
        ether_addr_copy(&app.port_eth_addr[port], &eth_hdr->s_addr);
        ether_addr_copy(&app.next_hop_eth_addr[port], &eth_hdr->d_addr);

        ecf_hdr->opcode = rte_cpu_to_be_16(opcode);
        ecf_hdr->flow_id = rte_cpu_to_be_32(flow_id);

        rte_eth_tx_burst(port, 0, &mbuf, 1);
}

static struct rte_mbuf *
jump_rx_msg(uint16_t port, uint32_t flow_id)
{
	uint16_t i;
	struct rte_mbuf *mbuf;

	mbuf = NULL;

	/* Check routing interim buffer */
	for (i = 0; i < app.mbuf_wk[port].n_mbufs; i++) {
		struct rte_mbuf *mb = app.mbuf_wk[port].array[i];
		if (get_flow_id(mb) == flow_id) {
			mbuf = mb;
			rte_memcpy((void *)&app.mbuf_wk[port].array[i],
					(const void *)&app.mbuf_wk[port].array[i + 1],
					sizeof(struct rte_mbuf *) * (app.mbuf_wk[port].n_mbufs - i - 1));
			app.mbuf_wk[port].n_mbufs--;
			break;
		}
	}

	/* Check RX ring */
	if (!mbuf)
		mbuf = rte_ring_sc_jump(app.rings_rx[port], check_flow_id, &flow_id);

	return mbuf;
}

static void
handle_tunnel_ext_req(uint16_t port,
		struct port_state *port_state)
{
	uint32_t flow_id;
	struct rte_mbuf *mbuf;

	flow_id = accept_ext_req(&port_state->lock_in);
	if (!flow_id)
		return;

	mbuf = jump_rx_msg(port, flow_id);
	if (mbuf) {
		uint64_t pkt_len;
		struct port_state *eport_state;

		/*RTE_LOG(INFO, USER1, "Port %u: (follower) pkt jumped ingress queue for flow %u\n",
				port, flow_id);*/
		pkt_len = mbuf->pkt_len;
		eport_state = &app.port_state[get_dest_port(mbuf)];
		forward_msg(port, mbuf, 1, 1);
		port_state->qlen_in -= pkt_len;
		unlock_iport(&port_state->lock_in, flow_id);
		unlock_eport(&eport_state->lock_out, flow_id);
	} else {
		/* Extend to upstream node (follower) */
		/*RTE_LOG(INFO, USER1, "Port %u: (follower) pkt NOT found in ingress queue for flow %u (extend)\n",
                                              port, flow_id);*/
		send_ecf(port, ECF_OP_CODE, flow_id);
	}
}

static uint16_t
handle_esc_pkt(uint16_t port,
		struct port_state *port_state,
		struct rte_mbuf **array,
                uint16_t n_mbufs)
{
        uint16_t index;
	uint32_t flow_id;
        struct rte_mbuf *mbuf;
	struct port_state *eport_state;
	struct port_lock *lock_in;

	lock_in = &port_state->lock_in;

	if (n_mbufs && port_locked(lock_in) && !lock_in->ext_req) {
		for (index = 0; index < n_mbufs; index++) {
                        mbuf = array[index];
			flow_id = get_flow_id(mbuf);
			if (flow_id == lock_in->flow_id) {
				/*RTE_LOG(INFO, USER1, "Port %u: (follower/leader) escaping pkt rcvd (flow = %u)\n",
						port, flow_id);*/
				eport_state = &app.port_state[get_dest_port(mbuf)];
				forward_msg(port, mbuf, 1, 1);
				unlock_iport(lock_in, flow_id);
				unlock_eport(&eport_state->lock_out, flow_id);
				rte_memcpy((void *)&array[index], (const void *)&array[index + 1],
						sizeof(struct rte_mbuf *) * (n_mbufs - index - 1));
				n_mbufs--;
				break;
			}
		}
        }

        return n_mbufs;
}

static void
handle_ecf(uint16_t port,
		struct port_state *port_state,
		uint32_t flow_id)
{
	struct rte_mbuf *mbuf;

	if (!rte_atomic16_read(&port_state->pfc_enforced))
                rte_panic("Port %u: received tunnel extension request (flow = %u) when PFC not enforced!\n",
                                port, flow_id);
	
	if (lock_eport(&port_state->lock_out, flow_id)) {
		/*RTE_LOG(INFO, USER1, "Port %u: (follower) accepted tunnel extension request (flow = %u)\n",
				port, flow_id);*/
		mbuf = rte_ring_sc_jump(app.rings_tx[port], check_flow_id, &flow_id);
		if (mbuf) {
			/*RTE_LOG(INFO, USER1, "Port %u: (follower) pkt jumped egress queue for flow %u\n",
					port, flow_id);*/
			eth_tx_pkt(port, mbuf);
			unlock_eport(&port_state->lock_out, flow_id);
		} else {
			/* Extend to ingress queue */
			uint16_t iport = get_flow_iport(flow_id);
			struct port_state *iport_state = &app.port_state[iport];
			if (rte_atomic16_read(&iport_state->pfc_requested)) {
				if (!lock_iport(&iport_state->lock_in, flow_id, 1)) {
					/*RTE_LOG(INFO, USER1, "Port %u: (follower) Failed to place tunnel extension request "
							"on port %u for flow %u\n",
							port, iport, flow_id);*/
					send_ecf(port, ECF_NACK_OP_CODE, flow_id);
					unlock_eport(&port_state->lock_out, flow_id);
				}
			} else {
				/*RTE_LOG(INFO, USER1, "Port %u: (follower) ingress port %u either did not request pause or "
						"absorb delay for flow %u\n",
						port, iport, flow_id);*/
				send_ecf(port, ECF_NACK_OP_CODE, flow_id);
				unlock_eport(&port_state->lock_out, flow_id);
			}
		}
	} else {
                /*RTE_LOG(INFO, USER1, "Port %u: (follower) failed to lock eport  (flow = %u)\n",
                              port, flow_id);*/
		send_ecf(port, ECF_NACK_OP_CODE, flow_id);
        }
}

static void
handle_nack(uint16_t port,
		struct port_state *port_state,
		uint32_t flow_id)
{
	uint16_t eport;
	struct port_state *eport_state;

	if (!port_locked(&port_state->lock_in)) /* TODO: or extension pending */
		rte_panic("Port %u received a NACK for flow %u when the port is not locked!",
				port, flow_id);
	
	unlock_iport(&port_state->lock_in, flow_id);
	/*RTE_LOG(INFO, USER1, "Port %u (ingress) unlocked for flow %u\n", port, flow_id);*/

	eport = get_flow_eport(flow_id);
	eport_state = &app.port_state[eport];

	if (!port_locked(&eport_state->lock_out))
                rte_panic("Port %u received a NACK for flow %u when the eport %u is not locked!",
				port, flow_id, eport);
	
	send_ecf(eport, ECF_NACK_OP_CODE, flow_id);
	unlock_eport(&eport_state->lock_out, flow_id);
	/*RTE_LOG(INFO, USER1, "Port %u (egress) unlocked for flow %u\n", eport, flow_id);*/
}

static uint16_t
consume_ecf_frame(uint16_t port,
		struct port_state *port_state,
		struct ether_hdr *eth_hdr)
{
        struct ecf_hdr *ecf_hdr;
	
	ecf_hdr = (struct ecf_hdr *)&eth_hdr[1];
	switch (rte_be_to_cpu_16(ecf_hdr->opcode)) {
		case ECF_OP_CODE: /* Extend channel */
			handle_ecf(port, port_state,
                                rte_be_to_cpu_32(ecf_hdr->flow_id));
			return 1;
		case ECF_NACK_OP_CODE: /* Abort channel */
			handle_nack(port, port_state,
                                rte_be_to_cpu_32(ecf_hdr->flow_id));
			return 1;
		default:
			return 0;
	}
}

static void
create_esc_tunnels(uint16_t port)
{
        uint16_t eport, idx;
        struct port_state *port_state, *eport_state, *eport_state2;
        struct flow *flow;
	struct rte_mbuf *mbuf;

	port_state = &app.port_state[port];

	if (port_locked(&port_state->lock_in)) /* iport already on a tunnel */
		return;
	if (!rte_atomic16_read(&port_state->pfc_requested))
		return;

        for (eport = 0; eport < app.n_ports; eport++) {
                eport_state = &app.port_state[eport];
                if (!rte_atomic16_read(&eport_state->pfc_enforced))
                        continue;
		if (unlikely(eport == port))
			continue;
		/* Check for HoL blocking */
		if (!rte_ring_sc_find(app.rings_rx[port], check_eg_port, &eport))
			continue;
		/* Look for flows that can escape */
		for (idx = 0; idx < FLOW_TABLE_SIZE; idx++) {
			flow = &app.flow_tables[port][idx];
			if (flow->id == 0)
				continue;
			if (flow->out_port == eport)
				continue;
			eport_state2 = &app.port_state[flow->out_port];
			if (rte_atomic16_read(&eport_state2->pfc_enforced))
				continue;
			if (!lock_eport(&eport_state2->lock_out, flow->id)) /* eport already on a tunnel */
				continue;
			if (!lock_iport(&port_state->lock_in, flow->id, 0)) { /* iport already on a tunnel */
				unlock_eport(&eport_state2->lock_out, flow->id);
				continue;
			}
			/*RTE_LOG(INFO, USER1, "Port %u: (leader) creating tunnel (flow = %u, eport = %u)\n",
					port, flow->id, flow->out_port);*/
			mbuf = jump_rx_msg(port, flow->id);
			if (mbuf) {
				uint64_t pkt_len;
				/* Leader */
				/*RTE_LOG(INFO, USER1, "Port %u: (leader) pkt jumped for flow %u\n",
						port, flow->id);*/
				pkt_len = mbuf->pkt_len;
				forward_msg(port, mbuf, 0, 1);
				port_state->qlen_in -= pkt_len;
				unlock_iport(&port_state->lock_in, flow->id);
				unlock_eport(&eport_state2->lock_out, flow->id);
			} else {
				/* Extend to upstream node (follower) */
				/*RTE_LOG(INFO, USER1, "Port %u: (leader) pkt NOT found for flow %u (extend)\n",
						port, flow->id);*/
				send_ecf(port, ECF_OP_CODE, flow->id);
			}
			//flow->out_port = 0; /* TODO: Properly invalidate the flow entry */
			return; /* Deal with one flow at a time */
		}
        }
}

static const uint16_t ENABLE_ESCAPE = 1;
static const uint16_t ESCAPE_T = 50; /* us */

static void
app_process_esc_timer(uint16_t port, uint64_t *prev_tsc,
		const uint64_t esc_tsc)
{
        uint64_t cur_tsc, diff_tsc;

        if (!ENABLE_ESCAPE)
                return;

        cur_tsc = rte_rdtsc();
        diff_tsc = cur_tsc - *prev_tsc;

        if (unlikely(esc_tsc <= diff_tsc)) {
		/* Escape tunnels */
                create_esc_tunnels(port);
                *prev_tsc = cur_tsc;
        }
}

#endif /* _ESCAPE_H_ */
