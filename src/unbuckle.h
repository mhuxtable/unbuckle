#ifndef UNBUCKLE_H
#define UNBUCKLE_H

#include <linux/rwsem.h>

#include <db/hashtable.h>

/* but we might spin up fewer if we don't have this many CPUs */
#define MAX_WORKERS 10


/* used to determine whether the system is up and running and when the threads
   should finish their loop and quit */
extern volatile int ub_sys_running;
extern unsigned int ub_num_rx_workers;

extern struct rw_semaphore rwlock[(1 << HASHTABLE_SIZE_BITS)];

#endif
