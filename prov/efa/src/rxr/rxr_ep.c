/*
 * Copyright (c) 2019-2020 Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "ofi.h"
#include <ofi_util.h>
#include <ofi_iov.h>

#include "rxr.h"
#include "efa.h"
#include "rxr_msg.h"
#include "rxr_rma.h"

#define RXR_PKT_DUMP_DATA_LEN 64

#if ENABLE_DEBUG
static void rxr_ep_print_rts_pkt(struct rxr_ep *ep,
				 char *prefix, struct rxr_rts_hdr *rts_hdr)
{
	char str[RXR_PKT_DUMP_DATA_LEN * 4];
	size_t str_len = RXR_PKT_DUMP_DATA_LEN * 4, l;
	uint8_t *src;
	uint8_t *data;
	int i;

	str[str_len - 1] = '\0';

	FI_DBG(&rxr_prov, FI_LOG_EP_DATA,
	       "%s RxR RTS packet - version: %"	PRIu8
	       " flags: %"	PRIu16
	       " tx_id: %"	PRIu32
	       " msg_id: %"	PRIu32
	       " tag: %lx data_len: %"	PRIu64 "\n",
	       prefix, rts_hdr->version, rts_hdr->flags, rts_hdr->tx_id,
	       rts_hdr->msg_id, rts_hdr->tag, rts_hdr->data_len);

	if ((rts_hdr->flags & RXR_REMOTE_CQ_DATA) &&
	    (rts_hdr->flags & RXR_REMOTE_SRC_ADDR)) {
		src = (uint8_t *)((struct rxr_ctrl_cq_pkt *)rts_hdr)->data;
		data = src + rts_hdr->addrlen;
	} else if (!(rts_hdr->flags & RXR_REMOTE_CQ_DATA) &&
		   (rts_hdr->flags & RXR_REMOTE_SRC_ADDR)) {
		src = (uint8_t *)((struct rxr_ctrl_pkt *)rts_hdr)->data;
		data = src + rts_hdr->addrlen;
	} else if ((rts_hdr->flags & RXR_REMOTE_CQ_DATA) &&
		   !(rts_hdr->flags & RXR_REMOTE_SRC_ADDR)) {
		data = (uint8_t *)((struct rxr_ctrl_cq_pkt *)rts_hdr)->data;
	} else {
		data = (uint8_t *)((struct rxr_ctrl_pkt *)rts_hdr)->data;
	}

	if (rts_hdr->flags & RXR_REMOTE_CQ_DATA)
		FI_DBG(&rxr_prov, FI_LOG_EP_DATA,
		       "\tcq_data: %08lx\n",
		       ((struct rxr_ctrl_cq_hdr *)rts_hdr)->cq_data);

	if (rts_hdr->flags & RXR_REMOTE_SRC_ADDR) {
		l = snprintf(str, str_len, "\tsrc_addr: ");
		for (i = 0; i < rts_hdr->addrlen; i++)
			l += snprintf(str + l, str_len - l, "%02x ", src[i]);
		FI_DBG(&rxr_prov, FI_LOG_EP_DATA, "%s\n", str);
	}

	l = snprintf(str, str_len, ("\tdata:    "));
	for (i = 0; i < MIN(rxr_get_rts_data_size(ep, rts_hdr),
			    RXR_PKT_DUMP_DATA_LEN); i++)
		l += snprintf(str + l, str_len - l, "%02x ", data[i]);
	FI_DBG(&rxr_prov, FI_LOG_EP_DATA, "%s\n", str);
}

static void rxr_ep_print_connack_pkt(char *prefix,
				     struct rxr_connack_hdr *connack_hdr)
{
	FI_DBG(&rxr_prov, FI_LOG_EP_DATA,
	       "%s RxR CONNACK packet - version: %" PRIu8
	       " flags: %x\n", prefix, connack_hdr->version,
	       connack_hdr->flags);
}

static void rxr_ep_print_cts_pkt(char *prefix, struct rxr_cts_hdr *cts_hdr)
{
	FI_DBG(&rxr_prov, FI_LOG_EP_DATA,
	       "%s RxR CTS packet - version: %"	PRIu8
	       " flags: %x tx_id: %" PRIu32
	       " rx_id: %"	   PRIu32
	       " window: %"	   PRIu64
	       "\n", prefix, cts_hdr->version, cts_hdr->flags,
	       cts_hdr->tx_id, cts_hdr->rx_id, cts_hdr->window);
}

static void rxr_ep_print_data_pkt(char *prefix, struct rxr_data_pkt *data_pkt)
{
	char str[RXR_PKT_DUMP_DATA_LEN * 4];
	size_t str_len = RXR_PKT_DUMP_DATA_LEN * 4, l;
	int i;

	str[str_len - 1] = '\0';

	FI_DBG(&rxr_prov, FI_LOG_EP_DATA,
	       "%s RxR DATA packet -  version: %" PRIu8
	       " flags: %x rx_id: %" PRIu32
	       " seg_size: %"	     PRIu64
	       " seg_offset: %"	     PRIu64
	       "\n", prefix, data_pkt->hdr.version, data_pkt->hdr.flags,
	       data_pkt->hdr.rx_id, data_pkt->hdr.seg_size,
	       data_pkt->hdr.seg_offset);

	l = snprintf(str, str_len, ("\tdata:    "));
	for (i = 0; i < MIN(data_pkt->hdr.seg_size, RXR_PKT_DUMP_DATA_LEN);
	     i++)
		l += snprintf(str + l, str_len - l, "%02x ",
			      ((uint8_t *)data_pkt->data)[i]);
	FI_DBG(&rxr_prov, FI_LOG_EP_DATA, "%s\n", str);
}

void rxr_ep_print_pkt(char *prefix, struct rxr_ep *ep, struct rxr_base_hdr *hdr)
{
	switch (hdr->type) {
	case RXR_RTS_PKT:
		rxr_ep_print_rts_pkt(ep, prefix, (struct rxr_rts_hdr *)hdr);
		break;
	case RXR_CONNACK_PKT:
		rxr_ep_print_connack_pkt(prefix, (struct rxr_connack_hdr *)hdr);
		break;
	case RXR_CTS_PKT:
		rxr_ep_print_cts_pkt(prefix, (struct rxr_cts_hdr *)hdr);
		break;
	case RXR_DATA_PKT:
		rxr_ep_print_data_pkt(prefix, (struct rxr_data_pkt *)hdr);
		break;
	default:
		FI_WARN(&rxr_prov, FI_LOG_CQ, "invalid ctl pkt type %d\n",
			rxr_get_base_hdr(hdr)->type);
		assert(0);
		return;
	}
}
#endif

struct rxr_rx_entry *rxr_ep_rx_entry_init(struct rxr_ep *ep,
					  struct rxr_rx_entry *rx_entry,
					  const struct iovec *iov,
					  size_t iov_count, uint64_t tag,
					  uint64_t ignore, void *context,
					  fi_addr_t addr, uint32_t op,
					  uint64_t flags)
{
	rx_entry->type = RXR_RX_ENTRY;
	rx_entry->rx_id = ofi_buf_index(rx_entry);
	rx_entry->addr = addr;
	rx_entry->fi_flags = flags;
	rx_entry->rxr_flags = 0;
	rx_entry->bytes_done = 0;
	rx_entry->window = 0;
	rx_entry->iov_count = iov_count;
	rx_entry->tag = tag;
	rx_entry->ignore = ignore;
	rx_entry->unexp_rts_pkt = NULL;
	rx_entry->rma_iov_count = 0;
	dlist_init(&rx_entry->queued_pkts);

	memset(&rx_entry->cq_entry, 0, sizeof(rx_entry->cq_entry));

	/* Handle case where we're allocating an unexpected rx_entry */
	if (iov) {
		memcpy(rx_entry->iov, iov, sizeof(*rx_entry->iov) * iov_count);
		rx_entry->cq_entry.len = ofi_total_iov_len(iov, iov_count);
		rx_entry->cq_entry.buf = iov[0].iov_base;
	}

	rx_entry->cq_entry.op_context = context;
	rx_entry->cq_entry.tag = 0;
	rx_entry->ignore = ~0;

	switch (op) {
	case ofi_op_tagged:
		rx_entry->cq_entry.flags = (FI_RECV | FI_MSG | FI_TAGGED);
		rx_entry->cq_entry.tag = tag;
		rx_entry->ignore = ignore;
		break;
	case ofi_op_msg:
		rx_entry->cq_entry.flags = (FI_RECV | FI_MSG);
		break;
	case ofi_op_read_rsp:
		rx_entry->cq_entry.flags = (FI_REMOTE_READ | FI_RMA);
		break;
	case ofi_op_write:
		rx_entry->cq_entry.flags = (FI_REMOTE_WRITE | FI_RMA);
		break;
	default:
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
			"Unknown operation while %s\n", __func__);
		assert(0 && "Unknown operation");
	}

	return rx_entry;
}

struct rxr_rx_entry *rxr_ep_get_rx_entry(struct rxr_ep *ep,
					 const struct iovec *iov,
					 size_t iov_count, uint64_t tag,
					 uint64_t ignore, void *context,
					 fi_addr_t addr, uint32_t op,
					 uint64_t flags)
{
	struct rxr_rx_entry *rx_entry;

	rx_entry = ofi_buf_alloc(ep->rx_entry_pool);
	if (OFI_UNLIKELY(!rx_entry)) {
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "RX entries exhausted\n");
		return NULL;
	}

#if ENABLE_DEBUG
	dlist_insert_tail(&rx_entry->rx_entry_entry, &ep->rx_entry_list);
#endif
	rx_entry = rxr_ep_rx_entry_init(ep, rx_entry, iov, iov_count, tag,
					ignore, context, addr, op, flags);
	rx_entry->state = RXR_RX_INIT;
	return rx_entry;
}

/*
 * Create a new rx_entry for an unexpected message. Store the packet for later
 * processing and put the rx_entry on the appropriate unexpected list.
 */
struct rxr_rx_entry *rxr_ep_get_new_unexp_rx_entry(struct rxr_ep *ep,
						   struct rxr_pkt_entry *pkt_entry)
{
	struct rxr_rx_entry *rx_entry;
	struct rxr_pkt_entry *unexp_entry;
	struct rxr_rts_hdr *rts_pkt;
	uint32_t op;

	if (rxr_env.rx_copy_unexp && pkt_entry->type == RXR_PKT_ENTRY_POSTED) {
		unexp_entry = rxr_get_pkt_entry(ep, ep->rx_unexp_pkt_pool);
		if (OFI_UNLIKELY(!unexp_entry)) {
			FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
				"Unable to allocate rx_pkt_entry for unexp msg\n");
			return NULL;
		}
		rxr_copy_pkt_entry(ep, unexp_entry, pkt_entry,
				   RXR_PKT_ENTRY_UNEXP);
		rxr_release_rx_pkt_entry(ep, pkt_entry);
	} else {
		unexp_entry = pkt_entry;
	}

	rts_pkt = rxr_get_rts_hdr(unexp_entry->pkt);

	if (rts_pkt->flags & RXR_TAGGED)
		op = ofi_op_tagged;
	else
		op = ofi_op_msg;

	rx_entry = rxr_ep_get_rx_entry(ep, NULL, 0, rts_pkt->tag, ~0, NULL,
				       unexp_entry->addr, op, 0);
	if (OFI_UNLIKELY(!rx_entry))
		return NULL;

	rx_entry->state = RXR_RX_UNEXP;
	rx_entry->total_len = rts_pkt->data_len;
	rx_entry->rxr_flags = rts_pkt->flags;
	rx_entry->unexp_rts_pkt = unexp_entry;

	if (op == ofi_op_tagged)
		dlist_insert_tail(&rx_entry->entry, &ep->rx_unexp_tagged_list);
	else
		dlist_insert_tail(&rx_entry->entry, &ep->rx_unexp_list);

	return rx_entry;
}

