#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <abstract.h>
#include <core.h>
#include <entry.h>
#include <request.h>
#include <uberrors.h>
#include <net/udpserver.h>

static void add_buffer_to_reply(struct request_state* req, void* buf, int len_buf)
{
	if (req->len_sendbuf_cur + len_buf > req->len_sendbuf)
		return;
	memcpy(req->sendbuf_cur, buf, len_buf);
	req->sendbuf_cur += len_buf;
	req->len_sendbuf_cur += len_buf;
	return;
}
static void add_string_to_reply(struct request_state* req, char* str)
{
	add_buffer_to_reply(req, str, strlen(str));
	return;
}

static void build_get_ascii_response(struct request_state* req)
{
	int len_len_valbuf;
	char len_valbuf_formatted[10];

	if (req->data)
	{
		// Horribly hacky way of converting the integer back to ASCII 
		// chars for the response
		len_len_valbuf = snprintf(&len_valbuf_formatted[0], 10, " 0 %d\r\n", req->len_data);
		
		// Found a response
		// TODO needs better error handling
		add_string_to_reply(req, "VALUE ");
		add_buffer_to_reply(req, req->key, req->len_key);
		add_buffer_to_reply(req, &len_valbuf_formatted, len_len_valbuf);
		add_buffer_to_reply(req, req->data, req->len_data);
		add_string_to_reply(req, "\r\nEND\r\n");
	}
	else if (req->err == -EUBKEYNOTFOUND)
		add_string_to_reply(req, "NOT_FOUND\r\n");
	else
		add_string_to_reply(req, "SERVER_ERROR something went wrong\r\n");
}		

static void build_common_binary_response_fields(struct request_state* req)
{
	if (!req->bin_hdr_response)
	{
		req->bin_hdr_response = (struct memcache_hdr_res*)
			ALLOCMEM(MEMCACHED_PKT_HDR_RES_LEN, GFP_KERNEL);
	}

	req->bin_hdr_response->magic = MEMCACHED_MAGIC_RES;
	req->bin_hdr_response->opcode = req->cmd;
	req->bin_hdr_response->len_key = 0L;
	req->bin_hdr_response->len_extras = 0L;
	req->bin_hdr_response->datatype = 0L;
	req->bin_hdr_response->opaque = 0L;
	req->bin_hdr_response->cas = 0L;
	req->bin_hdr_response->status = 0L; // overridden later if necessary
	req->bin_hdr_response->len_body = 0L;
	return;
}

static void build_get_binary_response(struct request_state* req)
{
	build_common_binary_response_fields(req);

	if (req->err == -EUBKEYNOTFOUND)
	{
		// Key was not found
		req->bin_hdr_response->status = htons(MEMCACHED_STATUS_KEYNOTFOUND);
	}

	// Note: the key is not echoed back with the data in a binary request
	req->bin_hdr_response->len_body = htonl(MEMCACHED_LEN_BODY(
		req->bin_hdr_response->len_extras,
		0, req->len_data
	));
	
	if (req->len_data > 0)
	{
		// Found a result so add that data to the scatter-gather to be returned
		add_buffer_to_reply(req, req->data, req->len_data);
	}
	
	//iov_add(req, req->bin_hdr_response, MEMCACHED_PKT_HDR_RES_LEN);
}

static void build_get_response(struct request_state* req)
{
	if (req->prot == binary)
		build_get_binary_response(req);
	else
		build_get_ascii_response(req);
}

static int process_get(struct request_state* req)
{
	struct ub_entry* e;

#ifdef STORE_LINKLIST	
	len_valbuf = memcached_db_linklist_findkey(req->key, req->len_key, &valbuf);
#endif
#ifdef STORE_HASHTABLE
	e = ub_cache_find(req->key, req->len_key);
#endif
	if (!e)
	{
		req->err = -EUBKEYNOTFOUND;
		return req->err;
	}

	req->data = ub_entry_loc_val(e);
	req->len_data = e->len_val;

	build_get_response(req);
		
	req->state = conn_send;

	return 0;
}

static void build_set_ascii_response(struct request_state* req)
{
	if (req->err == 0)
		add_string_to_reply(req, "STORED\r\n");
	else if (req->err == -ENOMEM)
		add_string_to_reply(req, "NOT_STORED\r\n");
	else
	{
		char errstring[20];
		snprintf(&errstring[0], 20, "SERVER ERROR %d\r\n", req->err);
		add_string_to_reply(req, errstring);
	}

	return;
}

static void build_set_binary_response(struct request_state* req)
{
	build_common_binary_response_fields(req);

	if (UNLIKELY(req->err < 0))
	{
		if (req->err == -EUBOUTOFMEM)
			req->bin_hdr_response->status = MEMCACHED_STATUS_NOMEM;
		else
			req->bin_hdr_response->status = MEMCACHED_STATUS_ITEMNOTSTORED;
	}

	//iov_add(req, req->bin_hdr_response, MEMCACHED_PKT_HDR_RES_LEN);

	// Nothing else special to set in a set response
	// TODO: needs to deal with item not set and key already exists (?)
	// Note 2014-02-07: no need in a SET request to deal with key already 
	// exists -- this is only relevant to the semantics of an ADD request 
	// (or a REPLACE for a non-existent key)

	return;
}

static void build_set_response(struct request_state* req)
{
	if (req->prot == binary)
		build_set_binary_response(req);
	else
		build_set_ascii_response(req);
}

static int process_set(struct request_state* req)
{
	// TODO: check the key doesn't exist already!!!!!!
#ifdef STORE_LINKLIST
	memcached_db_linklist_add(req->key, req->len_key, req->data, req->len_data);
#endif
#ifdef STORE_HASHTABLE
	req->err = ub_cache_replace(req->key, req->len_key, req->data, req->len_data);
#endif

	build_set_response(req);
	
	return 0;
}

int process_request(struct request_state* req)
{
	switch (req->cmd)
	{
	case cmd_get:
		if (process_get(req))
			return -1;
		break;
	case cmd_set:
		if (process_set(req))
			return -1;
		break;
	}

	req->state = conn_send;

	return 0;
}

int ub_core_run(void)
{
	int res;
	
	struct request_state* req = (struct request_state*) 
		ALLOCMEM(sizeof(struct request_state), GFP_KERNEL);
#ifdef __KERNEL__
	allow_signal(SIGKILL | SIGSTOP);
#endif
	if (!req
#ifdef __KERNEL__
		|| ksize(req) < sizeof(struct request_state)
#endif
		)
		return -ENOMEM;
	memset(req, 0, sizeof(struct request_state));

	if ((res = udpserver_start(req)) < 0)
	{
#ifdef DEBUG
		PRINTARGS(
#ifdef __KERNEL__
		KERN_ERR 
#endif
		"Something went wrong starting the UDP server %d\n", res);
#endif
		FREEMEM(req);
		return res;
	}
	res = process_slowpath(req);

	udpserver_exit();

	if (req)
	{
		FREEMEM(req);
		req = NULL;
	}

	return res;
}
