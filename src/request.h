#ifndef UNBUCKLE_REQUEST_H
#define UNBUCKLE_REQUEST_H

#ifdef __KERNEL__
#include <linux/in.h>
#include <linux/socket.h>
#else
#include <arpa/inet.h>
#include <stdint.h>
#endif

#include <core.h>
#include <prot/memcached.h>

enum conn_states {
	conn_wait,
	conn_proc_udp,
	conn_parse_cmd,
	conn_proc_cmd,
	conn_send,
	conn_done,
	conn_quit
};

enum memcache_commands {
	cmd_set,
	cmd_get
};

enum memcache_protocol {
	ascii, 
	binary
};

struct request_state {
	uint16_t	reqid;         // Request ID
	struct msghdr msg; // msghdr so we know where to send replies
	struct sockaddr_in sockaddr;
	unsigned char* recvbuf;     // receive buffer into which data is read
	unsigned char* recvbuf_cur; // pointer to current location in receive buffer
	size_t len_recvbuf;   // size of the receive buffer
	size_t len_rdata;     // data remaining under recvbuf_cur pointer
	enum conn_states state; // current state of the connection
 	
	// memcache protocol-specific fields
	enum memcache_protocol prot; // which protocol format is in use?
	enum memcache_commands cmd;  // command of the current request
	int err; // any errors arising from processing the request

	// The input data from the initial request
	unsigned char* key; // pointer to the key in the header
	int len_key; // length of said key
	unsigned char* data; // pointer to the data values in the request (i.e. value field)
	int len_data; // length of the said data value

	unsigned char* sendbuf;
	unsigned char* sendbuf_cur;
	size_t len_sendbuf;
	size_t len_sendbuf_cur;

	// for memcached binary protocol
	struct memcache_hdr_req* bin_hdr_request; // location of the binary header
	struct memcache_hdr_res* bin_hdr_response; // location of response header
	
	struct memcache_udp_header* udpheaders;  /* pointer to space for UDP headers */

#ifdef __KERNEL__
	// For kernel use only, when sending by using an SKB
	struct sk_buff* skb_tx;
	struct sk_buff* skb_rx;
	struct iphdr* iph;
	struct udphdr* udph;
	__be32 saddr;
	__be32 daddr;
#endif
};

#endif /* UNBUCKLE_REQUEST_H */
