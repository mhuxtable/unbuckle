
#include <buckets.h>
#include <db/hashtable.h>
#include <entry.h>
#include <kernel/net/skbs.h>

#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>

static void ub_cache_del(char* key, size_t len_key)
{
	struct ub_entry* e = ub_hashtbl_find(key, len_key);
	if (e)
	{
		kfree_skb(e->skb);
		ub_hashtbl_del(e);
	}
}

int ub_cache_replace(char* key, size_t len_key, char* val, size_t len_val)
{
	int err = 0;
	struct ub_entry* e;
	
	/* check whether the given key exists already and delete if so */
	ub_cache_del(key, len_key);

	// TODO: this function should receive a struct entry* not allocate memory here
//	err = ub_buckets_alloc(ub_entry_size(len_key, len_val), (void**) &e);
	e = (struct ub_entry *) kmalloc(ub_entry_size(len_key, len_val), GFP_KERNEL);
	
	if (err)
		return err;
	
	/* in the kernel, so need to set up the skb which will store the key and value.
	   The skb will contain the ASCII header EXACTLY as it will be played back in 
	   response to a GET request. This means storing the following:
	   
	     VALUE [The Key] 0 [ASCII formatted integer of byte length of the value]\r\n
	     [The actual data goes here]\r\nEND\r\n
	
	   Thus we need to do some calculations here and now before we can ask for an
	   skb. The fixed overhead is as follows:
	   
	   6 bytes ("VALUE ") + len_key + 3 bytes (" 0 ") + len_strlen_valbuf (ASCII 
	   format of len_val -- will vary) + 2 bytes ("\r\n") + len_val + 7 bytes 
	   ("\r\nEND\r\n").

	   i.e. 18 bytes + len_key + len_strlen_valbuf + len_val */
	
	{	
		char strlen_valbuf[10];
		int len_strlen_valbuf = 
			snprintf(&strlen_valbuf[0], 10, " 0 %zu\r\n", len_val);

		e->skb = ub_skb_set_up(len_key + len_val + len_strlen_valbuf + 18);

		if (unlikely(!e->skb))
			return -1;
		
		e->len_key = len_key;
		e->len_val = len_val;
		
		ub_push_data_to_skb(e->skb, "VALUE ", strlen("VALUE "));
		e->loc_key = ub_push_data_to_skb(e->skb, key, len_key);
		ub_push_data_to_skb(e->skb, strlen_valbuf, len_strlen_valbuf);
		e->loc_val = ub_push_data_to_skb(e->skb, val, len_val);
		ub_push_data_to_skb(e->skb, "\r\nEND\r\n", strlen("\r\nEND\r\n"));
	}

	/* add the embedded list header into the hash table */
	return ub_hashtbl_add(e);
}

struct ub_entry* ub_cache_find(char* key, size_t len_key)
{
	return ub_hashtbl_find(key, len_key);
}