struct rxr_rx_entry *rxr_ep_split_rx_entry(struct rxr_ep *ep,
					   struct rxr_rx_entry *posted_entry,
					   struct rxr_rx_entry *consumer_entry,
					   struct rxr_pkt_entry *pkt_entry)
{
	struct rxr_rx_entry *rx_entry;
	struct rxr_rts_hdr *rts_pkt = NULL;
	size_t buf_len, consumed_len;

	rts_pkt = rxr_get_rts_hdr(pkt_entry->pkt);
	if (!consumer_entry) {
		rx_entry = rxr_ep_get_rx_entry(ep, posted_entry->iov,
					       posted_entry->iov_count, rts_pkt->tag,
					       0, NULL, pkt_entry->addr, ofi_op_msg,
					       posted_entry->fi_flags);
		if (OFI_UNLIKELY(!rx_entry))
			return NULL;

		FI_DBG(&rxr_prov, FI_LOG_EP_CTRL,
		       "Splitting into new multi_recv consumer rx_entry %d from rx_entry %d\n",
		       rx_entry->rx_id,
		       posted_entry->rx_id);
	} else {
		rx_entry = consumer_entry;
		memcpy(rx_entry->iov, posted_entry->iov,
		       sizeof(*posted_entry->iov) * posted_entry->iov_count);
		rx_entry->iov_count = posted_entry->iov_count;
	}

	buf_len = ofi_total_iov_len(rx_entry->iov,
				    rx_entry->iov_count);
	consumed_len = MIN(buf_len, rts_pkt->data_len);

	rx_entry->rxr_flags |= RXR_MULTI_RECV_CONSUMER;
	rx_entry->fi_flags |= FI_MULTI_RECV;
	rx_entry->master_entry = posted_entry;
	rx_entry->cq_entry.len = consumed_len;
	rx_entry->cq_entry.buf = rx_entry->iov[0].iov_base;
	rx_entry->cq_entry.op_context = posted_entry->cq_entry.op_context;
	rx_entry->cq_entry.flags = (FI_RECV | FI_MSG);

	ofi_consume_iov(posted_entry->iov, &posted_entry->iov_count,
			consumed_len);

	dlist_init(&rx_entry->multi_recv_entry);
	dlist_insert_tail(&rx_entry->multi_recv_entry,
			  &posted_entry->multi_recv_consumers);
	return rx_entry;
}

/* Post buf as undirected recv (FI_ADDR_UNSPEC) */
int rxr_ep_post_buf(struct rxr_ep *ep, uint64_t flags, enum rxr_lower_ep_type lower_ep_type)
{
	struct fi_msg msg;
	struct iovec msg_iov;
	void *desc;
	struct rxr_pkt_entry *rx_pkt_entry = NULL;
	int ret = 0;

	switch (lower_ep_type) {
	case SHM_EP:
		rx_pkt_entry = rxr_get_pkt_entry(ep, ep->rx_pkt_shm_pool);
		break;
	case EFA_EP:
		rx_pkt_entry = rxr_get_pkt_entry(ep, ep->rx_pkt_efa_pool);
		break;
	default:
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
			"invalid lower EP type %d\n", lower_ep_type);
		assert(0 && "invalid lower EP type\n");
	}
	if (OFI_UNLIKELY(!rx_pkt_entry)) {
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
			"Unable to allocate rx_pkt_entry\n");
		return -FI_ENOMEM;
	}

	rx_pkt_entry->x_entry = NULL;
	rx_pkt_entry->type = RXR_PKT_ENTRY_POSTED;

	msg_iov.iov_base = (void *)rxr_pkt_start(rx_pkt_entry);
	msg_iov.iov_len = ep->mtu_size;

	msg.msg_iov = &msg_iov;
	msg.iov_count = 1;
	msg.addr = FI_ADDR_UNSPEC;
	msg.context = rx_pkt_entry;
	msg.data = 0;

	switch (lower_ep_type) {
	case SHM_EP:
		/* pre-post buffer with shm */
#if ENABLE_DEBUG
		dlist_insert_tail(&rx_pkt_entry->dbg_entry,
				  &ep->rx_posted_buf_shm_list);
#endif
		desc = NULL;
		msg.desc = &desc;
		ret = fi_recvmsg(ep->shm_ep, &msg, flags);
		if (OFI_UNLIKELY(ret)) {
			rxr_release_rx_pkt_entry(ep, rx_pkt_entry);
			FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
				"failed to post buf for shm  %d (%s)\n", -ret,
				fi_strerror(-ret));
			return ret;
		}
		ep->posted_bufs_shm++;
		break;
	case EFA_EP:
		/* pre-post buffer with efa */
#if ENABLE_DEBUG
		dlist_insert_tail(&rx_pkt_entry->dbg_entry,
				  &ep->rx_posted_buf_list);
#endif
		desc = rxr_ep_mr_local(ep) ? fi_mr_desc(rx_pkt_entry->mr) : NULL;
		msg.desc = &desc;
		ret = fi_recvmsg(ep->rdm_ep, &msg, flags);
		if (OFI_UNLIKELY(ret)) {
			rxr_release_rx_pkt_entry(ep, rx_pkt_entry);
			FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
				"failed to post buf %d (%s)\n", -ret,
				fi_strerror(-ret));
			return ret;
		}
		ep->posted_bufs_efa++;
		break;
	default:
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
			"invalid lower EP type %d\n", lower_ep_type);
		assert(0 && "invalid lower EP type\n");
	}

	return 0;
}

void rxr_tx_entry_init(struct rxr_ep *ep, struct rxr_tx_entry *tx_entry,
		       const struct fi_msg *msg, uint32_t op, uint64_t flags)
{
	uint64_t tx_op_flags;

	tx_entry->type = RXR_TX_ENTRY;
	tx_entry->op = op;
	tx_entry->tx_id = ofi_buf_index(tx_entry);
	tx_entry->state = RXR_TX_RTS;
	tx_entry->addr = msg->addr;

	tx_entry->send_flags = 0;
	tx_entry->bytes_acked = 0;
	tx_entry->bytes_sent = 0;
	tx_entry->window = 0;
	tx_entry->total_len = ofi_total_iov_len(msg->msg_iov, msg->iov_count);
	tx_entry->iov_count = msg->iov_count;
	tx_entry->iov_index = 0;
	tx_entry->iov_mr_start = 0;
	tx_entry->iov_offset = 0;
	tx_entry->msg_id = 0;
	dlist_init(&tx_entry->queued_pkts);

	memcpy(&tx_entry->iov[0], msg->msg_iov, sizeof(struct iovec) * msg->iov_count);
	if (msg->desc)
		memcpy(&tx_entry->desc[0], msg->desc, sizeof(*msg->desc) * msg->iov_count);
	else
		memset(&tx_entry->desc[0], 0, sizeof(*msg->desc) * msg->iov_count);

	/* set flags */
	assert(ep->util_ep.tx_msg_flags == 0 ||
	       ep->util_ep.tx_msg_flags == FI_COMPLETION);
	tx_op_flags = ep->util_ep.tx_op_flags;
	if (ep->util_ep.tx_msg_flags == 0)
		tx_op_flags &= ~FI_COMPLETION;
	tx_entry->fi_flags = flags | tx_op_flags;

	/* cq_entry on completion */
	tx_entry->cq_entry.op_context = msg->context;
	tx_entry->cq_entry.len = ofi_total_iov_len(msg->msg_iov, msg->iov_count);
	if (OFI_LIKELY(tx_entry->cq_entry.len > 0))
		tx_entry->cq_entry.buf = msg->msg_iov[0].iov_base;
	else
		tx_entry->cq_entry.buf = NULL;

	tx_entry->cq_entry.data = msg->data;
	switch (op) {
	case ofi_op_tagged:
		tx_entry->cq_entry.flags = FI_TRANSMIT | FI_MSG | FI_TAGGED;
		break;
	case ofi_op_write:
		tx_entry->cq_entry.flags = FI_RMA | FI_WRITE;
		break;
	case ofi_op_read_req:
		tx_entry->cq_entry.flags = FI_RMA | FI_READ;
		break;
	case ofi_op_msg:
		tx_entry->cq_entry.flags = FI_TRANSMIT | FI_MSG;
		break;
	default:
		FI_WARN(&rxr_prov, FI_LOG_CQ, "invalid operation type\n");
		assert(0);
	}
}

/* create a new tx entry */
struct rxr_tx_entry *rxr_ep_alloc_tx_entry(struct rxr_ep *rxr_ep,
					   const struct fi_msg *msg,
					   uint32_t op,
					   uint64_t tag,
					   uint64_t flags)
{
	struct rxr_tx_entry *tx_entry;

	tx_entry = ofi_buf_alloc(rxr_ep->tx_entry_pool);
	if (OFI_UNLIKELY(!tx_entry)) {
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "TX entries exhausted.\n");
		return NULL;
	}

	rxr_tx_entry_init(rxr_ep, tx_entry, msg, op, flags);

	if (op == ofi_op_tagged) {
		tx_entry->cq_entry.tag = tag;
		tx_entry->tag = tag;
	}

#if ENABLE_DEBUG
	dlist_insert_tail(&tx_entry->tx_entry_entry, &rxr_ep->tx_entry_list);
#endif
	return tx_entry;
}

/*
 * Copies all consecutive small iov's into one buffer. If the function reaches
 * an iov greater than the max memcpy size, it will end, only copying up to
 * that iov.
 */
static size_t rxr_copy_from_iov(void *buf, uint64_t remaining_len,
				struct rxr_tx_entry *tx_entry)
{
	struct iovec *tx_iov = tx_entry->iov;
	uint64_t done = 0, len;

	while (tx_entry->iov_index < tx_entry->iov_count &&
	       done < remaining_len) {
		len = tx_iov[tx_entry->iov_index].iov_len;
		if (tx_entry->mr[tx_entry->iov_index])
			break;

		len -= tx_entry->iov_offset;

		/*
		 * If the amount to be written surpasses the remaining length,
		 * copy up to the remaining length and return, else copy the
		 * entire iov and continue.
		 */
		if (done + len > remaining_len) {
			len = remaining_len - done;
			memcpy((char *)buf + done,
			       (char *)tx_iov[tx_entry->iov_index].iov_base +
			       tx_entry->iov_offset, len);
			tx_entry->iov_offset += len;
			done += len;
			break;
		}
		memcpy((char *)buf + done,
		       (char *)tx_iov[tx_entry->iov_index].iov_base +
		       tx_entry->iov_offset, len);
		tx_entry->iov_index++;
		tx_entry->iov_offset = 0;
		done += len;
	}
	return done;
}

