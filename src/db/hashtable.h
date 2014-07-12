
#ifndef UNBUCKLE_HASHTABLE_H
#define UNBUCKLE_HASHTABLE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdlib.h>
#endif

#include <entry.h>

#define HASHTABLE_SIZE_BITS 24

int ub_hashtbl_init(void);
void ub_hashtbl_exit(void);
struct ub_entry* ub_hashtbl_find(char* key, size_t len_key);

int ub_hashtbl_add(struct ub_entry*);
void ub_hashtbl_del(struct ub_entry*);

#ifdef __KERNEL__
/* Locking specific */
void ub_hashtbl_unlock_bucket(bucket_lock_ot *lock);
#endif

#endif /* UNBUCKLE_HASHTABLE_H */ 
