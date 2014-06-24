#ifndef LINKLIST_H
#define LINKLIST_H

/**
 * As the key and value are variable length, contiguous memory is allocated for
 * this data structure + length of key + length of data.
 */
struct memcached_linklist_node {
	struct memcached_linklist_node* next;
	int len_key;
	int len_value;
};

#define MEMCACHED_LINKLIST_WRAPPER_SIZE		sizeof(struct memcached_linklist_node)
#define	MEMCACHED_LINKLIST_NODE_SIZE(len_key, len_value)	\
											MEMCACHED_LINKLIST_WRAPPER_SIZE + len_key + len_value
#define MEMCACHED_LINKLIST_LOC_KEY(node)	(((char *) node) + MEMCACHED_LINKLIST_WRAPPER_SIZE)
#define MEMCACHED_LINKLIST_LOC_VAL(node)	MEMCACHED_LINKLIST_LOC_KEY(node) + node->len_key

int memcached_db_linklist_init(void);
void memcached_db_linklist_exit(void);
void memcached_db_linklist_add(char* key, int len_key, char* val, int len_val);
int memcached_db_linklist_findkey(char* key, int keylen, char** val);

#endif /* LINKLIST_H */
