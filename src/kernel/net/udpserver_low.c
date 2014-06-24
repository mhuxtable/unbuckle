#include <kernel/locks.h>
#include <kernel/net/udpserver_low.h>
#include <net/udpserver.h>
#include <unbuckle.h>

#include <linux/cpumask.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/spinlock.h>
#include <linux/udp.h>
#include <linux/workqueue.h>

struct ub_bh_work
{
	struct sk_buff* skb;
	struct iphdr* iph;
	struct udphdr* udph;
	struct work_struct work;
};

//static struct workqueue_struct* wq;
static volatile int thread_to_use = 0;
struct sk_buff_head ub_rx_queues[MAX_WORKERS];
static DEFINE_SPINLOCK(irq_lock);

void
ub_udp_rcv(struct work_struct* work)
{
	struct ub_bh_work* wrk = container_of(work, struct ub_bh_work, work);
	struct request_state req;
	get_cpu();

	req.skb_rx = wrk->skb;
	req.udph = wrk->udph;
	req.iph = wrk->iph;
	
	skb_pull(req.skb_rx, req.iph->ihl * 4);

	req.recvbuf = kmalloc(ntohs(req.udph->len), GFP_ATOMIC);
	req.recvbuf_cur = req.recvbuf;
	req.len_rdata = ntohs(req.udph->len) - sizeof(struct udphdr);

	if (unlikely(req.len_rdata > req.skb_rx->len))
	{
		printk(KERN_WARNING "UDP length seems to be more than SKB length?\n");
		goto drop;
	}
	skb_copy_bits(req.skb_rx, sizeof(struct udphdr), req.recvbuf, req.len_rdata);

	/* We don't want to bin the data from the IP and UDP headers, because we can
	   use their data to form the reply message. So need to do some yucky pointer
	   arithmetic to find the start of the data we are interested in. 
	   
	   We don't bother computing UDP headers here (yet) (TODO???) */
	
	/* Yucky coarse locking for now but don't prematurely optimise... TODO */
	process_fastpath(&req);
	
	/* Fallthrough to make sure the SKB gets freed. We are done with it */
drop:
	kfree(req.recvbuf);
	kfree(wrk);
	kfree_skb(req.skb_rx);
	put_cpu();
	return;
}

unsigned int
ub_udpserver_nethook_callback(
	unsigned int hooknum,
	struct sk_buff* skb,
	const struct net_device* in,
	const struct net_device* out,
	int (*okfn)(struct sk_buff*))
{
	//struct ub_bh_work* wrk = kmalloc(sizeof(struct ub_bh_work), GFP_ATOMIC);
	struct iphdr*  iph;
	struct udphdr* udp;
	unsigned long flags;
	
	/* Check the packet is UDP */
	iph = ip_hdr(skb);
	if (iph->protocol != IPPROTO_UDP)
		return NF_ACCEPT;
	
	if (unlikely(!pskb_may_pull(skb, sizeof(struct udphdr))))
	{
		/* Seems to be UDP but not have a full UDP header? Accepting here for 
		   safety as this will surely be handled in the main receive path and 
		   the packet discarded, but don't want to be responsible for screwing
		   up someone's network stack by arbitrarily dropping their UDP packets :) */
		printk("No space to pull UDP header\n");
		return NF_ACCEPT;
	}
	
	udp = (struct udphdr*) ((char*) ip_hdr(skb) + iph->ihl * 4);
	
	/* Catch packets not for us early and also don't process anything larger than 
	   one MTU at the moment (might be able to at this level of the stack as we 
	   are called after IP defragmentation occurs */
	if (ntohs(udp->dest) != UDP_PORT || ntohs(udp->len) > 1400)
		return NF_ACCEPT;
	
	//wrk->skb = skb;
	//wrk->iph = iph;
	//wrk->udph = udp;
	//INIT_WORK(&wrk->work, ub_udp_rcv);
	//queue_work(wq, &wrk->work);	
	//ub_udp_rcv(&wrk->work);

	/* queue the work up for thread_to_use */
	spin_lock_irqsave(&irq_lock, flags);
	if (thread_to_use == ub_num_rx_workers - 1)
	{
		/* wrap around */
		thread_to_use = 0;
	}
	else
		thread_to_use++;
	skb_queue_tail(&ub_rx_queues[thread_to_use], skb);
	spin_unlock_irqrestore(&irq_lock, flags);

	return NF_STOLEN;
}

static struct nf_hook_ops hook = 
{
	.hook     = (nf_hookfn*) ub_udpserver_nethook_callback,
	.owner    = THIS_MODULE,
	.pf       = PF_INET,
	.hooknum  = NF_INET_LOCAL_IN, 
	.priority = NF_IP_PRI_LAST, /* don't bypass firewall rules */
};

int ub_udpserver_netstack_register(void)
{
	/* set up the receive queue sk_buff structs */
	int cpu;
	for (cpu = 0; cpu < ub_num_rx_workers; cpu++)
		skb_queue_head_init(&ub_rx_queues[cpu]);

	printk(KERN_ALERT "[Unbuckle] Registering the netfilter hooks.\n");
	nf_register_hook(&hook);
	
	/* piggy back here for now and create the workqueue */
	//wq = alloc_workqueue("unbuckle", WQ_UNBOUND, 0);
	
	return 0;
}
int ub_udpserver_netstack_unregister(void)
{
	printk(KERN_ALERT "[Unbuckle] Unregistering the netfilter hooks.\n");
	nf_unregister_hook(&hook);

	return 0;
}
