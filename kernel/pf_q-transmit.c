/*
 * Copyright (c) 2014 Bonelli Nicola <nicola@pfq.io>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <pragma/diagnostic_push>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <pragma/diagnostic_pop>

#include <pf_q-thread.h>
#include <pf_q-transmit.h>
#include <pf_q-memory.h>
#include <pf_q-sock.h>
#include <pf_q-macro.h>
#include <pf_q-global.h>
#include <pf_q-GC.h>
#include <pf_q-printk.h>

static inline int
__pfq_xmit(struct sk_buff *skb, struct net_device *dev, struct netdev_queue *txq, int xmit_more);


static inline int
__pfq_dev_cap_txqueue(struct net_device *dev, int hw_queue)
{
	if (unlikely(hw_queue >= dev->real_num_tx_queues))
		return 0;

	return hw_queue;
}


#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,13,0))
static u16 __pfq_pick_tx_default(struct net_device *dev, struct sk_buff *skb)
{
	return 0;
}
#endif


/* select the right tx hw queue, and fix it (-1 means any queue) */

static struct netdev_queue *
pfq_pick_tx(struct net_device *dev, struct sk_buff *skb, int *hw_queue)
{
	if (dev->real_num_tx_queues != 1 && *hw_queue == -1) {

		const struct net_device_ops *ops = dev->netdev_ops;

		*hw_queue = ops->ndo_select_queue ?
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0))
			ops->ndo_select_queue(dev, skb)
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(3,13,0))
			ops->ndo_select_queue(dev, skb, NULL)
#else
			ops->ndo_select_queue(dev, skb, NULL, __pfq_pick_tx_default)
#endif
			: 0;
	}

	*hw_queue = __pfq_dev_cap_txqueue(dev, *hw_queue);

	return netdev_get_tx_queue(dev, *hw_queue);
}


static inline
bool is_kthread_should_stop(void)
{
	return (current->flags & PF_KTHREAD) && kthread_should_stop();
}


static inline
bool giveup_tx_process(void)
{
	return signal_pending(current) || is_kthread_should_stop();
}


/*
 * full_batch_xmit:
 *
 * return  >= 0 -> OK	 ( ret = number of skb sent)
 *         < 0  -> EINT  (~ret = number of skb sent)
 *
 * batch is consumed and if interrupted, it returns the packets
 * unsent is in the batch.
 */

static int
full_batch_xmit(struct local_data *local, struct pfq_skbuff_batch *skbs, struct net_device *dev, int hw_queue)
{
	int total = 0;

	while (pfq_skbuff_batch_len(skbs)) {

		struct sk_buff *skb;
		int sent, i;

		/* check for signal pending or if the calling kthread should stop */

		if (giveup_tx_process())
			return ~total;

		/* get the whole batch */

		for_each_skbuff(skbs, skb, i)
			skb_get(skb);

		/* attempt to transmit it */

		sent = pfq_batch_xmit(skbs, dev, hw_queue);
		if (sent == 0) {
			/* if no packets are sent */
			pfq_relax();
		}
                else {
			total += sent;

			/* free transmitted skb */
			for_each_skbuff_upto(sent, skbs, skb, i)
				pfq_kfree_skb_pool(skb, &local->tx_pool);

			/* remove freed pointers from the batch */
			pfq_skbuff_batch_drop_n(skbs, sent);
		}
	}

	return total;
}


static inline
bool transmission_required(struct pfq_skbuff_batch *q, ktime_t now, uint64_t ts)
{
	size_t len = pfq_skbuff_batch_len(q);
	return len == batch_len || ((len > 0) && (ts > ktime_to_ns(now)));
}


static inline
ktime_t wait_until(uint64_t ts)
{
	ktime_t now;
	do
	{
		now = ktime_get_real();
		if (giveup_tx_process())
			return now;
	}
	while (ktime_to_ns(now) < ts
	       && (pfq_relax(), true));

	return now;
}


static inline
int swap_tx_queue_and_wait(struct pfq_tx_queue *txs, int cpu, int *index)
{
	if (cpu != Q_NO_KTHREAD) {
		*index = __atomic_add_fetch(&txs->cons, 1, __ATOMIC_RELAXED);
		while (*index != __atomic_load_n(&txs->prod, __ATOMIC_RELAXED))
		{
			pfq_relax();
			if (giveup_tx_process())
				return -EINTR;
		}
	}
	else {
		*index = __atomic_add_fetch(&txs->cons, 1, __ATOMIC_RELAXED);
		__atomic_store_n(&txs->prod, 1, __ATOMIC_RELAXED);
	}

	return 0;
}


