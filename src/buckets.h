#ifndef UNBUCKLE_BUCKETS_H
#define UNBUCKLE_BUCKETS_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdlib.h>
#endif

int ub_buckets_alloc(size_t len_buffer, void** location);
int  ub_buckets_init(size_t memory_limit);
void ub_buckets_exit(void);

#endif /* UNBUCKLE_BUCKETS_H */