ssize_t rxr_ep_send_msg(struct rxr_ep *ep, struct rxr_pkt_entry *pkt_entry,
			const struct fi_msg *msg, uint64_t flags)
{
	struct rxr_peer *peer;
	size_t ret;

	peer = rxr_ep_get_peer(ep, pkt_entry->addr);
	assert(ep->tx_pending <= ep->max_outstanding_tx);

	if (ep->tx_pending == ep->max_outstanding_tx)
		return -FI_EAGAIN;

	if (peer->rnr_state & RXR_PEER_IN_BACKOFF)
		return -FI_EAGAIN;

#if ENABLE_DEBUG
	dlist_insert_tail(&pkt_entry->dbg_entry, &ep->tx_pkt_list);
#ifdef ENABLE_RXR_PKT_DUMP
	rxr_ep_print_pkt("Sent", ep, (struct rxr_base_hdr *)pkt_entry->pkt);
#endif
#endif
	if (rxr_env.enable_shm_transfer && peer->is_local) {
		ret = fi_sendmsg(ep->shm_ep, msg, flags);
	} else {
		ret = fi_sendmsg(ep->rdm_ep, msg, flags);
		if (OFI_LIKELY(!ret))
			rxr_ep_inc_tx_pending(ep, peer);
	}

	return ret;
}

static ssize_t rxr_ep_send_data_pkt_entry(struct rxr_ep *ep,
					  struct rxr_tx_entry *tx_entry,
					  struct rxr_pkt_entry *pkt_entry,
					  struct rxr_data_pkt *data_pkt)
{
	uint64_t payload_size;

	payload_size = MIN(tx_entry->total_len - tx_entry->bytes_sent,
			   ep->max_data_payload_size);
	payload_size = MIN(payload_size, tx_entry->window);
	data_pkt->hdr.seg_size = payload_size;

	pkt_entry->pkt_size = ofi_copy_from_iov(data_pkt->data,
						payload_size,
						tx_entry->iov,
						tx_entry->iov_count,
						tx_entry->bytes_sent);
	assert(pkt_entry->pkt_size == payload_size);

	pkt_entry->pkt_size += RXR_DATA_HDR_SIZE;
	pkt_entry->addr = tx_entry->addr;

	return rxr_ep_send_pkt_flags(ep, pkt_entry, tx_entry->addr,
				     tx_entry->send_flags);
}

/* If mr local is not set, will skip copying and only send user buffers */
static ssize_t rxr_ep_mr_send_data_pkt_entry(struct rxr_ep *ep,
					     struct rxr_tx_entry *tx_entry,
					     struct rxr_pkt_entry *pkt_entry,
					     struct rxr_data_pkt *data_pkt)
{
	/* The user's iov */
	struct iovec *tx_iov = tx_entry->iov;
	/* The constructed iov to be passed to sendv
	 * and corresponding fid_mrs
	 */
	struct iovec iov[ep->core_iov_limit];
	void *desc[ep->core_iov_limit];
	/* Constructed iov's total size */
	uint64_t payload_size = 0;
	/* pkt_entry offset to write data into */
	uint64_t pkt_used = 0;
	/* Remaining size that can fit in the constructed iov */
	uint64_t remaining_len = MIN(tx_entry->window,
				     ep->max_data_payload_size);
	/* The constructed iov's index */
	size_t i = 0;
	size_t len = 0;

	ssize_t ret;

	/* Assign packet header in constructed iov */
	iov[i].iov_base = rxr_pkt_start(pkt_entry);
	iov[i].iov_len = RXR_DATA_HDR_SIZE;
	desc[i] = rxr_ep_mr_local(ep) ? fi_mr_desc(pkt_entry->mr) : NULL;
	i++;

	/*
	 * Loops until payload size is at max, all user iovs are sent, the
	 * constructed iov count is greater than the core iov limit, or the tx
	 * entry window is exhausted.  Each iteration fills one entry of the
	 * iov to be sent.
	 */
	while (tx_entry->iov_index < tx_entry->iov_count &&
	       remaining_len > 0 && i < ep->core_iov_limit) {
		if (!rxr_ep_mr_local(ep) ||
		    /* from the inline registration post-RTS */
		    tx_entry->mr[tx_entry->iov_index] ||
		    /* from application-provided descriptor */
		    tx_entry->desc[tx_entry->iov_index]) {
			iov[i].iov_base =
				(char *)tx_iov[tx_entry->iov_index].iov_base +
				tx_entry->iov_offset;
			if (rxr_ep_mr_local(ep))
				desc[i] = tx_entry->desc[tx_entry->iov_index] ?
					  tx_entry->desc[tx_entry->iov_index] :
					  fi_mr_desc(tx_entry->mr[tx_entry->iov_index]);

			len = tx_iov[tx_entry->iov_index].iov_len
			      - tx_entry->iov_offset;
			if (len > remaining_len) {
				len = remaining_len;
				tx_entry->iov_offset += len;
			} else {
				tx_entry->iov_index++;
				tx_entry->iov_offset = 0;
			}
			iov[i].iov_len = len;
		} else {
			/*
			 * Copies any consecutive small iov's, returning size
			 * written while updating iov index and offset
			 */
			len = rxr_copy_from_iov((char *)data_pkt->data +
						 pkt_used,
						 remaining_len,
						 tx_entry);

			iov[i].iov_base = (char *)data_pkt->data + pkt_used;
			iov[i].iov_len = len;
			desc[i] = fi_mr_desc(pkt_entry->mr);
			pkt_used += len;
		}
		payload_size += len;
		remaining_len -= len;
		i++;
	}
	data_pkt->hdr.seg_size = (uint16_t)payload_size;
	pkt_entry->pkt_size = payload_size + RXR_DATA_HDR_SIZE;
	pkt_entry->addr = tx_entry->addr;

	FI_DBG(&rxr_prov, FI_LOG_EP_DATA,
	       "Sending an iov count, %zu with payload size: %lu.\n",
	       i, payload_size);
	ret = rxr_ep_sendv_pkt(ep, pkt_entry, tx_entry->addr,
			       (const struct iovec *)iov,
			       desc, i, tx_entry->send_flags);
	return ret;
}

ssize_t rxr_ep_post_data(struct rxr_ep *rxr_ep,
			 struct rxr_tx_entry *tx_entry)
{
	struct rxr_pkt_entry *pkt_entry;
	struct rxr_data_pkt *data_pkt;
	ssize_t ret;

	pkt_entry = rxr_get_pkt_entry(rxr_ep, rxr_ep->tx_pkt_efa_pool);

	if (OFI_UNLIKELY(!pkt_entry))
		return -FI_ENOMEM;

	pkt_entry->x_entry = (void *)tx_entry;
	pkt_entry->addr = tx_entry->addr;

	data_pkt = (struct rxr_data_pkt *)pkt_entry->pkt;

	data_pkt->hdr.type = RXR_DATA_PKT;
	data_pkt->hdr.version = RXR_PROTOCOL_VERSION;
	data_pkt->hdr.flags = 0;

	data_pkt->hdr.rx_id = tx_entry->rx_id;

	/*
	 * Data packets are sent in order so using bytes_sent is okay here.
	 */
	data_pkt->hdr.seg_offset = tx_entry->bytes_sent;

	if (efa_mr_cache_enable) {
		ret = rxr_ep_mr_send_data_pkt_entry(rxr_ep, tx_entry, pkt_entry,
						    data_pkt);
	} else {
		ret = rxr_ep_send_data_pkt_entry(rxr_ep, tx_entry, pkt_entry,
						 data_pkt);
	}

	if (OFI_UNLIKELY(ret)) {
		rxr_release_tx_pkt_entry(rxr_ep, pkt_entry);
		return ret;
	}
	data_pkt = rxr_get_data_pkt(pkt_entry->pkt);
	tx_entry->bytes_sent += data_pkt->hdr.seg_size;
	tx_entry->window -= data_pkt->hdr.seg_size;

	return ret;
}

void rxr_inline_mr_reg(struct rxr_domain *rxr_domain,
		       struct rxr_tx_entry *tx_entry)
{
	ssize_t ret;
	size_t offset;
	int index;

	/* Set the iov index and iov offset from bytes sent */
	offset = tx_entry->bytes_sent;
	for (index = 0; index < tx_entry->iov_count; ++index) {
		if (offset >= tx_entry->iov[index].iov_len) {
			offset -= tx_entry->iov[index].iov_len;
		} else {
			tx_entry->iov_index = index;
			tx_entry->iov_offset = offset;
			break;
		}
	}

	tx_entry->iov_mr_start = index;
	while (index < tx_entry->iov_count) {
		if (tx_entry->iov[index].iov_len > rxr_env.max_memcpy_size) {
			ret = fi_mr_reg(rxr_domain->rdm_domain,
					tx_entry->iov[index].iov_base,
					tx_entry->iov[index].iov_len,
					FI_SEND, 0, 0, 0,
					&tx_entry->mr[index], NULL);
			if (ret)
				tx_entry->mr[index] = NULL;
		}
		index++;
	}

	return;
}

void rxr_ep_calc_cts_window_credits(struct rxr_ep *ep, struct rxr_peer *peer,
				    uint64_t size, int request,
				    int *window, int *credits)
{
	struct efa_av *av;
	int num_peers;

	/*
	 * Adjust the peer credit pool based on the current AV size, which could
	 * have grown since the time this peer was initialized.
	 */
	av = rxr_ep_av(ep);
	num_peers = av->used - 1;
	if (num_peers && ofi_div_ceil(rxr_env.rx_window_size, num_peers) < peer->rx_credits)
		peer->rx_credits = ofi_div_ceil(peer->rx_credits, num_peers);

	/*
	 * Allocate credits for this transfer based on the request, the number
	 * of available data buffers, and the number of outstanding peers this
	 * endpoint is actively tracking in the AV. Also ensure that a minimum
	 * number of credits are allocated to the transfer so the sender can
	 * make progress.
	 */
	*credits = MIN(MIN(ep->available_data_bufs, ep->posted_bufs_efa),
		       peer->rx_credits);
	*credits = MIN(request, *credits);
	*credits = MAX(*credits, rxr_env.tx_min_credits);
	*window = MIN(size, *credits * ep->max_data_payload_size);
	if (peer->rx_credits > ofi_div_ceil(*window, ep->max_data_payload_size))
		peer->rx_credits -= ofi_div_ceil(*window, ep->max_data_payload_size);
}

int rxr_ep_init_cts_pkt(struct rxr_ep *ep,
			struct rxr_rx_entry *rx_entry,
			struct rxr_pkt_entry *pkt_entry)
{
	int window = 0;
	struct rxr_cts_hdr *cts_hdr;
	struct rxr_peer *peer;
	size_t bytes_left;

	cts_hdr = (struct rxr_cts_hdr *)pkt_entry->pkt;
	cts_hdr->type = RXR_CTS_PKT;
	cts_hdr->version = RXR_PROTOCOL_VERSION;
	cts_hdr->flags = 0;

	if (rx_entry->cq_entry.flags & FI_READ)
		cts_hdr->flags |= RXR_READ_REQ;

	cts_hdr->tx_id = rx_entry->tx_id;
	cts_hdr->rx_id = rx_entry->rx_id;

	bytes_left = rx_entry->total_len - rx_entry->bytes_done;
	peer = rxr_ep_get_peer(ep, rx_entry->addr);
	rxr_ep_calc_cts_window_credits(ep, peer, bytes_left,
				       rx_entry->credit_request,
				       &window, &rx_entry->credit_cts);
	cts_hdr->window = window;
	pkt_entry->pkt_size = RXR_CTS_HDR_SIZE;
	pkt_entry->addr = rx_entry->addr;
	pkt_entry->x_entry = (void *)rx_entry;
	return 0;
}

void rxr_ep_handle_cts_sent(struct rxr_ep *ep,
			    struct rxr_pkt_entry *pkt_entry)
{
	struct rxr_rx_entry *rx_entry;

	rx_entry = (struct rxr_rx_entry *)pkt_entry->x_entry;
	rx_entry->window = rxr_get_cts_hdr(pkt_entry->pkt)->window;
	ep->available_data_bufs -= rx_entry->credit_cts;