static inline
bool traverse_tx_queue(char *ptr, char *begin, char *end, int idx)
{
	struct pfq_pkthdr_tx *hdr = (struct pfq_pkthdr_tx *)ptr;

#ifdef PFQ_DEBUG
	if (ptr < begin || ptr >= end) {
		printk(KERN_INFO "[PFQ] BUG: queue[%d] ptr overflow: %p: [%p,%p]\n", idx,
		       ptr, begin, end);
		return false;
	}

	if (hdr->len > 2048) {
		printk(KERN_INFO "[PFQ] BUG: queue[%d]@offset=%zu bad hdr->len: %zu@%p [%p,%p]\n", idx,
		       ptr-begin, hdr->len, hdr, begin, end);
		return false;
	}

	return hdr->len != 0;
#else
	return ptr < end && (hdr->len !=0);
#endif

}


int
__pfq_queue_xmit(size_t idx, struct pfq_tx_opt *to, struct net_device *dev, int cpu, int node)
{
	size_t len, disc = 0, tot_sent = 0;
	struct pfq_skbuff_short_batch skbs;
	struct pfq_tx_queue *txs;
	struct local_data *local;
	int hw_queue, swap;
        int err;

	char *ptr, *begin, *end;
        ktime_t now;
        uint64_t last_ts;

	/* get the Tx queue */

	txs = pfq_get_tx_queue(to, idx);

	/* get the netdev_queue for transmission */

	hw_queue = to->queue[idx].hw_queue;

	/* get local cpu data */

	local = this_cpu_ptr(cpu_data);

	/* swap the soft Tx queue */

	if ((err = swap_tx_queue_and_wait(txs, cpu, &swap)) < 0)
		return err;

	/* increment the swap index of the current queue */

	swap++;

        /* initialize pointer to the current transmit queue */

	begin = to->queue[idx].base_addr + (swap & 1) * txs->size;
        end = begin + txs->size;

	/* initialize the batch */

	pfq_skbuff_batch_init(SKBUFF_BATCH_ADDR(skbs));

	/* Tx loop */

	now = ktime_get_real();
	ptr = begin;

	while(traverse_tx_queue(ptr, begin, end, idx))
	{
		struct pfq_pkthdr_tx * hdr = (struct pfq_pkthdr_tx *)ptr;
		struct sk_buff *skb;

		/* get the tstamp of this packet */

		last_ts = hdr->nsec;

		/* if the batch is full or the last packet is to be transmitted,
		 * do it now. */

		if (transmission_required(SKBUFF_BATCH_ADDR(skbs), now, last_ts)) {
			int sent = full_batch_xmit(local, SKBUFF_BATCH_ADDR(skbs), dev, hw_queue);
			tot_sent += (sent >= 0 ? sent : ~sent);
			if (sent < 0)
				break;
		}

		/* wait until the ts */

		if (last_ts > ktime_to_ns(now))
			now = wait_until(last_ts);

		/* allocate a packet */

		skb = pfq_tx_alloc_skb(max_len, GFP_KERNEL, node);
		if (unlikely(skb == NULL)) {
			printk(KERN_INFO "[PFQ] Tx could not allocate an skb!\n");
			break;
		}

		/* fill the skb */

		len = min_t(size_t, hdr->len, max_len);

		skb_reset_tail_pointer(skb);
		skb->dev = dev;
		skb->len = 0;

		__skb_put(skb, len);

		skb_set_queue_mapping(skb, hw_queue);

		/* copy bytes in the socket buffer */

		skb_copy_to_linear_data(skb, hdr+1, len < 64 ? 64 : len);

		/* push the packet into the batch */

		pfq_skbuff_short_batch_push(SKBUFF_BATCH_ADDR(skbs), skb);

		/* move ptr to the next packet */

		ptr += sizeof(struct pfq_pkthdr_tx) + ALIGN(hdr->len, 8);
		hdr = (struct pfq_pkthdr_tx *)ptr;
	}

	/* flush the current batch */

	if (pfq_skbuff_batch_len(SKBUFF_BATCH_ADDR(skbs))) {
		int sent = full_batch_xmit(local, SKBUFF_BATCH_ADDR(skbs), dev, hw_queue);
		tot_sent += (sent >= 0 ? sent : ~sent);
	}

	/* free packet unsent from the last batch */

	disc = pfq_skbuff_batch_len(SKBUFF_BATCH_ADDR(skbs));
	if (disc) {
		struct sk_buff *skb;
		size_t n;
		for_each_skbuff(SKBUFF_BATCH_ADDR(skbs), skb, n)
			pfq_kfree_skb_pool(skb, &local->tx_pool);
	}

	/* disc takes into account for the packets left in the shared queue */

	while(traverse_tx_queue(ptr, begin, end, idx))
	{
		struct pfq_pkthdr_tx *hdr = (struct pfq_pkthdr_tx *)ptr;
		ptr += sizeof(struct pfq_pkthdr_tx) + ALIGN(hdr->len, 8);
		disc++;
	}

	/* update stats */

	if (cpu != Q_NO_KTHREAD) {
		__sparse_add(&to->stats.disc, disc, cpu);
		__sparse_add(&global_stats.disc, disc, cpu);
		__sparse_add(&to->stats.sent, tot_sent, cpu);
		__sparse_add(&global_stats.sent, tot_sent, cpu);
	}
	else {
		sparse_add(&to->stats.disc, disc);
		sparse_add(&global_stats.disc, disc);
		sparse_add(&to->stats.sent, tot_sent);
		sparse_add(&global_stats.sent, tot_sent);
	}

	/* clear the queue */
	{
		struct pfq_pkthdr_tx *hdr = (struct pfq_pkthdr_tx *)begin;
		hdr->len = 0;
	}

	return tot_sent;
}


