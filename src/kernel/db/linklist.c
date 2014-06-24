/**
 * Unbuckle -- Linked List implementation for backend data storage
 * making use of the kernel's internal interface for manipulating
 * linked lists.
 */

#include <linux/slab.h>
#include <linux/string.h>

#include <kernel/db/linklist.h>

struct memcached_linklist_node* list_root = NULL;

// Forward declarations
#ifdef DEBUG
static int listlen(void);
#endif
/**
 * Allocates a memcached linklist node structure for the given key and value,
 * but does not take the additional step of linking this into the linked list.
 * (that is handled elsewhere). Used internally for memory allocaion and
 * encapsulating the internal parameters of the linklist node.
 */
static struct memcached_linklist_node* memcached_db_linklist_mknode(char* key, 
    int len_key, char* value, int len_value)
{
    struct memcached_linklist_node* node;

    // Allocate normal kernel memory to hold the node in the list
    node = (struct memcached_linklist_node*) 
        kmalloc(MEMCACHED_LINKLIST_NODE_SIZE(len_key, len_value), GFP_KERNEL);

	node->next = NULL;	
	node->len_key = len_key;
	node->len_value = len_value;
    
	// Copy the key and value data into the memory allocated for them
    strncpy(MEMCACHED_LINKLIST_LOC_KEY(node), key, len_key);
	strncpy(MEMCACHED_LINKLIST_LOC_VAL(node), value, len_value);

    // Note -- it still needs linking in to the linked list structure, but all
    // we do here is make the actual node to be linked elsewhere.
    return node;
}

int memcached_db_linklist_init(void)
{
	// Don't need to actually do anything to initialise the list.
	return 0;
}

void memcached_db_linklist_exit(void)
{
	// Need to systematically free everything stored in the list
	struct memcached_linklist_node* next;
	struct memcached_linklist_node* el = list_root;

	while (el)
	{
		printk("Releasing link list node.\n");
		next = el->next;
		kfree(el);
		el = next;
	}

	return;
}

void memcached_db_linklist_add(char* key, int len_key, char* val, int len_val)
{
	struct memcached_linklist_node* p;
    struct memcached_linklist_node* node;
#ifdef DEBUG
	printk("Adding %.*s\n", len_key, key);
#endif
	node = memcached_db_linklist_mknode(key, len_key, val, len_val);

    // Link the new node into the linked list data structure
    if (!list_root) {
		list_root = node;
    }
	else {
		p = list_root;
		while (p->next) {
			p = p->next;
		}
		p->next = node;
#ifdef DEBUG
		printk("[linklist] list has length %d \n", listlen());
#endif
	}
}

int memcached_db_linklist_findkey(char* key, int keylen, char** val)
{
    // Iterates over the Linked List to attempt to locate the given key
    // (obviously in time O(n) -- ugly for lots of data, but a necessary first attempt)

	struct memcached_linklist_node* p;
#ifdef DEBUG	
	printk("[linklist] Trying to find %.*s\n", keylen, key);
#endif
	p = list_root;
	
	while (p) {
		if (!strncmp(key, MEMCACHED_LINKLIST_LOC_KEY(p), keylen)) {
			// Found the right key!
#ifdef DEBUG
			printk("Found key %.*s\n", p->len_key, MEMCACHED_LINKLIST_LOC_KEY(p));
#endif
			*val = MEMCACHED_LINKLIST_LOC_VAL(p);
			return p->len_value;
		}
		else
		{
			p = p->next;
		}
	}

	// If it gets here, then nothing was found.
	*val = NULL;
	return 0;
}

#ifdef DEBUG
static int listlen(void)
{
	struct memcached_linklist_node* p;
	int len = 0; 

	p = list_root;

	while (p) {
		len++;
		p = p->next;
	}
	
	return len;
}
#endif
