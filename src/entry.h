#ifndef UNBUCKLE_ENTRY_H
#define UNBUCKLE_ENTRY_H

#ifdef __KERNEL__
#include <linux/skbuff.h>
#include <linux/types.h>
#include <kernel/db/uthash.h>
#else
#include <stdlib.h>

#include <user/db/uthash.h>
#endif

/* item entries -- used for storing metadata and actual cached data - the idea 
   is to allocate enough memory for the entry header + the key and value to be
   stored in the cache, unless you're in the kernel, when struct ub_entry is a
   fixed size with a pointer to the actual data stored in an skb pre-prepared 
   for shipping out to a NIC when a request turns up. */
struct ub_entry {
#ifdef HASHTABLE_UTHASH
	UT_hash_handle hh;
#endif
#ifdef HASHTABLE_KHASH
	struct hlist_node hlist;
#endif
	size_t len_key;
	size_t len_val;
#ifdef __KERNEL__
	unsigned char* loc_key;
	unsigned char* loc_val;
	struct sk_buff* skb;
#endif
};

#define UB_ENTRY_SIZE sizeof(struct ub_entry)

/* inlined here so that each compilation unit gets its own copy, which might be
   wasteful but avoids the overhead of a branch for what is a very simple ALU
   calculation. Note that since the change to storing skbuffs directly in the k-v
   store for the kernel version, the kernel needs a different return value here to
   the userspace versions. */

#ifdef __KERNEL__
static inline size_t ub_entry_size(size_t len_key, size_t len_val)
{
	/* the key and value are stored in the skb instead in the kernel */
	return UB_ENTRY_SIZE;
}
static inline char* ub_entry_loc_key(struct ub_entry* e)
{
	return e->loc_key;
}
static inline char* ub_entry_loc_val(struct ub_entry* e)
{
	return e->loc_val;
}
#else
static inline size_t ub_entry_size(size_t len_key, size_t len_val)
{
	return UB_ENTRY_SIZE + len_key + len_val;
}
static inline char* ub_entry_loc_key(struct ub_entry* e)
{
	return ((char*) e) + UB_ENTRY_SIZE;
}
static inline char* ub_entry_loc_val(struct ub_entry* e)
{
	return ((char*) e) + UB_ENTRY_SIZE + e->len_key; 
}
#endif

/* replacement will replace an item which already exists by another, and add a 
   new entry (possibly with a different value) for an item if it already exists. */
int ub_cache_replace(char* key, size_t len_key, char* val, size_t len_val);
struct ub_entry* ub_cache_find(char* key, size_t len_key);

#endif
