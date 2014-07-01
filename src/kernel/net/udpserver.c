#include <abstract.h>
#include <net/udpserver.h>
#include <request.h>
#include <kernel/net/udpserver_low.h>
#include <kernel/net/udpserver_send.h>
#include <unbuckle.h>

#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/udp.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <net/ip.h>
#include <net/net_namespace.h>
#include <net/sock.h>

static __u16 __bitwise ip_id = 1;

#define NET_HDR_OVERHEAD \
	sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr) + 2

static struct net_device* dev  = NULL;

int udpserver_init_sendbuffers(struct request_state* req)
{
	req->len_sendbuf = UDP_SEND_BUFFER;
	req->sendbuf = (char*) ALLOCMEM(req->len_sendbuf, GFP_KERNEL);
	req->sendbuf_cur = req->sendbuf;
	
	if (!req->sendbuf)
		return -1;

	if (ksize(req->sendbuf) < req->len_sendbuf)
	{
		printk(KERN_WARNING 
			"[Unbuckle] Asked for a transmit buffer of %lu, but got %lu.\n",
			req->len_sendbuf, ksize(req->sendbuf)
		);
		req->len_sendbuf = ksize(req->sendbuf);
	}

	return 0;
}

void udpserver_free_sendbuffers(struct request_state* req)
{
	if (req->sendbuf)
	{
		FREEMEM(req->sendbuf);
		req->sendbuf = NULL;
		req->sendbuf_cur = NULL;
		req->len_sendbuf = 0;
		req->len_sendbuf_cur = 0;
	}
	return;
}

/* declared inline to suppress unused warning -- want to keep this code around 
    for fastpath stuff later */
static inline 
int udpserver_sendmsg
	(struct request_state* req, struct msghdr* msg, size_t len_data)
{
	// Sends a message back to the sender of the active request
	int err;
	
#ifdef DEBUG
	PRINT("[Unbuckle] Sending a message.\n");
#endif

	err = kernel_sendmsg(udpserver->sock, msg, (struct kvec*) msg->msg_iov, 
		msg->msg_iovlen, len_data);

#ifdef DEBUG
	if (err < 0)
	{
		PRINTARGS("[Unbuckle] encountered error %d writing a UDP response\n", err);
	}
#endif
	
	return err;
}

static inline 
void set_up_udp_header(struct request_state* req, struct sk_buff* skb)
{
	__be16 dstport;
	struct udphdr* udp;

	dstport = req->udph->source;

	udp = (struct udphdr*) skb_push(skb, sizeof(struct udphdr));

	/* Don't compute a UDP checksum -- what's the point? */
	udp->check = 0;
	udp->source = htons(11211);
	udp->dest = dstport;
	udp->len = htons(skb->len);

	/* update the checksum */
	skb->csum = csum_partial((char*) udp, sizeof(struct udphdr), skb->csum);

	return;
}

static inline
void set_up_ip_header(struct request_state* req, struct sk_buff* skb)
{
	struct iphdr* ip;

	ip = (struct iphdr*) skb_push(skb, sizeof(struct iphdr));
	ip->version = 4;
	ip->ihl = 5; /* no options */
	ip->tos = 0;
	ip->tot_len = htons(skb->len);
	ip->frag_off = 0;
	ip->id = htons(ip_id++);
	ip->ttl = 64;
	ip->protocol = IPPROTO_UDP;
	
	ip->saddr = req->daddr;
	ip->daddr = req->saddr;

	/* checksum */
	ip_send_check(ip);

	skb_reset_network_header(skb);

	return;
}

static inline void copy_mac(unsigned char *from, unsigned char *to, size_t len)
{
	int i;

	if (unlikely(!from || !to))
		return;
	if (unlikely(len > ETH_ALEN))
		len = ETH_ALEN;
	
	for (i = 0; i < ETH_ALEN; i++)
		to[i] = from[i];
	return;
}

static inline
int set_up_eth_header(struct request_state* req, struct sk_buff* skb)
{
	int err;

	/* Ask the driver to add the hardware layer address. We are sending an IP 
	   packet which leads to the choice of ETH_P_IP. dev_addr refers to the 
	   length of the hardware addresses on this system (the MAC addresses which
	   are passed in as arrays.
	
	   I *think* this calls eth_header in net/ethernet/eth.c to actually build.
	   The return value is the number of bytes. len should be packet length
	   in that case. It would make sense since the signature of that method
	   effectively matches what is passed in to dev_hard_header. There's a layer
	   of indirection around the header_ops struct which contains pointers to
	   different functions for manipulating different headers depending on the
	   device struct dev. */
	unsigned char mac_send[ETH_ALEN];
	struct net_device* devsend = req->devrcv;

	copy_mac(req->mac_src, mac_send, ETH_ALEN);
	err = dev_hard_header(skb, devsend, ETH_P_IP, mac_send, devsend->dev_addr, skb->len);
	if (!err)
		return err;

	/* make sure the pointers to the ethernet header are present in the skb 
	   as well as length info */
	skb_reset_mac_header(skb);
	skb_reset_mac_len(skb);

	skb->dev = devsend;
	skb->protocol = ETH_P_IP;

	return 0;
}

