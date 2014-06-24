#ifndef UB_SKBS_H
#define UB_SKBS_H

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/udp.h>

#include <prot/memcached.h>

#define UB_SKB_HEADER_OVERHEAD \
	sizeof(struct ethhdr) + \
	sizeof(struct iphdr)  + \
	sizeof(struct udphdr) + \
	sizeof(struct memcache_udp_header)

static inline 
struct sk_buff* ub_skb_set_up(size_t datasize)
{
	struct sk_buff* skb = alloc_skb(datasize + UB_SKB_HEADER_OVERHEAD, GFP_ATOMIC);
	
	if (unlikely(!skb))
	{
		printk("Dazed and confused. Couldn't allocate an skb? Are you out of memory?\n");
		return NULL;
	}

	// Reserve space at the front for the header overhead
	skb_reserve(skb, UB_SKB_HEADER_OVERHEAD);

	return skb;
}

static inline
unsigned char* ub_push_data_to_skb
	(struct sk_buff* skb, unsigned char* buf, size_t len_buf)
{
	unsigned char* data = skb_put(skb, len_buf);
	memcpy(data, buf, len_buf);
	return data;
}

#endif