	/*
	 * Set a timer if available_bufs is exhausted. We may encounter a
	 * scenario where a peer has stopped responding so we need a fallback
	 * to replenish the credits.
	 */
	if (OFI_UNLIKELY(ep->available_data_bufs == 0))
		ep->available_data_bufs_ts = ofi_gettime_us();
}

void rxr_ep_init_connack_pkt_entry(struct rxr_ep *ep,
				   struct rxr_pkt_entry *pkt_entry,
				   fi_addr_t addr)
{
	struct rxr_connack_hdr *connack_hdr;

	connack_hdr = (struct rxr_connack_hdr *)pkt_entry->pkt;

	connack_hdr->type = RXR_CONNACK_PKT;
	connack_hdr->version = RXR_PROTOCOL_VERSION;
	connack_hdr->flags = 0;

	pkt_entry->pkt_size = RXR_CONNACK_HDR_SIZE;
	pkt_entry->addr = addr;
}

/* RTS related functions */
char *rxr_ep_init_rts_hdr(struct rxr_ep *ep,
			  struct rxr_tx_entry *tx_entry,
			  struct rxr_pkt_entry *pkt_entry)
{
	struct rxr_rts_hdr *rts_hdr;
	struct rxr_peer *peer;
	char *src;

	rts_hdr = (struct rxr_rts_hdr *)pkt_entry->pkt;
	peer = rxr_ep_get_peer(ep, tx_entry->addr);

	rts_hdr->type = RXR_RTS_PKT;
	rts_hdr->version = RXR_PROTOCOL_VERSION;
	rts_hdr->tag = tx_entry->tag;

	rts_hdr->data_len = tx_entry->total_len;
	rts_hdr->tx_id = tx_entry->tx_id;
	rts_hdr->msg_id = tx_entry->msg_id;
	/*
	 * Even with protocol versions prior to v3 that did not include a
	 * request in the RTS, the receiver can test for this flag and decide if
	 * it should be used as a heuristic for credit calculation. If the
	 * receiver is on <3 protocol version, the flag and the request just get
	 * ignored.
	 */
	rts_hdr->flags |= RXR_CREDIT_REQUEST;
	rts_hdr->credit_request = tx_entry->credit_request;

	if (tx_entry->fi_flags & FI_REMOTE_CQ_DATA) {
		rts_hdr->flags = RXR_REMOTE_CQ_DATA;
		pkt_entry->pkt_size = RXR_CTRL_HDR_SIZE;
		rxr_get_ctrl_cq_pkt(rts_hdr)->hdr.cq_data =
			tx_entry->cq_entry.data;
		src = rxr_get_ctrl_cq_pkt(rts_hdr)->data;
	} else {
		rts_hdr->flags = 0;
		pkt_entry->pkt_size = RXR_CTRL_HDR_SIZE_NO_CQ;
		src = rxr_get_ctrl_pkt(rts_hdr)->data;
	}

	if (tx_entry->cq_entry.flags & FI_TAGGED)
		rts_hdr->flags |= RXR_TAGGED;

	rts_hdr->addrlen = 0;
	if (OFI_UNLIKELY(peer->state != RXR_PEER_ACKED)) {
		/*
		 * This is the first communication with this peer on this
		 * endpoint, so send the core's address for this EP in the RTS
		 * so the remote side can insert it into its address vector.
		 */
		rts_hdr->addrlen = ep->core_addrlen;
		rts_hdr->flags |= RXR_REMOTE_SRC_ADDR;
		memcpy(src, ep->core_addr, rts_hdr->addrlen);
		src += rts_hdr->addrlen;
		pkt_entry->pkt_size += rts_hdr->addrlen;
	}

	return src;
}

static size_t rxr_ep_init_rts_pkt(struct rxr_ep *ep,
				  struct rxr_tx_entry *tx_entry,
				  struct rxr_pkt_entry *pkt_entry)
{
	struct rxr_peer *peer;
	struct rxr_rts_hdr *rts_hdr;
	char *data, *src;
	uint64_t data_len;
	size_t mtu = ep->mtu_size;

	if (tx_entry->op == ofi_op_read_req)
		return rxr_rma_init_read_rts(ep, tx_entry, pkt_entry);

	src = rxr_ep_init_rts_hdr(ep, tx_entry, pkt_entry);
	if (tx_entry->op == ofi_op_write)
		src = rxr_rma_init_rts_hdr(ep, tx_entry, pkt_entry, src);

	peer = rxr_ep_get_peer(ep, tx_entry->addr);
	assert(peer);
	data = src;
	rts_hdr = rxr_get_rts_hdr(pkt_entry->pkt);
	if (rxr_env.enable_shm_transfer && peer->is_local) {
		rts_hdr->flags |= RXR_SHM_HDR;
		/* will be sent over shm provider */
		if (tx_entry->total_len <= rxr_env.shm_max_medium_size) {
			data_len = ofi_copy_from_iov(data, rxr_env.shm_max_medium_size,
						     tx_entry->iov, tx_entry->iov_count, 0);
			assert(data_len == tx_entry->total_len);
			rts_hdr->flags |= RXR_SHM_HDR_DATA;
			pkt_entry->pkt_size += data_len;
		} else {
			/* rendezvous protocol
			 * place iov_count first, then local iov
			 */
			memcpy(data, &tx_entry->iov_count, sizeof(size_t));
			data += sizeof(size_t);
			pkt_entry->pkt_size += sizeof(size_t);
			memcpy(data, tx_entry->iov, sizeof(struct iovec) * tx_entry->iov_count);
			pkt_entry->pkt_size += sizeof(struct iovec) * tx_entry->iov_count;
		}
	} else {
		/* will be sent over efa provider */
		data_len = ofi_copy_from_iov(data, mtu - pkt_entry->pkt_size,
					     tx_entry->iov, tx_entry->iov_count, 0);
		assert(data_len == rxr_get_rts_data_size(ep, rts_hdr));
		pkt_entry->pkt_size += data_len;
	}

	assert(pkt_entry->pkt_size <= mtu);
	pkt_entry->addr = tx_entry->addr;
	pkt_entry->x_entry = (void *)tx_entry;
	return 0;
}

void rxr_ep_handle_rts_sent(struct rxr_ep *ep, struct rxr_pkt_entry *pkt_entry)
{
	struct rxr_peer *peer;
	struct rxr_tx_entry *tx_entry;
	size_t data_sent;

	tx_entry = (struct rxr_tx_entry *)pkt_entry->x_entry;

	peer = rxr_ep_get_peer(ep, pkt_entry->addr);
	assert(peer);
	if (tx_entry->op == ofi_op_read_req) {
		tx_entry->bytes_sent = 0;
		tx_entry->state = RXR_TX_WAIT_READ_FINISH;
		return;
	}

	data_sent = rxr_get_rts_data_size(ep, rxr_get_rts_hdr(pkt_entry->pkt));

	tx_entry->bytes_sent += data_sent;

	if ((rxr_env.enable_shm_transfer && peer->is_local) ||
	    !(efa_mr_cache_enable && tx_entry->total_len > data_sent))
		return;

	/*
	 * Register the data buffers inline only if the application did not
	 * provide a descriptor with the tx op
	 */
	if (rxr_ep_mr_local(ep) && !tx_entry->desc[0])
		rxr_inline_mr_reg(rxr_ep_domain(ep), tx_entry);

	return;
}

int rxr_ep_init_ctrl_pkt(struct rxr_ep *rxr_ep, int entry_type, void *x_entry,
			 int ctrl_type, struct rxr_pkt_entry *pkt_entry)
{
	int ret = 0;

	switch (ctrl_type) {
	case RXR_RTS_PKT:
		ret = rxr_ep_init_rts_pkt(rxr_ep, (struct rxr_tx_entry *)x_entry, pkt_entry);
		break;
	case RXR_READRSP_PKT:
		ret = rxr_rma_init_readrsp_pkt(rxr_ep, (struct rxr_tx_entry *)x_entry, pkt_entry);
		break;
	case RXR_CTS_PKT:
		ret = rxr_ep_init_cts_pkt(rxr_ep, (struct rxr_rx_entry *)x_entry, pkt_entry);
		break;
	case RXR_EOR_PKT:
		ret = rxr_rma_init_eor_pkt(rxr_ep, (struct rxr_rx_entry *)x_entry, pkt_entry);
		break;
	default:
		ret = -FI_EINVAL;
		assert(0 && "unknown pkt type to init");
		break;
	}

	return ret;
}

void rxr_ep_handle_ctrl_sent(struct rxr_ep *rxr_ep, struct rxr_pkt_entry *pkt_entry)
{
	int ctrl_type = rxr_get_base_hdr(pkt_entry->pkt)->type;

	switch (ctrl_type) {
	case RXR_RTS_PKT:
		rxr_ep_handle_rts_sent(rxr_ep, pkt_entry);
		break;
	case RXR_READRSP_PKT:
		rxr_rma_handle_readrsp_sent(rxr_ep, pkt_entry);
		break;
	case RXR_CTS_PKT:
		rxr_ep_handle_cts_sent(rxr_ep, pkt_entry);
		break;
	case RXR_EOR_PKT:
		rxr_rma_handle_eor_sent(rxr_ep, pkt_entry);
		break;
	default:
		assert(0 && "Unknown packet type to handle sent");
		break;
	}
}

static size_t rxr_ep_post_ctrl(struct rxr_ep *rxr_ep, int entry_type, void *x_entry,
			       int ctrl_type, bool inject)
{
	struct rxr_pkt_entry *pkt_entry;
	struct rxr_tx_entry *tx_entry;
	struct rxr_rx_entry *rx_entry;
	struct rxr_peer *peer;
	ssize_t err;
	fi_addr_t addr;

	if (entry_type == RXR_TX_ENTRY) {
		tx_entry = (struct rxr_tx_entry *)x_entry;
		addr = tx_entry->addr;
	} else {
		rx_entry = (struct rxr_rx_entry *)x_entry;
		addr = rx_entry->addr;
	}

	peer = rxr_ep_get_peer(rxr_ep, addr);
	if (peer->is_local)
		pkt_entry = rxr_get_pkt_entry(rxr_ep, rxr_ep->tx_pkt_shm_pool);
	else
		pkt_entry = rxr_get_pkt_entry(rxr_ep, rxr_ep->tx_pkt_efa_pool);

	if (!pkt_entry)
		return -FI_EAGAIN;

	err = rxr_ep_init_ctrl_pkt(rxr_ep, entry_type, x_entry, ctrl_type, pkt_entry);
	if (OFI_UNLIKELY(err)) {
		rxr_release_tx_pkt_entry(rxr_ep, pkt_entry);
		return err;
	}

	/* if send, tx_pkt_entry will be released while handle completion
	 * if inject, there will not be completion, therefore tx_pkt_entry has to be
	 * released here
	 */
	if (inject)
		err = rxr_ep_inject_pkt(rxr_ep, pkt_entry, addr);
	else
		err = rxr_ep_send_pkt(rxr_ep, pkt_entry, addr);

	if (OFI_UNLIKELY(err)) {
		rxr_release_tx_pkt_entry(rxr_ep, pkt_entry);
		return err;
	}

	rxr_ep_handle_ctrl_sent(rxr_ep, pkt_entry);

	if (inject)
		rxr_release_tx_pkt_entry(rxr_ep, pkt_entry);

	return 0;
}

