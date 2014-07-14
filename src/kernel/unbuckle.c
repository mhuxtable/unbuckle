#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/rwsem.h>

#include <core.h>
#include <buckets.h>
#include <db/hashtable.h>
#include <kernel/db/linklist.h>
#include <unbuckle.h>
#include <kernel/net/udpserver_low.h>
#include <kernel/net/udpserver_send.h>

/* memory limit -- measured in megabytes here */
static unsigned int ub_global_memory_limit = 55000;
module_param_named(memlim, ub_global_memory_limit, int, 0);

/* maximum number of worker threads */
static int ub_max_worker_threads = MAX_WORKERS;
module_param_named(workers, ub_max_worker_threads, int, 0);

volatile int ub_sys_running = 0;
unsigned int ub_num_rx_workers = MAX_WORKERS;

static struct task_struct* workers[MAX_WORKERS];

/* start up worker threads */
static int worker_init(void)
{
	// Thread creation -- ensure get_task_struct is called so the task_struct struct
	// does not go away if the thread terminates before Unbuckle asks it to. Avoids
	// NULL pointer deref issues when it is time to stop the threads.

	// Note that at this point the UDP server runs within the context of the worker.
	int i;
	/* don't use CPUid 0 (leave for the system) and don't use the last one 
	   (for the NIC TX worker) */
	for (i = 0; i < ub_num_rx_workers; i++)
	{
		char name[15];
		snprintf(name, 15, "unbucklerx%d", i);
		printk("Starting rxworker %s\n", name);

		workers[i] = kthread_create((void*) ub_core_run, NULL, name);

		if (workers[i])
		{
			printk("Binding and waking. %s\n", name);
			kthread_bind(workers[i], i+2);
			get_task_struct(workers[i]);
			wake_up_process(workers[i]);
		}
	}

	return 0;
}

static void worker_exit(void)
{
	int i;
	for (i = 0; i < MAX_WORKERS; i++)
	{
		if (workers[i])
		{
			kthread_stop(workers[i]);
			// kernel will free task_struct when no one is using it anymore
			put_task_struct(workers[i]);
		}
	}
	return;
}

static void init_rwsem_locks(void)
{
	int i;
	for (i = 0; i < (1 << HASHTABLE_SIZE_BITS); i++)
		init_rwsem(&rwlock[i]);
	return;
}

static int __init unbuckle_init(void)
{
	printk(KERN_ALERT "Unbuckle Key-Value Store starting up...\n");	

	if (ub_max_worker_threads > MAX_WORKERS)
	{
		printk(KERN_WARNING "[Unbuckle] %d workers out of range. "
			"Limiting to %d workers.", ub_max_worker_threads, MAX_WORKERS);
		ub_max_worker_threads = MAX_WORKERS;
	}

	printk(KERN_ALERT "Limiting memory usage to %u MB.\n", ub_global_memory_limit);
	
	init_rwsem_locks();	

#ifdef STORE_LINKLIST
	memcached_db_linklist_init();
#endif
#ifdef STORE_HASHTABLE
	ub_hashtbl_init();
#endif
	
	ub_buckets_init(ub_global_memory_limit);

	ub_sys_running = 1;

	ub_udpserver_nictxworker_init();
	ub_udpserver_netstack_register();
	worker_init();

	return 0;
}

static void __exit unbuckle_exit(void)
{
	printk(KERN_ALERT "Unloading Unbuckle...\n");
	ub_sys_running = 0;
	
	ub_udpserver_netstack_unregister();
	worker_exit();
	ub_udpserver_nictxworker_exit();

#ifdef STORE_LINKLIST
	memcached_db_linklist_exit();
#endif
#ifdef STORE_HASHTABLE
	ub_hashtbl_exit();
#endif

	ub_buckets_exit();
}

	
module_init(unbuckle_init);
module_exit(unbuckle_exit);
MODULE_LICENSE("GPL");
