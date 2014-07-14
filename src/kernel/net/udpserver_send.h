#ifndef UB_UDPSERVER_SEND
#define UB_UDPSERVER_SEND

/* This header defines a thread which will be the sole process which deals with 
   sending packets into the NIC's send buffers, thereby avoiding lock contention
   by having all the workers invoke TX themselves (dev_queue_xmit has to acquire
   a TX lock when it invokes transmission which obviously delays more useful 
   work being done) */

#include <linux/skbuff.h>
#include <linux/spinlock.h>

#define MAX_CPUS	8

extern struct sk_buff_head ub_tx_queues[MAX_CPUS];

/* control functions for starting and stopping the TX worker thread */
void ub_udpserver_nictxworker_init(void);
void ub_udpserver_nictxworker_exit(void);

#endif