int rxr_ep_post_ctrl_or_queue(struct rxr_ep *ep, int entry_type, void *x_entry, int ctrl_type, bool inject)
{
	ssize_t err;
	struct rxr_tx_entry *tx_entry;
	struct rxr_rx_entry *rx_entry;

	err = rxr_ep_post_ctrl(ep, entry_type, x_entry, ctrl_type, inject);
	if (err == -FI_EAGAIN) {
		if (entry_type == RXR_TX_ENTRY) {
			tx_entry = (struct rxr_tx_entry *)x_entry;
			tx_entry->state = RXR_TX_QUEUED_CTRL;
			tx_entry->queued_ctrl.type = ctrl_type;
			tx_entry->queued_ctrl.inject = inject;
			dlist_insert_tail(&tx_entry->queued_entry,
					  &ep->tx_entry_queued_list);
		} else {
			assert(entry_type == RXR_RX_ENTRY);
			rx_entry = (struct rxr_rx_entry *)x_entry;
			rx_entry->state = RXR_RX_QUEUED_CTRL;
			rx_entry->queued_ctrl.type = ctrl_type;
			rx_entry->queued_ctrl.inject = inject;
			dlist_insert_tail(&rx_entry->queued_entry,
					  &ep->rx_entry_queued_list);
		}

		err = 0;
	}

	return err;
}

/* Generic send */
int rxr_ep_set_tx_credit_request(struct rxr_ep *rxr_ep, struct rxr_tx_entry *tx_entry)
{
	struct rxr_peer *peer;
	int pending;

	peer = rxr_ep_get_peer(rxr_ep, tx_entry->addr);
	assert(peer);
	/*
	 * Init tx state for this peer. The rx state and reorder buffers will be
	 * initialized on the first recv so as to not allocate resources unless
	 * necessary.
	 */
	if (!peer->tx_init) {
		peer->tx_credits = rxr_env.tx_max_credits;
		peer->tx_init = 1;
	}

	/*
	 * Divy up available credits to outstanding transfers and request the
	 * minimum of that and the amount required to finish the current long
	 * message.
	 */
	pending = peer->tx_pending + 1;
	tx_entry->credit_request = MIN(ofi_div_ceil(peer->tx_credits, pending),
				       ofi_div_ceil(tx_entry->total_len,
						    rxr_ep->max_data_payload_size));
	tx_entry->credit_request = MAX(tx_entry->credit_request,
				       rxr_env.tx_min_credits);
	if (peer->tx_credits >= tx_entry->credit_request)
		peer->tx_credits -= tx_entry->credit_request;

	/* Queue this RTS for later if there are too many outstanding packets */
	if (!tx_entry->credit_request)
		return -FI_EAGAIN;

	return 0;
}

static void rxr_ep_free_res(struct rxr_ep *rxr_ep)
{
	struct rxr_peer *peer;
	struct dlist_entry *tmp;
#if ENABLE_DEBUG
	struct dlist_entry *entry;
	struct rxr_rx_entry *rx_entry;
	struct rxr_tx_entry *tx_entry;
	struct rxr_pkt_entry *pkt;
#endif

	if (rxr_need_sas_ordering(rxr_ep)) {
		dlist_foreach_container_safe(&rxr_ep->peer_list,
					     struct rxr_peer,
					     peer, entry, tmp) {
			ofi_recvwin_free(peer->robuf);
		}

		if (rxr_ep->robuf_fs)
			rxr_robuf_fs_free(rxr_ep->robuf_fs);
	}

#if ENABLE_DEBUG
	dlist_foreach_container_safe(&rxr_ep->peer_list,
				     struct rxr_peer,
				     peer, entry, tmp) {
		/*
		 * TODO: Add support for wait/signal until all pending messages
		 * have been sent/received so the core does not attempt to
		 * complete a data operation or an internal RxR transfer after
		 * the EP is shutdown.
		 */
		if (peer->state == RXR_PEER_CONNREQ)
			FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
				"Closing EP with unacked CONNREQs in flight\n");
	}

	dlist_foreach(&rxr_ep->rx_unexp_list, entry) {
		rx_entry = container_of(entry, struct rxr_rx_entry, entry);
		rxr_release_rx_pkt_entry(rxr_ep, rx_entry->unexp_rts_pkt);
	}

	dlist_foreach(&rxr_ep->rx_unexp_tagged_list, entry) {
		rx_entry = container_of(entry, struct rxr_rx_entry, entry);
		rxr_release_rx_pkt_entry(rxr_ep, rx_entry->unexp_rts_pkt);
	}

	dlist_foreach(&rxr_ep->rx_entry_queued_list, entry) {
		rx_entry = container_of(entry, struct rxr_rx_entry,
					queued_entry);
		dlist_foreach_container_safe(&rx_entry->queued_pkts,
					     struct rxr_pkt_entry,
					     pkt, entry, tmp)
			rxr_release_tx_pkt_entry(rxr_ep, pkt);
	}

	dlist_foreach(&rxr_ep->tx_entry_queued_list, entry) {
		tx_entry = container_of(entry, struct rxr_tx_entry,
					queued_entry);
		dlist_foreach_container_safe(&tx_entry->queued_pkts,
					     struct rxr_pkt_entry,
					     pkt, entry, tmp)
			rxr_release_tx_pkt_entry(rxr_ep, pkt);
	}

	dlist_foreach_safe(&rxr_ep->rx_pkt_list, entry, tmp) {
		pkt = container_of(entry, struct rxr_pkt_entry, dbg_entry);
		rxr_release_rx_pkt_entry(rxr_ep, pkt);
	}

	dlist_foreach_safe(&rxr_ep->tx_pkt_list, entry, tmp) {
		pkt = container_of(entry, struct rxr_pkt_entry, dbg_entry);
		rxr_release_tx_pkt_entry(rxr_ep, pkt);
	}

	dlist_foreach_safe(&rxr_ep->rx_posted_buf_list, entry, tmp) {
		pkt = container_of(entry, struct rxr_pkt_entry, dbg_entry);
		ofi_buf_free(pkt);
	}
	dlist_foreach_safe(&rxr_ep->rx_entry_list, entry, tmp) {
		rx_entry = container_of(entry, struct rxr_rx_entry,
					rx_entry_entry);
		rxr_release_rx_entry(rxr_ep, rx_entry);
	}
	dlist_foreach_safe(&rxr_ep->tx_entry_list, entry, tmp) {
		tx_entry = container_of(entry, struct rxr_tx_entry,
					tx_entry_entry);
		rxr_release_tx_entry(rxr_ep, tx_entry);
	}
	if (rxr_env.enable_shm_transfer) {
		dlist_foreach_safe(&rxr_ep->rx_posted_buf_shm_list, entry, tmp) {
			pkt = container_of(entry, struct rxr_pkt_entry, dbg_entry);
			ofi_buf_free(pkt);
		}
	}
#endif

	if (rxr_ep->rx_entry_pool)
		ofi_bufpool_destroy(rxr_ep->rx_entry_pool);

	if (rxr_ep->tx_entry_pool)
		ofi_bufpool_destroy(rxr_ep->tx_entry_pool);

	if (rxr_ep->readrsp_tx_entry_pool)
		ofi_bufpool_destroy(rxr_ep->readrsp_tx_entry_pool);

	if (rxr_ep->rx_ooo_pkt_pool)
		ofi_bufpool_destroy(rxr_ep->rx_ooo_pkt_pool);

	if (rxr_ep->rx_unexp_pkt_pool)
		ofi_bufpool_destroy(rxr_ep->rx_unexp_pkt_pool);

	if (rxr_ep->rx_pkt_efa_pool)
		ofi_bufpool_destroy(rxr_ep->rx_pkt_efa_pool);

	if (rxr_ep->tx_pkt_efa_pool)
		ofi_bufpool_destroy(rxr_ep->tx_pkt_efa_pool);

	if (rxr_env.enable_shm_transfer) {
		if (rxr_ep->rx_pkt_shm_pool)
			ofi_bufpool_destroy(rxr_ep->rx_pkt_shm_pool);

		if (rxr_ep->tx_pkt_shm_pool)
			ofi_bufpool_destroy(rxr_ep->tx_pkt_shm_pool);
	}
}

static int rxr_ep_close(struct fid *fid)
{
	int ret, retv = 0;
	struct rxr_ep *rxr_ep;

	rxr_ep = container_of(fid, struct rxr_ep, util_ep.ep_fid.fid);

	ret = fi_close(&rxr_ep->rdm_ep->fid);
	if (ret) {
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "Unable to close EP\n");
		retv = ret;
	}

	ret = fi_close(&rxr_ep->rdm_cq->fid);
	if (ret) {
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "Unable to close msg CQ\n");
		retv = ret;
	}

	/* Close shm provider's endpoint and cq */
	if (rxr_env.enable_shm_transfer) {
		ret = fi_close(&rxr_ep->shm_ep->fid);
		if (ret) {
			FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "Unable to close shm EP\n");
			retv = ret;
		}

		ret = fi_close(&rxr_ep->shm_cq->fid);
		if (ret) {
			FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "Unable to close shm CQ\n");
			retv = ret;
		}
	}


	ret = ofi_endpoint_close(&rxr_ep->util_ep);
	if (ret) {
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "Unable to close util EP\n");
		retv = ret;
	}
	rxr_ep_free_res(rxr_ep);
	free(rxr_ep->peer);
	free(rxr_ep);
	return retv;
}

static int rxr_ep_bind(struct fid *ep_fid, struct fid *bfid, uint64_t flags)
{
	struct rxr_ep *rxr_ep =
		container_of(ep_fid, struct rxr_ep, util_ep.ep_fid.fid);
	struct util_cq *cq;
	struct efa_av *av;
	struct util_cntr *cntr;
	struct util_eq *eq;
	struct dlist_entry *ep_list_first_entry;
	struct util_ep *util_ep;
	struct rxr_ep *rxr_first_ep;
	struct rxr_peer *first_ep_peer, *peer;
	int ret = 0;
	size_t i;

	switch (bfid->fclass) {
	case FI_CLASS_AV:
		av = container_of(bfid, struct efa_av, util_av.av_fid.fid);
		/* Bind util provider endpoint and av */
		ret = ofi_ep_bind_av(&rxr_ep->util_ep, &av->util_av);
		if (ret)
			return ret;

		ret = fi_ep_bind(rxr_ep->rdm_ep, &av->util_av.av_fid.fid, flags);
		if (ret)
			return ret;

		rxr_ep->peer = calloc(av->util_av.count,
				      sizeof(struct rxr_peer));
		if (!rxr_ep->peer)
			return -FI_ENOMEM;

		rxr_ep->robuf_fs = rxr_robuf_fs_create(av->util_av.count,
						       NULL, NULL);
		if (!rxr_ep->robuf_fs)
			return -FI_ENOMEM;

		/* Bind shm provider endpoint & shm av */
		if (rxr_env.enable_shm_transfer) {
			ret = fi_ep_bind(rxr_ep->shm_ep, &av->shm_rdm_av->fid, flags);
			if (ret)
				return ret;

			/*
			 * We always update the new added EP's local information with the first
			 * bound EP. The if (ep_list_first_entry->next) check here is to skip the
			 * update for the first bound EP.
			 */
			ep_list_first_entry = av->util_av.ep_list.next;
			if (ep_list_first_entry->next) {
				util_ep = container_of(ep_list_first_entry, struct util_ep, av_entry);
				rxr_first_ep = container_of(util_ep, struct rxr_ep, util_ep);

				/*
				 * Copy the entire peer array, because we may not be able to make the
				 * assumption that insertions are always indexed in order in the future.
				 */
				for (i = 0; i <= av->util_av.count; i++) {
					first_ep_peer = rxr_ep_get_peer(rxr_first_ep, i);
					if (first_ep_peer->is_local) {
						peer = rxr_ep_get_peer(rxr_ep, i);
						peer->shm_fiaddr = first_ep_peer->shm_fiaddr;
						peer->is_local = 1;
					}
				}
			}
		}
		break;
	case FI_CLASS_CQ:
		cq = container_of(bfid, struct util_cq, cq_fid.fid);

		ret = ofi_ep_bind_cq(&rxr_ep->util_ep, cq, flags);
		if (ret)
			return ret;
		break;
	case FI_CLASS_CNTR:
		cntr = container_of(bfid, struct util_cntr, cntr_fid.fid);

		ret = ofi_ep_bind_cntr(&rxr_ep->util_ep, cntr, flags);
		if (ret)
			return ret;
		break;
	case FI_CLASS_EQ:
		eq = container_of(bfid, struct util_eq, eq_fid.fid);

		ret = ofi_ep_bind_eq(&rxr_ep->util_ep, eq);
		if (ret)
			return ret;
		break;
	default:
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "invalid fid class\n");
		ret = -FI_EINVAL;
		break;
	}
	return ret;
}

