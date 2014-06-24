#include <kernel/net/udpserver_send.h>
#include <unbuckle.h>

#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

struct sk_buff_head ub_tx_queues[MAX_CPUS];
static struct task_struct* txworker;
static struct task_struct* txworker2;

static int nictxworker_run(void)
{
	int err = 0;

	while (!kthread_should_stop() && ub_sys_running)
	{
		int cpu;
		int workdone = 0;
		for (cpu = 0; cpu < MAX_CPUS; cpu++)
		{
			struct sk_buff_head* q = &ub_tx_queues[cpu];
			if (!skb_queue_empty(q))
			{
				/* data to be transmitted */
				struct sk_buff* skb = skb_dequeue(q);
				workdone = 1;
				if (skb)
				{
					err = dev_queue_xmit(skb);
					err = net_xmit_eval(err);
				}
			}
		}
		if (workdone == 0)
			schedule();
	}

	return 0;
}

void ub_udpserver_nictxworker_init(void)
{
	/* initialise skbuff queue heads */
	int cpu;
	for (cpu = 0; cpu < MAX_CPUS; cpu++)
		skb_queue_head_init(&ub_tx_queues[cpu]);

	txworker = kthread_create((void*) nictxworker_run, NULL, "unbuckletx1");

	if (txworker)
	{
		/* the first CPU core is reserved for us */
		kthread_bind(txworker, 0);
		get_task_struct(txworker);
		wake_up_process(txworker);
	}

	txworker2 = kthread_create((void*) nictxworker_run, NULL, "unbuckletx2");

	if (txworker2)
	{
		/* the first CPU core is reserved for us */
		kthread_bind(txworker2, 1);
		get_task_struct(txworker2);
		wake_up_process(txworker2);
	}
	return;
}
void ub_udpserver_nictxworker_exit(void)
{
	if (txworker)
	{
		kthread_stop(txworker);
		put_task_struct(txworker);
	}

	if (txworker2)
	{
		kthread_stop(txworker2);
		put_task_struct(txworker2);
	}
	return;
}