/*
 * flush the soft queue
 */

int
pfq_queue_flush(struct pfq_sock *so, int index)
{
	struct net_device *dev;

	if (so->tx_opt.queue[index].task) {
		return 0;
	}

	dev = dev_get_by_index(sock_net(&so->sk), so->tx_opt.queue[index].if_index);
	if (!dev) {
		printk(KERN_INFO "[PFQ] pfq_queue_flush[%d]: bad if_index:%d!\n",
		       index, so->tx_opt.queue[index].if_index);
		return -EPERM;
	}

	pfq_queue_xmit(index, &so->tx_opt, dev);
	dev_put(dev);
	return 0;
}


static inline int
__pfq_xmit(struct sk_buff *skb, struct net_device *dev, struct netdev_queue *txq, int xmit_more)
{
	int rc = -ENOMEM;

#ifndef PFQ_USE_XMIT_MORE
	xmit_more = 0;
#endif

#if(LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0))
	skb->xmit_more = xmit_more;
#else
	skb->mark = xmit_more;
#endif

	skb_reset_mac_header(skb);

	if (dev->flags & IFF_UP) {

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,2,0))
		if (!netif_tx_queue_stopped(txq))
#else
		if (!netif_xmit_stopped(txq))
#endif
		{
			rc = dev->netdev_ops->ndo_start_xmit(skb, dev);
			if (dev_xmit_complete(rc))
				goto out;
		}
	}

        SPARSE_INC(&memory_stats.os_free);
	kfree_skb(skb);
	return -ENETDOWN;
out:
	return rc;
}


int
pfq_xmit(struct sk_buff *skb, struct net_device *dev, int hw_queue, int more)
{
	struct netdev_queue *txq;
	int ret = 0;

	/* get txq and fix the hw_queue for this batch.
	 *
	 * note: in case the hw_queue is set to any-queue (-1), the driver along the first skb
	 * select the queue */

	txq = pfq_pick_tx(dev, skb, &hw_queue);

	skb_set_queue_mapping(skb, hw_queue);

	__netif_tx_lock_bh(txq);

	ret = __pfq_xmit(skb, dev, txq, more);

	__netif_tx_unlock_bh(txq);

	return ret;
}


int
pfq_batch_xmit(struct pfq_skbuff_batch *skbs, struct net_device *dev, int hw_queue)
{
	struct netdev_queue *txq;
	struct sk_buff *skb;
	int n, ret = 0;
	size_t last;

	/* get txq and fix the hw_queue for this batch.
	 *
	 * note: in case the hw_queue is set to any-queue (-1), the driver along the first skb
	 * select the queue */

	txq = pfq_pick_tx(dev, skbs->queue[0], &hw_queue);

	last = pfq_skbuff_batch_len(skbs) - 1;

	__netif_tx_lock_bh(txq);

	for_each_skbuff(skbs, skb, n)
	{
		skb_set_queue_mapping(skb, hw_queue);

		if (__pfq_xmit(skb, dev, txq, n != last) == NETDEV_TX_OK)
			++ret;
		else
			goto intr;
	}

	__netif_tx_unlock_bh(txq);
	return ret;

intr:
	for_each_skbuff_from(ret + 1, skbs, skb, n) {
		SPARSE_INC(&memory_stats.os_free);
		kfree_skb(skb);
	}

	__netif_tx_unlock_bh(txq);
	return ret;
}