static int rxr_ep_ctrl(struct fid *fid, int command, void *arg)
{
	ssize_t ret;
	size_t i;
	struct rxr_ep *ep;
	uint64_t flags = FI_MORE;
	size_t rx_size, shm_rx_size;
	char shm_ep_name[NAME_MAX];

	switch (command) {
	case FI_ENABLE:
		/* Enable core endpoints & post recv buff */
		ep = container_of(fid, struct rxr_ep, util_ep.ep_fid.fid);

		rx_size = rxr_get_rx_pool_chunk_cnt(ep);
		ret = fi_enable(ep->rdm_ep);
		if (ret)
			return ret;

		fastlock_acquire(&ep->util_ep.lock);
		for (i = 0; i < rx_size; i++) {
			if (i == rx_size - 1)
				flags = 0;

			ret = rxr_ep_post_buf(ep, flags, EFA_EP);

			if (ret)
				goto out;
		}

		ep->available_data_bufs = rx_size;

		ep->core_addrlen = RXR_MAX_NAME_LENGTH;
		ret = fi_getname(&ep->rdm_ep->fid,
				 ep->core_addr,
				 &ep->core_addrlen);
		assert(ret != -FI_ETOOSMALL);
		FI_DBG(&rxr_prov, FI_LOG_EP_CTRL, "core_addrlen = %ld\n",
		       ep->core_addrlen);

		/* Enable shm provider endpoint & post recv buff.
		 * Once core ep enabled, 18 bytes efa_addr (16 bytes raw + 2 bytes qpn) is set.
		 * We convert the address to 'gid_qpn' format, and set it as shm ep name, so
		 * that shm ep can create shared memory region with it when enabling.
		 * In this way, each peer is able to open and map to other local peers'
		 * shared memory region.
		 */
		if (rxr_env.enable_shm_transfer) {
			ret = rxr_ep_efa_addr_to_str(ep->core_addr, shm_ep_name);
			if (ret < 0)
				goto out;

			fi_setname(&ep->shm_ep->fid, shm_ep_name, sizeof(shm_ep_name));
			shm_rx_size = shm_info->rx_attr->size;
			ret = fi_enable(ep->shm_ep);
			if (ret)
				return ret;
			/* Pre-post buffer to receive from shm provider */
			for (i = 0; i < shm_rx_size; i++) {
				if (i == shm_rx_size - 1)
					flags = 0;

				ret = rxr_ep_post_buf(ep, flags, SHM_EP);

				if (ret)
					goto out;
			}
		}

out:
		fastlock_release(&ep->util_ep.lock);
		break;
	default:
		ret = -FI_ENOSYS;
		break;
	}

	return ret;
}

static struct fi_ops rxr_ep_fi_ops = {
	.size = sizeof(struct fi_ops),
	.close = rxr_ep_close,
	.bind = rxr_ep_bind,
	.control = rxr_ep_ctrl,
	.ops_open = fi_no_ops_open,
};

static int rxr_ep_cancel_match_recv(struct dlist_entry *item,
				    const void *context)
{
	struct rxr_rx_entry *rx_entry = container_of(item,
						     struct rxr_rx_entry,
						     entry);
	return rx_entry->cq_entry.op_context == context;
}

static ssize_t rxr_ep_cancel_recv(struct rxr_ep *ep,
				  struct dlist_entry *recv_list,
				  void *context)
{
	struct rxr_domain *domain;
	struct dlist_entry *entry;
	struct rxr_rx_entry *rx_entry;
	struct fi_cq_err_entry err_entry;
	uint32_t api_version;

	fastlock_acquire(&ep->util_ep.lock);
	entry = dlist_remove_first_match(recv_list,
					 &rxr_ep_cancel_match_recv,
					 context);
	if (!entry) {
		fastlock_release(&ep->util_ep.lock);
		return 0;
	}

	rx_entry = container_of(entry, struct rxr_rx_entry, entry);
	rx_entry->rxr_flags |= RXR_RECV_CANCEL;
	if (rx_entry->fi_flags & FI_MULTI_RECV &&
	    rx_entry->rxr_flags & RXR_MULTI_RECV_POSTED) {
		if (dlist_empty(&rx_entry->multi_recv_consumers)) {
			/*
			 * No pending messages for the buffer,
			 * release it back to the app.
			 */
			rx_entry->cq_entry.flags |= FI_MULTI_RECV;
		} else {
			rx_entry = container_of(rx_entry->multi_recv_consumers.next,
						struct rxr_rx_entry,
						multi_recv_entry);
			rxr_msg_multi_recv_handle_completion(ep, rx_entry);
		}
	} else if (rx_entry->fi_flags & FI_MULTI_RECV &&
		   rx_entry->rxr_flags & RXR_MULTI_RECV_CONSUMER) {
		rxr_msg_multi_recv_handle_completion(ep, rx_entry);
	}
	fastlock_release(&ep->util_ep.lock);
	memset(&err_entry, 0, sizeof(err_entry));
	err_entry.op_context = rx_entry->cq_entry.op_context;
	err_entry.flags |= rx_entry->cq_entry.flags;
	err_entry.tag = rx_entry->tag;
	err_entry.err = FI_ECANCELED;
	err_entry.prov_errno = -FI_ECANCELED;

	domain = rxr_ep_domain(ep);
	api_version =
		 domain->util_domain.fabric->fabric_fid.api_version;
	if (FI_VERSION_GE(api_version, FI_VERSION(1, 5)))
		err_entry.err_data_size = 0;
	/*
	 * Other states are currently receiving data. Subsequent messages will
	 * be sunk (via RXR_RECV_CANCEL flag) and the completion suppressed.
	 */
	if (rx_entry->state & (RXR_RX_INIT | RXR_RX_UNEXP | RXR_RX_MATCHED))
		rxr_release_rx_entry(ep, rx_entry);
	return ofi_cq_write_error(ep->util_ep.rx_cq, &err_entry);
}

static ssize_t rxr_ep_cancel(fid_t fid_ep, void *context)
{
	struct rxr_ep *ep;
	int ret;

	ep = container_of(fid_ep, struct rxr_ep, util_ep.ep_fid.fid);

	ret = rxr_ep_cancel_recv(ep, &ep->rx_list, context);
	if (ret)
		return ret;

	ret = rxr_ep_cancel_recv(ep, &ep->rx_tagged_list, context);
	return ret;
}

static int rxr_ep_getopt(fid_t fid, int level, int optname, void *optval,
			 size_t *optlen)
{
	struct rxr_ep *rxr_ep = container_of(fid, struct rxr_ep,
					     util_ep.ep_fid.fid);

	if (level != FI_OPT_ENDPOINT || optname != FI_OPT_MIN_MULTI_RECV)
		return -FI_ENOPROTOOPT;

	*(size_t *)optval = rxr_ep->min_multi_recv_size;
	*optlen = sizeof(size_t);

	return FI_SUCCESS;
}

static int rxr_ep_setopt(fid_t fid, int level, int optname,
			 const void *optval, size_t optlen)
{
	struct rxr_ep *rxr_ep = container_of(fid, struct rxr_ep,
					     util_ep.ep_fid.fid);

	if (level != FI_OPT_ENDPOINT || optname != FI_OPT_MIN_MULTI_RECV)
		return -FI_ENOPROTOOPT;

	if (optlen < sizeof(size_t))
		return -FI_EINVAL;

	rxr_ep->min_multi_recv_size = *(size_t *)optval;

	return FI_SUCCESS;
}

static struct fi_ops_ep rxr_ops_ep = {
	.size = sizeof(struct fi_ops_ep),
	.cancel = rxr_ep_cancel,
	.getopt = rxr_ep_getopt,
	.setopt = rxr_ep_setopt,
	.tx_ctx = fi_no_tx_ctx,
	.rx_ctx = fi_no_rx_ctx,
	.rx_size_left = fi_no_rx_size_left,
	.tx_size_left = fi_no_tx_size_left,
};

static int rxr_buf_region_alloc_hndlr(struct ofi_bufpool_region *region)
{
	size_t ret;
	struct fid_mr *mr;
	struct rxr_domain *domain = region->pool->attr.context;

	ret = fi_mr_reg(domain->rdm_domain, region->alloc_region,
			region->pool->alloc_size,
			FI_SEND | FI_RECV, 0, 0, 0, &mr, NULL);

	region->context = mr;
	return ret;
}

static void rxr_buf_region_free_hndlr(struct ofi_bufpool_region *region)
{
	ssize_t ret;

	ret = fi_close((struct fid *)region->context);
	if (ret)
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
			"Unable to deregister memory in a buf pool: %s\n",
			fi_strerror(-ret));
}

static int rxr_create_pkt_pool(struct rxr_ep *ep, size_t size,
			       size_t chunk_count,
			       struct ofi_bufpool **buf_pool)
{
	struct ofi_bufpool_attr attr = {
		.size		= size,
		.alignment	= RXR_BUF_POOL_ALIGNMENT,
		.max_cnt	= chunk_count,
		.chunk_cnt	= chunk_count,
		.alloc_fn	= rxr_ep_mr_local(ep) ?
					rxr_buf_region_alloc_hndlr : NULL,
		.free_fn	= rxr_ep_mr_local(ep) ?
					rxr_buf_region_free_hndlr : NULL,
		.init_fn	= NULL,
		.context	= rxr_ep_domain(ep),
		.flags		= OFI_BUFPOOL_HUGEPAGES,
	};

	return ofi_bufpool_create_attr(&attr, buf_pool);
}

