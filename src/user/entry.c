
#include <buckets.h>
#include <entry.h>
#include <db/hashtable.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int ub_cache_replace(char* key, size_t len_key, char* val, size_t len_val)
{
	int err;
	struct ub_entry* e;
	
	/* check whether the given key exists already */
	/*e = ub_hashtbl_find(key, len_key);
	if (e)
	{*/
		/* the key already exists -- simplest thing to do at the moment is to delete
		   the corresponding entry and add in a new one */
	/*	ub_hashtbl_del(e);
		kfree(e);
	}*/
	// TODO: this function should receive a struct entry* not allocate memory here
	err = ub_buckets_alloc(ub_entry_size(len_key, len_val), (void**) &e);
	
	if (err)
		return err;
	
	e->len_key = len_key;
	e->len_val = len_val;

	memcpy(ub_entry_loc_key(e), key, len_key);
	memcpy(ub_entry_loc_val(e), val, len_val);

	/* add the embedded list header into the hash table */
	return ub_hashtbl_add(e);
}

struct ub_entry* ub_cache_find(char* key, size_t len_key)
{
	return ub_hashtbl_find(key, len_key);
}
