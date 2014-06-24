#ifndef UNBUCKLE_UDPSERVER_LOW_H
#define UNBUCKLE_UDPSERVER_LOW_H

#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/workqueue.h>
#include <unbuckle.h>

extern struct sk_buff_head ub_rx_queues[MAX_WORKERS];

int ub_udpserver_netstack_register(void);
int ub_udpserver_netstack_unregister(void);

void ub_udp_rcv(struct work_struct*);

#endif 