int rxr_ep_init(struct rxr_ep *ep)
{
	size_t entry_sz;
	int ret;

	entry_sz = ep->mtu_size + sizeof(struct rxr_pkt_entry);
#ifdef ENABLE_EFA_POISONING
	ep->tx_pkt_pool_entry_sz = entry_sz;
	ep->rx_pkt_pool_entry_sz = entry_sz;
#endif

	ret = rxr_create_pkt_pool(ep, entry_sz, rxr_get_tx_pool_chunk_cnt(ep),
				  &ep->tx_pkt_efa_pool);
	if (ret)
		goto err_out;

	ret = rxr_create_pkt_pool(ep, entry_sz, rxr_get_rx_pool_chunk_cnt(ep),
				  &ep->rx_pkt_efa_pool);
	if (ret)
		goto err_free_tx_pool;

	if (rxr_env.rx_copy_unexp) {
		ret = ofi_bufpool_create(&ep->rx_unexp_pkt_pool, entry_sz,
					 RXR_BUF_POOL_ALIGNMENT, 0,
					 rxr_get_rx_pool_chunk_cnt(ep), 0);

		if (ret)
			goto err_free_rx_pool;
	}

	if (rxr_env.rx_copy_ooo) {
		ret = ofi_bufpool_create(&ep->rx_ooo_pkt_pool, entry_sz,
					 RXR_BUF_POOL_ALIGNMENT, 0,
					 rxr_env.recvwin_size, 0);

		if (ret)
			goto err_free_rx_unexp_pool;
	}

	ret = ofi_bufpool_create(&ep->tx_entry_pool,
				 sizeof(struct rxr_tx_entry),
				 RXR_BUF_POOL_ALIGNMENT,
				 ep->tx_size, ep->tx_size, 0);
	if (ret)
		goto err_free_rx_ooo_pool;

	ret = ofi_bufpool_create(&ep->readrsp_tx_entry_pool,
				 sizeof(struct rxr_tx_entry),
				 RXR_BUF_POOL_ALIGNMENT,
				 RXR_MAX_RX_QUEUE_SIZE,
				 ep->rx_size, 0);
	if (ret)
		goto err_free_tx_entry_pool;

	ret = ofi_bufpool_create(&ep->rx_entry_pool,
				 sizeof(struct rxr_rx_entry),
				 RXR_BUF_POOL_ALIGNMENT,
				 RXR_MAX_RX_QUEUE_SIZE,
				 ep->rx_size, 0);
	if (ret)
		goto err_free_readrsp_tx_entry_pool;

	/* create pkt pool for shm */
	if (rxr_env.enable_shm_transfer) {
		ret = ofi_bufpool_create(&ep->tx_pkt_shm_pool,
					 entry_sz,
					 RXR_BUF_POOL_ALIGNMENT,
					 shm_info->tx_attr->size,
					 shm_info->tx_attr->size, 0);
		if (ret)
			goto err_free_rx_entry_pool;

		ret = ofi_bufpool_create(&ep->rx_pkt_shm_pool,
					 entry_sz,
					 RXR_BUF_POOL_ALIGNMENT,
					 shm_info->rx_attr->size,
					 shm_info->rx_attr->size, 0);
		if (ret)
			goto err_free_tx_pkt_shm_pool;

		dlist_init(&ep->rx_posted_buf_shm_list);
	}

	/* Initialize entry list */
	dlist_init(&ep->rx_list);
	dlist_init(&ep->rx_unexp_list);
	dlist_init(&ep->rx_tagged_list);
	dlist_init(&ep->rx_unexp_tagged_list);
	dlist_init(&ep->rx_posted_buf_list);
	dlist_init(&ep->rx_entry_queued_list);
	dlist_init(&ep->tx_entry_queued_list);
	dlist_init(&ep->tx_pending_list);
	dlist_init(&ep->peer_backoff_list);
	dlist_init(&ep->peer_list);
#if ENABLE_DEBUG
	dlist_init(&ep->rx_pending_list);
	dlist_init(&ep->rx_pkt_list);
	dlist_init(&ep->tx_pkt_list);
	dlist_init(&ep->rx_entry_list);
	dlist_init(&ep->tx_entry_list);
#endif

	return 0;

err_free_tx_pkt_shm_pool:
	if (ep->tx_pkt_shm_pool)
		ofi_bufpool_destroy(ep->tx_pkt_shm_pool);
err_free_rx_entry_pool:
	if (ep->rx_entry_pool)
		ofi_bufpool_destroy(ep->rx_entry_pool);
err_free_readrsp_tx_entry_pool:
	if (ep->readrsp_tx_entry_pool)
		ofi_bufpool_destroy(ep->readrsp_tx_entry_pool);
err_free_tx_entry_pool:
	if (ep->tx_entry_pool)
		ofi_bufpool_destroy(ep->tx_entry_pool);
err_free_rx_ooo_pool:
	if (rxr_env.rx_copy_ooo && ep->rx_ooo_pkt_pool)
		ofi_bufpool_destroy(ep->rx_ooo_pkt_pool);
err_free_rx_unexp_pool:
	if (rxr_env.rx_copy_unexp && ep->rx_unexp_pkt_pool)
		ofi_bufpool_destroy(ep->rx_unexp_pkt_pool);
err_free_rx_pool:
	if (ep->rx_pkt_efa_pool)
		ofi_bufpool_destroy(ep->rx_pkt_efa_pool);
err_free_tx_pool:
	if (ep->tx_pkt_efa_pool)
		ofi_bufpool_destroy(ep->tx_pkt_efa_pool);
err_out:
	return ret;
}

static int rxr_ep_rdm_setname(fid_t fid, void *addr, size_t addrlen)
{
	struct rxr_ep *ep;

	ep = container_of(fid, struct rxr_ep, util_ep.ep_fid.fid);
	return fi_setname(&ep->rdm_ep->fid, addr, addrlen);
}

static int rxr_ep_rdm_getname(fid_t fid, void *addr, size_t *addrlen)
{
	struct rxr_ep *ep;

	ep = container_of(fid, struct rxr_ep, util_ep.ep_fid.fid);
	return fi_getname(&ep->rdm_ep->fid, addr, addrlen);
}

struct fi_ops_cm rxr_ep_cm = {
	.size = sizeof(struct fi_ops_cm),
	.setname = rxr_ep_rdm_setname,
	.getname = rxr_ep_rdm_getname,
	.getpeer = fi_no_getpeer,
	.connect = fi_no_connect,
	.listen = fi_no_listen,
	.accept = fi_no_accept,
	.reject = fi_no_reject,
	.shutdown = fi_no_shutdown,
	.join = fi_no_join,
};

static inline int rxr_ep_bulk_post_recv(struct rxr_ep *ep)
{
	uint64_t flags = FI_MORE;
	int ret;

	while (ep->rx_bufs_efa_to_post) {
		if (ep->rx_bufs_efa_to_post == 1)
			flags = 0;
		ret = rxr_ep_post_buf(ep, flags, EFA_EP);
		if (OFI_LIKELY(!ret))
			ep->rx_bufs_efa_to_post--;
		else
			return ret;
	}
	/* bulk post recv buf for shm provider */
	flags = FI_MORE;
	while (rxr_env.enable_shm_transfer && ep->rx_bufs_shm_to_post) {
		if (ep->rx_bufs_shm_to_post == 1)
			flags = 0;
		ret = rxr_ep_post_buf(ep, flags, SHM_EP);
		if (OFI_LIKELY(!ret))
			ep->rx_bufs_shm_to_post--;
		else
			return ret;
	}

	return 0;
}

static inline int rxr_ep_send_queued_pkts(struct rxr_ep *ep,
					  struct dlist_entry *pkts)
{
	struct dlist_entry *tmp;
	struct rxr_pkt_entry *pkt_entry;
	int ret;

	dlist_foreach_container_safe(pkts, struct rxr_pkt_entry,
				     pkt_entry, entry, tmp) {
		if (rxr_env.enable_shm_transfer &&
				rxr_ep_get_peer(ep, pkt_entry->addr)->is_local) {
			dlist_remove(&pkt_entry->entry);
			continue;
		}
		ret = rxr_ep_send_pkt(ep, pkt_entry, pkt_entry->addr);
		if (ret)
			return ret;
		dlist_remove(&pkt_entry->entry);
	}
	return 0;
}

static inline void rxr_ep_check_available_data_bufs_timer(struct rxr_ep *ep)
{
	if (OFI_LIKELY(ep->available_data_bufs != 0))
		return;

	if (ofi_gettime_us() - ep->available_data_bufs_ts >=
	    RXR_AVAILABLE_DATA_BUFS_TIMEOUT) {
		ep->available_data_bufs = rxr_get_rx_pool_chunk_cnt(ep);
		ep->available_data_bufs_ts = 0;
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
			"Reset available buffers for large message receives\n");
	}
}

static inline void rxr_ep_check_peer_backoff_timer(struct rxr_ep *ep)
{
	struct rxr_peer *peer;
	struct dlist_entry *tmp;

	if (OFI_LIKELY(dlist_empty(&ep->peer_backoff_list)))
		return;

	dlist_foreach_container_safe(&ep->peer_backoff_list, struct rxr_peer,
				     peer, rnr_entry, tmp) {
		peer->rnr_state &= ~RXR_PEER_BACKED_OFF;
		if (!rxr_peer_timeout_expired(ep, peer, ofi_gettime_us()))
			continue;
		peer->rnr_state = 0;
		dlist_remove(&peer->rnr_entry);
	}
}

static inline void rxr_ep_poll_cq(struct rxr_ep *ep,
				  struct fid_cq *cq,
				  size_t cqe_to_process,
				  bool is_shm_cq)
{
	struct fi_cq_data_entry cq_entry;
	fi_addr_t src_addr;
	ssize_t ret;
	int i;

	VALGRIND_MAKE_MEM_DEFINED(&cq_entry, sizeof(struct fi_cq_data_entry));

	for (i = 0; i < cqe_to_process; i++) {
		ret = fi_cq_readfrom(cq, &cq_entry, 1, &src_addr);

		if (ret == -FI_EAGAIN)
			return;

		if (OFI_UNLIKELY(ret < 0)) {
			if (rxr_cq_handle_cq_error(ep, ret))
				assert(0 &&
				       "error writing error cq entry after reading from cq");
			rxr_ep_bulk_post_recv(ep);
			return;
		}

		if (OFI_UNLIKELY(ret == 0))
			return;

		if (is_shm_cq && (cq_entry.flags & FI_REMOTE_CQ_DATA)) {
			rxr_cq_handle_shm_rma_write_data(ep, &cq_entry, src_addr);
		} else if (cq_entry.flags & (FI_SEND | FI_READ | FI_WRITE)) {
#if ENABLE_DEBUG
			if (!is_shm_cq)
				ep->send_comps++;
#endif
			rxr_cq_handle_pkt_send_completion(ep, &cq_entry);
		} else if (cq_entry.flags & (FI_RECV | FI_REMOTE_CQ_DATA)) {
			rxr_cq_handle_pkt_recv_completion(ep, &cq_entry, src_addr);
#if ENABLE_DEBUG
			if (!is_shm_cq)
				ep->recv_comps++;
#endif
		} else {
			FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
				"Unhandled cq type\n");
			assert(0 && "Unhandled cq type");
		}
	}
}

