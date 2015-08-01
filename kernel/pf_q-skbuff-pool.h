/***************************************************************
 *
 * (C) 2011-14 Nicola Bonelli <nicola@pfq.io>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 ****************************************************************/

#ifndef PF_Q_SKBUFF_POOL_H
#define PF_Q_SKBUFF_POOL_H

#include <pragma/diagnostic_push>
#include <linux/skbuff.h>
#include <pragma/diagnostic_pop>

#include <pf_q-global.h>
#include <pf_q-stats.h>

struct pfq_skb_pool
{
	struct sk_buff ** skbs;
	size_t size;
	size_t p_idx;
	size_t c_idx;
};



void	pfq_skb_pool_enable(bool value);
int     pfq_skb_pool_init_all(void);
int	pfq_skb_pool_free_all(void);
int	pfq_skb_pool_flush_all(void);

int	pfq_skb_pool_init (struct pfq_skb_pool *pool, size_t size);
size_t	pfq_skb_pool_free (struct pfq_skb_pool *pool);
size_t	pfq_skb_pool_flush(struct pfq_skb_pool *pool);

struct  pfq_pool_stat pfq_get_skb_pool_stats(void);


static inline
size_t __pool_next(struct pfq_skb_pool *pool, size_t i)
{
	size_t n = i + 1;
	if (n == pool->size)
		return 0;
	return n;
}


static inline
struct sk_buff *pfq_skb_pool_pop(struct pfq_skb_pool *pool)
{
	if (likely(pool->skbs)) {
		size_t c = __atomic_load_n(&pool->c_idx, __ATOMIC_RELAXED);
		size_t p = __atomic_load_n(&pool->p_idx, __ATOMIC_ACQUIRE);
		if (c != p) {
			struct sk_buff *skb = pool->skbs[c];
			if (atomic_read(&skb->users) < 2) {
				size_t n = __pool_next(pool, c);
				pool->skbs[c] = NULL;
				__atomic_store_n(&pool->c_idx, n, __ATOMIC_RELEASE);
				return skb;
			}
		}
	}

	return NULL;
}


static inline
bool pfq_skb_pool_push(struct pfq_skb_pool *pool, struct sk_buff *skb)
{
	if (likely(pool->skbs)) {

		size_t p = __atomic_load_n(&pool->p_idx, __ATOMIC_RELAXED);
		size_t c = __atomic_load_n(&pool->c_idx, __ATOMIC_ACQUIRE);
		size_t n = __pool_next(pool, p);
		if (n != c) {
			BUG_ON(pool->skbs[p] != NULL);
			pool->skbs[p] = skb;
			__atomic_store_n(&pool->p_idx, n, __ATOMIC_RELEASE);
			return true;
		}
	}

        SPARSE_INC(&memory_stats.os_free);
	kfree_skb(skb);
	return false;
}

#endif /* PF_Q_SKBUFF_POOL_H */
