/**
 * memcached protocol
 * Contains definitions of that defined in the memcached (binary) protocol,
 * found at http://code.google.com/p/memcached/wiki/MemcacheBinaryProtocol.
 */

#ifndef MEMCACHED_H
#define MEMCACHED_H

#ifdef __KERNEL__
#include <asm/types.h>
#include <linux/types.h>
#else
#include <stdint.h>
#include <stdlib.h>
#endif

#define MEMCACHED_MAGIC_REQ 0x80
#define MEMCACHED_MAGIC_RES 0x81

#define	MEMCACHED_OPCODE_GET	0x00
#define	MEMCACHED_OPCODE_SET	0x01

#define MEMCACHED_STATUS_NOERROR        0x00
#define MEMCACHED_STATUS_KEYNOTFOUND    0x01
#define MEMCACHED_STATUS_KEYEXISTS      0x02
#define MEMCACHED_STATUS_VALUETOOLARGE  0x03
#define MEMCACHED_STATUS_INVALIDARGS    0x04
#define MEMCACHED_STATUS_ITEMNOTSTORED  0x05
#define MEMCACHED_STATUS_INCRDECRNOTNUM 0x06
#define MEMCACHED_STATUS_UNKNOWNCOMMAND 0x07
#define MEMCACHED_STATUS_NOMEM          0x08

// Request header
struct memcache_hdr_req {
	uint8_t		magic;		/* magic byte -- pair for request/response, 
						   one for each version of the protocol */
	uint8_t		opcode;
	uint16_t		len_key;
	uint8_t		len_extras;
	uint8_t		datatype;	/* currently unused -- should be initialised to 0x00 */
	uint16_t		reserved;	
	uint32_t		len_body;
	uint32_t		opaque;		/* copied back in the response */
	uint64_t		cas;		/* data version check */
};

// Response header
struct memcache_hdr_res {
    uint8_t		magic;
    uint8_t		opcode;
    uint16_t	len_key;
    uint8_t		len_extras;
    uint8_t		datatype;
    uint16_t	status;
    uint32_t	len_body;
    uint32_t	opaque;
    uint64_t	cas;
};

// UDP frame header at the front of every message received by UDP.
struct memcache_udp_header {
#ifdef __KERNEL__
	__be16 req;      /* request ID -- supplied by client and copied back */
	__be16 seq;      /* packet sequence number -- 0 - n-1 (n total packets) */
	__be16 count;    /* total number of datagrams in this message */
	__be16 reserved; /* reserved for future use and must initialise to 0x00 */
#else
	uint16_t req;
	uint16_t seq;
	uint16_t count;
	uint16_t reserved;
#endif
};

#define MEMCACHED_PKT_REQ_LEN(payload)	(int) (sizeof(struct memcache_hdr_req) + payload)
#define MEMCACHED_PKT_RES_LEN(payload)  (int) (sizeof(struct memcache_hdr_res) + payload)
#define MEMCACHED_PKT_HDR_REQ_LEN		sizeof(struct memcache_hdr_req)
#define MEMCACHED_PKT_HDR_RES_LEN       sizeof(struct memcache_hdr_res)

/* This covers up a subtlety -- the length used here is for one of the two defined
   header structures but this field can be used to refer to both the response and
   the request headers. The header is defined in the memcached packet structure as a
   24 byte length structure. Both the response and request headers are defined in
   accordance with this format, and just vary by their definition of internal fields.
   Thus, it is okay to arbitrarily pick one of the headers to use as the header length */
#define MEMCACHED_PKT_HDR_LEN          MEMCACHED_PKT_HDR_REQ_LEN

/* Determines the pointer at which the various variable length data sections appear
   following the header block. The order of these sections is defined in the memcached
   binary protocol. */
#define MEMCACHED_PKT_EXTRAS(hdr)		(void *)(((char *) hdr) + MEMCACHED_PKT_HDR_LEN)
#define MEMCACHED_PKT_KEY(hdr, len_extra) \
	(void *)(((char *) MEMCACHED_PKT_EXTRAS(hdr)) + len_extra)
#define MEMCACHED_PKT_VALUE(hdr, len_extra, len_key) \
	(void *)(((char *)MEMCACHED_PKT_KEY(hdr, len_extra)) + len_key)

#define MEMCACHED_LEN_VAL(req)	req->len_body - (req->len_key + req->len_extras)
#define MEMCACHED_LEN_BODY(extras, key, val)	extras + key + val

struct memcache_hdr_req * memcached_produce_request(int opcode, char* extras, 
    int len_extra, char* key, int len_key, char* value, int len_value);
struct memcache_hdr_res * memcached_produce_response(int opcode, char* extras, 
    int len_extra, char* key, int len_key, char* value, int len_value);

#endif /* MEMCACHED_H */