void rxr_ep_progress_internal(struct rxr_ep *ep)
{
	struct rxr_rx_entry *rx_entry;
	struct rxr_tx_entry *tx_entry;
	struct dlist_entry *tmp;
	ssize_t ret;

	rxr_ep_check_available_data_bufs_timer(ep);

	// Poll the EFA completion queue
	rxr_ep_poll_cq(ep, ep->rdm_cq, rxr_env.efa_cq_read_size, 0);

	// Poll the SHM completion queue if enabled
	if (rxr_env.enable_shm_transfer)
		rxr_ep_poll_cq(ep, ep->shm_cq, rxr_env.shm_cq_read_size, 1);

	ret = rxr_ep_bulk_post_recv(ep);

	if (OFI_UNLIKELY(ret)) {
		if (rxr_cq_handle_cq_error(ep, ret))
			assert(0 &&
			       "error writing error cq entry after failed post recv");
		return;
	}

	rxr_ep_check_peer_backoff_timer(ep);

	/*
	 * Send any queued RTS/CTS packets.
	 * Send any queued large message RMA Read and EOR for shm
	 */
	dlist_foreach_container_safe(&ep->rx_entry_queued_list,
				     struct rxr_rx_entry,
				     rx_entry, queued_entry, tmp) {
		if (rx_entry->state == RXR_RX_QUEUED_CTRL)
			ret = rxr_ep_post_ctrl(ep, RXR_RX_ENTRY, rx_entry,
					       rx_entry->queued_ctrl.type,
					       rx_entry->queued_ctrl.inject);
		else if (rx_entry->state == RXR_RX_QUEUED_SHM_LARGE_READ)
			ret = rxr_cq_recv_shm_large_message(ep, rx_entry);
		else
			ret = rxr_ep_send_queued_pkts(ep,
						      &rx_entry->queued_pkts);
		if (ret == -FI_EAGAIN)
			break;
		if (OFI_UNLIKELY(ret))
			goto rx_err;

		dlist_remove(&rx_entry->queued_entry);
		rx_entry->state = RXR_RX_RECV;
	}

	dlist_foreach_container_safe(&ep->tx_entry_queued_list,
				     struct rxr_tx_entry,
				     tx_entry, queued_entry, tmp) {
		if (tx_entry->state == RXR_TX_QUEUED_CTRL)
			ret = rxr_ep_post_ctrl(ep, RXR_TX_ENTRY, tx_entry,
					       tx_entry->queued_ctrl.type,
					       tx_entry->queued_ctrl.inject);
		else if (tx_entry->state == RXR_TX_QUEUED_SHM_RMA)
			ret = rxr_rma_post_shm_rma(ep, tx_entry);
		else
			ret = rxr_ep_send_queued_pkts(ep,
						      &tx_entry->queued_pkts);

		if (ret == -FI_EAGAIN)
			break;
		if (OFI_UNLIKELY(ret))
			goto tx_err;

		dlist_remove(&tx_entry->queued_entry);

		if (tx_entry->state == RXR_TX_QUEUED_RTS_RNR)
			tx_entry->state = RXR_TX_RTS;
		else if (tx_entry->state == RXR_TX_QUEUED_SHM_RMA)
			tx_entry->state = RXR_TX_SHM_RMA;
		else if (tx_entry->state == RXR_TX_QUEUED_DATA_RNR) {
			tx_entry->state = RXR_TX_SEND;
			dlist_insert_tail(&tx_entry->entry,
					  &ep->tx_pending_list);
		}
	}

	/*
	 * Send data packets until window or tx queue is exhausted.
	 */
	dlist_foreach_container(&ep->tx_pending_list, struct rxr_tx_entry,
				tx_entry, entry) {
		if (tx_entry->window > 0)
			tx_entry->send_flags |= FI_MORE;
		else
			continue;

		while (tx_entry->window > 0) {
			if (ep->max_outstanding_tx - ep->tx_pending <= 1 ||
			    tx_entry->window <= ep->max_data_payload_size)
				tx_entry->send_flags &= ~FI_MORE;
			/*
			 * The core's TX queue is full so we can't do any
			 * additional work.
			 */
			if (ep->tx_pending == ep->max_outstanding_tx)
				goto out;
			ret = rxr_ep_post_data(ep, tx_entry);
			if (OFI_UNLIKELY(ret)) {
				tx_entry->send_flags &= ~FI_MORE;
				goto tx_err;
			}
		}
	}

out:
	return;
rx_err:
	if (rxr_cq_handle_rx_error(ep, rx_entry, ret))
		assert(0 &&
		       "error writing error cq entry when handling RX error");
	return;
tx_err:
	if (rxr_cq_handle_tx_error(ep, tx_entry, ret))
		assert(0 &&
		       "error writing error cq entry when handling TX error");
	return;
}

void rxr_ep_progress(struct util_ep *util_ep)
{
	struct rxr_ep *ep;

	ep = container_of(util_ep, struct rxr_ep, util_ep);

	fastlock_acquire(&ep->util_ep.lock);
	rxr_ep_progress_internal(ep);
	fastlock_release(&ep->util_ep.lock);
}

int rxr_endpoint(struct fid_domain *domain, struct fi_info *info,
		 struct fid_ep **ep, void *context)
{
	struct fi_info *rdm_info;
	struct rxr_domain *rxr_domain;
	struct rxr_ep *rxr_ep;
	struct fi_cq_attr cq_attr;
	int ret, retv;

	rxr_ep = calloc(1, sizeof(*rxr_ep));
	if (!rxr_ep)
		return -FI_ENOMEM;

	rxr_domain = container_of(domain, struct rxr_domain,
				  util_domain.domain_fid);
	memset(&cq_attr, 0, sizeof(cq_attr));
	cq_attr.format = FI_CQ_FORMAT_DATA;
	cq_attr.wait_obj = FI_WAIT_NONE;

	ret = ofi_endpoint_init(domain, &rxr_util_prov, info, &rxr_ep->util_ep,
				context, rxr_ep_progress);
	if (ret)
		goto err_free_ep;

	ret = rxr_get_lower_rdm_info(rxr_domain->util_domain.fabric->
				     fabric_fid.api_version, NULL, NULL, 0,
				     &rxr_util_prov, info, &rdm_info);
	if (ret)
		goto err_close_ofi_ep;

	rxr_reset_rx_tx_to_core(info, rdm_info);

	ret = fi_endpoint(rxr_domain->rdm_domain, rdm_info,
			  &rxr_ep->rdm_ep, rxr_ep);
	if (ret)
		goto err_free_rdm_info;

	/* Open shm provider's endpoint */
	if (rxr_env.enable_shm_transfer) {
		assert(!strcmp(shm_info->fabric_attr->name, "shm"));
		ret = fi_endpoint(rxr_domain->shm_domain, shm_info,
				  &rxr_ep->shm_ep, rxr_ep);
		if (ret)
			goto err_close_core_ep;
	}

	rxr_ep->rx_size = info->rx_attr->size;
	rxr_ep->tx_size = info->tx_attr->size;
	rxr_ep->rx_iov_limit = info->rx_attr->iov_limit;
	rxr_ep->tx_iov_limit = info->tx_attr->iov_limit;
	rxr_ep->max_outstanding_tx = rdm_info->tx_attr->size;
	rxr_ep->core_rx_size = rdm_info->rx_attr->size;
	rxr_ep->core_iov_limit = rdm_info->tx_attr->iov_limit;
	rxr_ep->core_caps = rdm_info->caps;

	cq_attr.size = MAX(rxr_ep->rx_size + rxr_ep->tx_size,
			   rxr_env.cq_size);

	if (info->tx_attr->op_flags & FI_DELIVERY_COMPLETE)
		FI_INFO(&rxr_prov, FI_LOG_CQ, "FI_DELIVERY_COMPLETE unsupported\n");

	assert(info->tx_attr->msg_order == info->rx_attr->msg_order);
	rxr_ep->msg_order = info->rx_attr->msg_order;
	rxr_ep->core_msg_order = rdm_info->rx_attr->msg_order;
	rxr_ep->core_inject_size = rdm_info->tx_attr->inject_size;
	rxr_ep->mtu_size = rdm_info->ep_attr->max_msg_size;
	if (rxr_env.mtu_size > 0 && rxr_env.mtu_size < rxr_ep->mtu_size)
		rxr_ep->mtu_size = rxr_env.mtu_size;

	if (rxr_ep->mtu_size > RXR_MTU_MAX_LIMIT)
		rxr_ep->mtu_size = RXR_MTU_MAX_LIMIT;

	rxr_ep->max_data_payload_size = rxr_ep->mtu_size - RXR_DATA_HDR_SIZE;
	/*
	 * Assume our eager message size is the largest control header size
	 * without the source address. Use that value to set the default
	 * receive release threshold.
	 */
	rxr_ep->min_multi_recv_size = rxr_ep->mtu_size - RXR_CTRL_HDR_SIZE;

	if (rxr_env.tx_queue_size > 0 &&
	    rxr_env.tx_queue_size < rxr_ep->max_outstanding_tx)
		rxr_ep->max_outstanding_tx = rxr_env.tx_queue_size;

#if ENABLE_DEBUG
	rxr_ep->sends = 0;
	rxr_ep->send_comps = 0;
	rxr_ep->failed_send_comps = 0;
	rxr_ep->recv_comps = 0;
#endif

	rxr_ep->posted_bufs_shm = 0;
	rxr_ep->rx_bufs_shm_to_post = 0;
	rxr_ep->posted_bufs_efa = 0;
	rxr_ep->rx_bufs_efa_to_post = 0;
	rxr_ep->tx_pending = 0;
	rxr_ep->available_data_bufs_ts = 0;

	ret = fi_cq_open(rxr_domain->rdm_domain, &cq_attr,
			 &rxr_ep->rdm_cq, rxr_ep);
	if (ret)
		goto err_close_shm_ep;

	ret = fi_ep_bind(rxr_ep->rdm_ep, &rxr_ep->rdm_cq->fid,
			 FI_TRANSMIT | FI_RECV);
	if (ret)
		goto err_close_core_cq;

	/* Bind ep with shm provider's cq */
	if (rxr_env.enable_shm_transfer) {
		ret = fi_cq_open(rxr_domain->shm_domain, &cq_attr,
				 &rxr_ep->shm_cq, rxr_ep);
		if (ret)
			goto err_close_core_cq;

		ret = fi_ep_bind(rxr_ep->shm_ep, &rxr_ep->shm_cq->fid,
				 FI_TRANSMIT | FI_RECV);
		if (ret)
			goto err_close_shm_cq;
	}

	ret = rxr_ep_init(rxr_ep);
	if (ret)
		goto err_close_shm_cq;

	*ep = &rxr_ep->util_ep.ep_fid;
	(*ep)->msg = &rxr_ops_msg;
	(*ep)->rma = &rxr_ops_rma;
	(*ep)->tagged = &rxr_ops_tagged;
	(*ep)->fid.ops = &rxr_ep_fi_ops;
	(*ep)->ops = &rxr_ops_ep;
	(*ep)->cm = &rxr_ep_cm;
	return 0;

err_close_shm_cq:
	if (rxr_env.enable_shm_transfer && rxr_ep->shm_cq) {
		retv = fi_close(&rxr_ep->shm_cq->fid);
		if (retv)
			FI_WARN(&rxr_prov, FI_LOG_CQ, "Unable to close shm cq: %s\n",
				fi_strerror(-retv));
	}
err_close_core_cq:
	retv = fi_close(&rxr_ep->rdm_cq->fid);
	if (retv)
		FI_WARN(&rxr_prov, FI_LOG_CQ, "Unable to close cq: %s\n",
			fi_strerror(-retv));
err_close_shm_ep:
	if (rxr_env.enable_shm_transfer && rxr_ep->shm_ep) {
		retv = fi_close(&rxr_ep->shm_ep->fid);
		if (retv)
			FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "Unable to close shm EP: %s\n",
				fi_strerror(-retv));
	}
err_close_core_ep:
	retv = fi_close(&rxr_ep->rdm_ep->fid);
	if (retv)
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL, "Unable to close EP: %s\n",
			fi_strerror(-retv));
err_free_rdm_info:
	fi_freeinfo(rdm_info);
err_close_ofi_ep:
	retv = ofi_endpoint_close(&rxr_ep->util_ep);
	if (retv)
		FI_WARN(&rxr_prov, FI_LOG_EP_CTRL,
			"Unable to close util EP: %s\n",
			fi_strerror(-retv));
err_free_ep:
	free(rxr_ep);
	return ret;
}
