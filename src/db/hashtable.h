
#ifndef UNBUCKLE_HASHTABLE_H
#define UNBUCKLE_HASHTABLE_H

#ifdef __KERNEL__
#include <linux/spinlock.h>
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
void ub_hashtbl_unlock_bucket(spinlock_t *lock);
int ub_hashtbl_lock_bucket(spinlock_t *lock);
spinlock_t *ub_hashtbl_get_bucket_lock(char *key, int len_key);
#endif

#endif /* UNBUCKLE_HASHTABLE_H */ 
