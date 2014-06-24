#include <prot/memcached.h>

#ifdef __KERNEL__
#include <linux/slab.h>
#else
#include <stdlib.h>
#include <string.h>
#endif	/* __KERNEL__ */

// Produce memcached packets for REQuests
struct memcache_hdr_req * memcached_produce_request(int opcode, char* extras,
	int len_extra, char* key, int len_key, char* value, int len_value)
{
	struct memcache_hdr_req* packet;
	char* loc_extra;
	char* loc_key;
	char* loc_value;

	int len_packet = MEMCACHED_PKT_REQ_LEN(len_extra + len_key + len_value);

#ifdef __KERNEL__
	packet = (struct memcache_hdr_req *) kmalloc(len_packet, GFP_KERNEL);
#else
	packet = (struct memcache_hdr_req *) malloc(len_packet);
#endif
	memset(packet, 0, len_packet);

	loc_extra = MEMCACHED_PKT_EXTRAS(packet);
	loc_key   = MEMCACHED_PKT_KEY(packet, len_extra);
	loc_value = MEMCACHED_PKT_VALUE(packet, len_extra, len_key);

	packet->opcode = opcode;
	packet->magic = MEMCACHED_MAGIC_REQ;
	packet->len_key = len_key;
	packet->len_extras = len_extra;
	packet->len_body = len_key + len_extra + len_value;
	
	// Copy data items to the proper location in the packet
	strncpy(loc_extra, extras, len_extra);
	strncpy(loc_key, key, len_key);
	strncpy(loc_value, value, len_value);
	
	return packet;
}

// Produce memcached packets for RESponses
struct memcache_hdr_res * memcached_produce_response(int opcode, char* extras,
	int len_extra, char* key, int len_key, char* value, int len_value)
{
	struct memcache_hdr_res* packet;
	char* loc_extra;
	char* loc_key;
	char* loc_value;

	int len_packet = MEMCACHED_PKT_RES_LEN(len_extra + len_key + len_value);
#ifdef __KERNEL__
	packet = (struct memcache_hdr_res *) kmalloc(len_packet, GFP_KERNEL);
#else
	packet = (struct memcache_hdr_res *) malloc(len_packet);
#endif
	memset(packet, 0, len_packet);

	loc_extra = MEMCACHED_PKT_EXTRAS(packet);
	loc_key   = MEMCACHED_PKT_KEY(packet, len_extra);
	loc_value = MEMCACHED_PKT_VALUE(packet, len_extra, len_key);

	packet->opcode = opcode;
	packet->magic = MEMCACHED_MAGIC_RES;
	packet->len_key = len_key;
	packet->len_extras = len_extra;
	packet->len_body = len_key + len_extra + len_value;

	// Copy data items to the proper location in the packet
	memcpy(loc_extra, extras, len_extra);
	memcpy(loc_key, key, len_key);
	memcpy(loc_value, value, len_value);

	return packet;
}