static inline
void copy_data_to_skb(struct request_state* req, struct sk_buff* skb)
{
	unsigned char* data = skb_put(skb, req->len_sendbuf_cur);
	memcpy(data, req->sendbuf, req->len_sendbuf_cur);
	/* 0 corresponds to no checksum existing to include in this computation */
	skb->csum = csum_partial(data, req->len_sendbuf_cur, 0x0);
	return;
}

static inline
int ship_skb(struct sk_buff* skb)
{
	int err;

	err = dev_queue_xmit(skb);
	err = net_xmit_eval(err);
	
	if (err)
		schedule();

	return err;
}

/* this is deprecated -- it isn't called anywhere because we don't set up skbs 
   when packets are ready to go, but a priori when they are added to the hash
   table */
static inline 
struct sk_buff* set_up_skb(struct request_state* req)
{
	/* TODO: this is probably bad to keep allocing here unless it comes from slabs? 
	         Doing this as a proof of concept for later optimisation.
	         "Premature optimisation is the root of all evil" :-) */
	/* should allocate 42 bytes for additional headers to add, but going for 64 to
	    be safe */
	/* process for creating and pushing to SKBs illustrated in all its wonderful detail
	   at http://vger.kernel.org/~davem/skb_data.html */
	struct sk_buff* skb = alloc_skb(req->len_sendbuf_cur + NET_HDR_OVERHEAD, GFP_ATOMIC);

	if (!skb)
	{
		printk("Didn't get an skb?\n");
		return NULL;
	}

	/* reserve space for the various headers; note the Ethernet header has a special
	   call afterwards to align it back on a 4-byte boundary on most systems (typical
	   size is 14 bytes */
	skb_reserve(skb, NET_HDR_OVERHEAD);
	
	copy_data_to_skb(req, skb);
	set_up_udp_header(req, skb);
	set_up_ip_header(req, skb);
	set_up_eth_header(req, skb);

	/* TODO: do something with errors which can be returned from eth header and
	         shipping and deal with them gracefully. At the moment the calls above
	         return the errors but they are ignored here. */

	return skb;
}

int udpserver_sendall(struct request_state* req)
{
	struct sk_buff *skb;

	if (unlikely(!req)) 
	{
		printk("No request?\n");
		return -1;
	}

	skb = req->skb_tx;

	/* get the net_device from the udp server's sock if we haven't already set it up
	   in global state*/
	if (!dev)
	{
		struct net* ns;
		/* enumerate network namespaces until we find the one with the interface 
		   we are interested in (eth0 here) */
		rcu_read_lock();
		for_each_net_rcu(ns)
		{
			dev = dev_get_by_name(ns, "eth1.2"); // 10.10.0.x
			if (dev)
				break;
		}
		rcu_read_unlock();

		if (!dev)
		{
			printk("Uh oh! Cannot find the network device in any class\n");
			return -1;
		}
	}
	
	/* Set up the pointer to the UDP headers before adding them */
	req->udpheaders = (struct memcache_udp_header*) 
		skb_push(skb, sizeof(struct memcache_udp_header));
	add_udp_headers(req);
	
	skb->csum = csum_partial(skb->data, skb->len, 0x0);
	
	set_up_udp_header(req, skb);
	set_up_ip_header(req, skb);
	set_up_eth_header(req, skb);

	if (skb)
	{
		struct sk_buff_head* q = &ub_tx_queues[smp_processor_id()];
		skb_queue_tail(q, skb);
		return 0;
	}
	else
	{
		kfree_skb(skb);
		return -1;
	}
}

int do_kernel_rx_worker(struct request_state* req)
{
	struct sk_buff_head* q = &ub_rx_queues[smp_processor_id() - 2];
	printk("In kernel_rx_worker, SMP id %d\n", smp_processor_id());
	
	/* loop waiting for something to do */
	while (!kthread_should_stop() && ub_sys_running)
	{
		struct sk_buff* skb;
		struct ethhdr  *eth;
		struct iphdr*   iph;
		struct udphdr*  udph;

		if (skb_queue_empty(q))
		{
			schedule();
			continue;
		}

		/* got some work to do */
		skb = skb_dequeue(q);

		iph = ip_hdr(skb);
		udph = (struct udphdr*) ((char*) iph + iph->ihl * 4);

		skb_pull(skb, iph->ihl * 4);

		req->skb_rx = skb;
		req->udph = udph;
		req->iph = iph;
		
		req->recvbuf_cur = req->recvbuf;
		req->len_rdata = ntohs(req->udph->len) - sizeof(struct udphdr);

		if (unlikely(req->len_rdata > skb->len))
		{
			printk(KERN_WARNING "UDP length seems to be more than SKB length?\n");
			goto drop;
		}
		skb_copy_bits(skb, sizeof(struct udphdr), req->recvbuf, req->len_rdata);
		
		req->saddr = iph->saddr;
		req->daddr = iph->daddr;
		
		eth = (struct ethhdr*) (((char*)iph) - sizeof(struct ethhdr));
		copy_mac(eth->h_source, req->mac_src, ETH_ALEN);		 
		
		req->devrcv = skb->dev;	
		
		process_fastpath(req);
		
		/* Fallthrough to make sure the SKB gets freed. We are done with it */
drop:
		kfree_skb(skb);
		continue;
	}

	return 0;
}


