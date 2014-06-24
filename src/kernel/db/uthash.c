#include <db/hashtable.h>
#include <entry.h>
#include <kernel/db/uthash.h>
#include <linux/slab.h>

static struct ub_entry* hashtable = NULL;

int ub_hashtbl_init(void)
{
	return 0;
}

void ub_hashtbl_exit(void)
{
	//hash_map_delete(hashtable);
}

struct ub_entry* ub_hashtbl_find(char* key, size_t len_key)
{
	struct ub_entry* e = NULL;
	HASH_FIND(hh, hashtable, key, len_key, e);
	return e;
}

int ub_hashtbl_add(struct ub_entry* e)
{
	HASH_ADD_KEYPTR(hh, hashtable, ub_entry_loc_key(e), e->len_key, e);
	return 0;
}

void ub_hashtbl_del(struct ub_entry* e)
{
	HASH_DELETE(hh, hashtable, e);
	return;
}