int
pfq_batch_xmit_by_mask(struct pfq_skbuff_batch *skbs, unsigned long long mask, struct net_device *dev, int hw_queue)
{
	struct netdev_queue *txq;
	struct sk_buff *skb;
	int n, ret = 0;

	/* get txq and fix the hw_queue for this batch.
	 *
	 * note: in case the hw_queue is set to any-queue (-1), the driver along the first skb
	 * select the queue */

	txq = pfq_pick_tx(dev, skbs->queue[0], &hw_queue);

	__netif_tx_lock_bh(txq);

	for_each_skbuff_bitmask(skbs, mask, skb, n)
	{
		skb_set_queue_mapping(skb, hw_queue);

		if (__pfq_xmit(skb, dev, txq, 0) == NETDEV_TX_OK)
			++ret;
		else
			goto intr;
	}

	__netif_tx_unlock_bh(txq);
	return ret;

intr:
	for_each_skbuff_from(ret + 1, skbs, skb, n) {
		SPARSE_INC(&memory_stats.os_free);
		kfree_skb(skb);
	}

	__netif_tx_unlock_bh(txq);
	return ret;
}


int
pfq_lazy_xmit(struct gc_buff buff, struct net_device *dev, int hw_queue)
{
	struct gc_log *log = PFQ_CB(buff.skb)->log;

	if (log->num_devs >= Q_GC_LOG_QUEUE_LEN) {
		if (printk_ratelimit())
			printk(KERN_INFO "[PFQ] bridge %s: too many annotation!\n", dev->name);
		return 0;
	}

	skb_set_queue_mapping(buff.skb, hw_queue);
	log->dev[log->num_devs++] = dev;
	log->xmit_todo++;

	return 1;
}


int
pfq_batch_lazy_xmit(struct gc_queue_buff *queue, struct net_device *dev, int hw_queue)
{
	struct gc_buff buff;
	int i, n = 0;

	for_each_gcbuff(queue, buff, i)
	{
		if (pfq_lazy_xmit(buff, dev, hw_queue))
			++n;
	}

	return n;
}


int
pfq_batch_lazy_xmit_by_mask(struct gc_queue_buff *queue, unsigned long long mask, struct net_device *dev, int hw_queue)
{
	struct gc_buff buff;
	int i, n = 0;

	for_each_gcbuff_bitmask(queue, mask, buff, i)
	{
		if (pfq_lazy_xmit(buff, dev, hw_queue))
			++n;
	}

	return n;
}


size_t
pfq_lazy_xmit_exec(struct gc_data *gc, struct lazy_fwd_targets const *ts)
{
	struct netdev_queue *txq;
	struct net_device *dev;
	struct sk_buff *skb;
        size_t sent = 0;
	size_t n, i;
	int queue;

	/* for each net_device... */

	for(n = 0; n < ts->num; n++)
	{
		size_t sent_dev = 0;

		dev = ts->dev[n];
		txq = NULL;

		/* scan the list of skbs, and forward them in batch fashion */

		for(i = 0; i < gc->pool.len; i++)
		{
			struct gc_log *log;
                        size_t j, num;

			/* select the packet */

			skb = gc->pool.queue[i].skb;
                        log = &gc->log[i];
			num = gc_count_dev_in_log(dev, log);

			if (num == 0)
				continue;

			/* the first packet for this dev determines the hw queue */

			if (!txq) {
				queue = skb->queue_mapping;
				txq = pfq_pick_tx(dev, skb, &queue);
				__netif_tx_lock_bh(txq);
			}

			/* forward this skb `num` times */

			for (j = 0; j < num; j++)
			{
				const int xmit_more = ++sent_dev != ts->cnt[n];
				const bool to_clone = log->to_kernel || log->xmit_todo-- > 1;

				struct sk_buff *nskb = to_clone ? skb_clone(skb, GFP_ATOMIC) : skb_get(skb);
				if (nskb) {
					if (__pfq_xmit(nskb, dev, txq, xmit_more) == NETDEV_TX_OK)
						sent++;
					else
						sparse_inc(&global_stats.abrt);
				}
				else {
					sparse_inc(&global_stats.abrt);
				}
			}
		}

		if (txq)
			__netif_tx_unlock_bh(txq);
	}

	return sent;
}

