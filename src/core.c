
#ifdef __KERNEL__
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/wait.h>
#include <net/sock.h>
#include <linux/types.h>
#include <linux/byteorder/generic.h>
#include <kernel/net/udpserver_low.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#endif

#include <abstract.h>
#include <core.h>
#include <entry.h>
#include <net/udpserver.h>
#include <request.h>
#include <unbuckle.h>
#include <prot/memcached.h>
#include <uberrors.h>

#ifdef __KERNEL__
#endif

static int parse_ascii_request(struct request_state* req)
{
	int err;
	int tokens = 0;
	unsigned char* start;
	
	if (ascii != req->prot)
		return MEMCACHE_PROT_ERROR;
	
	start = req->recvbuf_cur;
	while (req->len_rdata > 0 && req->recvbuf_cur[0] != '\n')
	{
		if (req->recvbuf_cur[0] == ' ' || req->recvbuf_cur[0] == '\r')
		{
			// Found a token
			req->recvbuf_cur[0] = '\0';

			switch (tokens)
			{
			case 0:
				// This is the command - note strnicmp will corrupt the value of the
				// "start" pointer by advancing it -- assume it needs resetting.
				if (!STRNICMP(start, "get", (req->recvbuf_cur - start)))
					req->cmd = cmd_get;
				else if (!STRNICMP(start, "set", (req->recvbuf_cur - start)))
					req->cmd = cmd_set;
				else
					return MEMCACHE_UNSUPPORTED_CMD;
				
				break;

			case 1:
				// This is the key. Just set up a pointer from here and compute the
				// length of the key by pointer arithmetic to next space delimiter.
				req->key = start;
				req->len_key = req->recvbuf_cur - start;

				break;

			case 2:
				// This is the flags. Ignore for now. Unimplemented.
				break;
			case 3:
				// This is the expiry. Ignore for now. Unimplemented.
				break;
			case 4:
				// This is the number of bytes in the request. It needs to be 
				// converted from its req->recvbuf_current char representation to an int.
#ifdef __KERNEL__
				err = kstrtoint(start, 0, &req->len_data);
#else
				req->len_data = strtol(start, NULL, 0);
				err = 0;
#endif
				if (!err)
				{
#ifdef DEBUG
					PRINTARGS("Number of bytes %d\n", req->len_data);
#endif
				}
				else
					return MEMCACHE_PROT_ERROR;

				break;
			case 5:
				// This is the (optional) command noreply -- signalling a reply
				// is not wanted at this time. This is unimplemented.
				break;
			}

			start = req->recvbuf_cur + 1;
			tokens++;
		}

		// WARNING: this is NOT atomic (but multiple threads shouldn't conreq->recvbuf_currently
		// be processing a single request anyway).
		req->recvbuf_cur++;
		req->len_rdata--;
	}

	// The memcached protocol requires us to have seen at least 5 tokens. Something
	// went wrong if this was not the case.
	if (UNLIKELY( (tokens < 5 && req->cmd == cmd_set) || (tokens < 2 && req->cmd == cmd_get) ))
		return MEMCACHE_PROT_ERROR;
	
	// Consume the final \n assuming there is still data to consume
	if (req->len_rdata > 0)
	{
		req->recvbuf_cur++;
		req->len_rdata--;
	}

	// The next position is the starting point of the data value (if it exists)
	if (req->len_rdata > 0)
		req->data = req->recvbuf_cur;
	else
	{
		// Ensure the data pointer is flushed
		// It wasn't guaranteed that request_state struct was clean
		req->data = NULL;
		req->len_data = 0;
	}
	
	return MEMCACHE_PROT_OK;
}

static int parse_binary_request(struct request_state* req)
{
	if (binary != req->prot)
		return MEMCACHE_PROT_ERROR;

	if (req->len_rdata < MEMCACHED_PKT_HDR_REQ_LEN)
	{
#ifdef DEBUG
		PRINT("[Unbuckle] UDP binary request received, but insufficient data for a header.\n");
#endif
		return MEMCACHE_PROT_ERROR;
	}

	req->bin_hdr_request = (struct memcache_hdr_req*) req->recvbuf_cur;

	// consume header
	req->recvbuf_cur += MEMCACHED_PKT_HDR_REQ_LEN;
	req->len_rdata -= MEMCACHED_PKT_HDR_REQ_LEN;

	// deal with byte ordering
	req->bin_hdr_request->len_key = ntohs(req->bin_hdr_request->len_key);
	req->bin_hdr_request->len_body = ntohl(req->bin_hdr_request->len_body);
	// not dealing with byte order of opaque / cas etc. as these unimplemented
	
	// don't need to check magic is request as Unbuckle will only
	// consider an inbound packet binary if magic signals REQ.

	switch (req->bin_hdr_request->opcode)
	{
	case MEMCACHED_OPCODE_GET:
		req->cmd = cmd_get;
		break;
	case MEMCACHED_OPCODE_SET:
		req->cmd = cmd_set;
		break;
	}

	// flags unimplemented but if some extras were sent, need to
	// consume them from the recvbuffer as they will be before
	// the key and the value data
	if (req->bin_hdr_request->len_extras > 0)
	{
		req->recvbuf_cur += req->bin_hdr_request->len_extras;
		req->len_rdata -= req->bin_hdr_request->len_extras;
	}
	
	// expiry would be sent as an extra but is unimplemented

	// length of the key and the value data
	// set up the lengths and the pointers to them
	req->key = req->recvbuf_cur;
	req->len_key = req->bin_hdr_request->len_key;
	
	req->recvbuf_cur += req->len_key;
	req->len_rdata -= req->len_key;
	
	req->len_data = MEMCACHED_LEN_VAL(req->bin_hdr_request);

	if (req->len_rdata > 0 && req->len_data > 0)
	{
		req->data = req->recvbuf_cur;
		req->recvbuf_cur += req->len_data;
		req->len_rdata -= req->len_data;
	}
	else if (UNLIKELY(req->len_rdata == 0 && req->len_data > 0))
	{
		// something went wrong
		return MEMCACHE_PROT_ERROR;
	}
	else
	{
		req->data = NULL;
	}

	return MEMCACHE_PROT_OK;
}

static int parse_request(struct request_state* req)
{
	int err;

	// determine whether ASCII or binary protocol data -- the first byte
	// of the receive buffer should be the appropriate magic if the binary
	// protocol is in use
	if (req->recvbuf_cur[0] == MEMCACHED_MAGIC_REQ)
	{
#ifdef DEBUG
		PRINT("[Unbuckle] the active request is a binary request\n");
#endif
		req->prot = binary;
		err = parse_binary_request(req);
		return err;
	}
	else
	{
#ifdef DEBUG
		PRINT("[Unbuckle] the active request is an ASCII request\n");
#endif
		req->prot = ascii;

		// The command format according to the memcached protocol (ASCII) is:
		// <command name> <key> <flags> <exptime> <bytes> [noreply]\r\n
		// This iteration currently ignores all but the <command name>, 
		// <key> and <bytes> fields.
		err = parse_ascii_request(req);
		return err;
	}
}

static int process_udp_header(struct request_state* req)
{
	struct memcache_udp_header* udp = 
		(struct memcache_udp_header*) req->recvbuf_cur;

	// Expecting an 8 byte header at the front of the receive buffer
	// which will need dealing with for network -> host byte order issues
	// TODO: need to deal with the case where this data could arrive corrupted

	// if not 8 bytes then did not receive enough data or request was gibberish
	if (req->len_rdata >= 8)
	{
		req->reqid = ntohs(udp->req);
		
		// need to throw away the packet if the count > 1
		if (1 < ntohs(udp->count))
		{
#ifdef DEBUG
			PRINTARGS("[Unbuckle] UDP server throwing away a packet due to >1 "
				"packet count (packet count is %d)\n", ntohs(udp->count));
#endif
			// Throw away
			return PROC_UDP_INVALID;
		}

#ifdef DEBUG
		PRINTARGS("[Unbuckle] The packet count in this UDP request is %d. "
			"The request ID is %d.\n", ntohs(udp->count), req->reqid);
#endif

		// Swing the current pointer forward 8 bytes to the start of the actual
		// memcached data. The pointer to the start of the receive buffer needs
		// to stay in tact so that no memory leak occurs when reallocation is
		// performed, but don't want the overhead of reallocating mid-request
		req->recvbuf_cur += sizeof(struct memcache_udp_header);
		req->len_rdata -= sizeof(struct memcache_udp_header);

		return PROC_UDP_VALID;
	}
	else
	{
#ifdef DEBUG
		PRINT("[Unbuckle] Throwing away a packet as the UDP header must be >8 bytes.\n");
#endif
		return PROC_UDP_INVALID;
	}
}

static void request_reset(struct request_state* req)
{
	// Resets only the fields in a request_state structure which 
	// absolutely must not persist across a request.
	req->reqid = 0L;
	req->data = NULL;
	req->len_data = 0;

	req->udpheaders = (struct memcache_udp_header*) req->sendbuf_cur;
	req->sendbuf_cur = req->sendbuf + sizeof(struct memcache_udp_header);
	req->len_sendbuf_cur = sizeof(struct memcache_udp_header);

	return;
}

int process_fastpath(struct request_state* req)
{
	int res;
	req->state = conn_proc_udp;
	
	while (req->state != conn_done)
	{
		switch (req->state)
		{
		case conn_wait:
			/* Don't wait in the state machine any longer on the socket */
			req->state = conn_done;
			break;

		case conn_proc_udp:
#ifdef DEBUG
			PRINT("[Unbuckle] UDP server state machine in conn_proc_udp\n");
#endif
			// Process the data in the req->recvbuf to extract the initial 8 bytes
			// worth of data from the memcached UDP header
			res = process_udp_header(req);
			
			// Shouldn't get a situation where insufficient data arrived and have
			// to buffer for more -- kernel doesn't allow this form of reading from
			// UDP sockets, so either it all turned up in conn_wait or none
			if (UNLIKELY(res == PROC_UDP_INVALID))
			{
				// Invalid UDP header for some reason (too few bytes, corrupt)
				// Go back to conn_done, implicitly throwing away this request
				req->state = conn_done;
			}
			else
			{
				// Processed the UDP header so move on to parsing
				req->state = conn_parse_cmd;
			}
			break;

		case conn_parse_cmd:
#ifdef DEBUG
			PRINT("[Unbuckle] UDP server state machine in conn_parse_cmd\n");
#endif
			// Attempts to parse the first line of ASCII
			res = parse_request(req);

			if (UNLIKELY(PROT_UNSUPPORTED == res))
			{
#ifdef DEBUG
				PRINT("[Unbuckle] The protocol in use is unsupported! Dropping request.\n");
#endif
				req->state = conn_done;
				break;
			}

			if (UNLIKELY(res != MEMCACHE_PROT_OK))
			{
#ifdef DEBUG
				PRINTARGS("[Unbuckle] Error %d while parsing the request.\n", res);
#endif
				// Send the state machine back to the start -- drop this request
				req->state = conn_done;
				break;
			}

			// Otherwise, the request is parsed and we proceed to conn_proc_cmd
			req->state = conn_proc_cmd;

			break;

		case conn_proc_cmd:
#ifdef DEBUG
			PRINT("[Unbuckle] UDP Server state machine in state conn_proc_cmd\n");
#endif
			process_request(req);
			if (req->state == conn_proc_cmd)
				req->state = conn_done; // some unknown error as not shifted to send
			break;

		case conn_send:
			
#ifdef DEBUG
			PRINT("[Unbuckle] UDP server state machine in state conn_send\n");
#endif
			udpserver_sendall(req);
			req->state = conn_done;
			break;

		case conn_done:
			return 0;
			break;
		case conn_quit:
			ub_sys_running = 0;
			break;
		}
	};

	return 0;
}	

int process_slowpath(struct request_state* req)
{
	/* This is the slow receive and processing path using the socket interface */
	int res;
#ifdef __KERNEL__
	DECLARE_WAITQUEUE(wait, current);
#endif

	request_reset(req);
	// The initial state will be conn_wait
	req->state = conn_wait;
	
	// Run forever and try to process data until signalled to stop, or until
	// the process is requested to stop perhaps due to some inconsistency in
	// which event ub_sys_running will be reconfigured to 0.
	
	// This is a state machine model which tracks the current state of the 
	// machine in the request_state structure (variable "req").
	while (
#ifdef __KERNEL__
		!kthread_should_stop() && 
#endif
		ub_sys_running)
	{
#ifdef __KERNEL__
		if (signal_pending(current))
			break;
#endif

		if (!req->recvbuf) 
		{
#ifdef DEBUG
			PRINT("[Unbuckle] UDP server seems to be out of memory"
				"-- could not reallocate.\n");
#endif
			req->state = conn_quit;
			break;
		}
	
		
		// In this state we wait on the UDP socket for some data to arrive
		res = udpserver_recvmsg(req);

		// Check if any data was actually returned
		if (-EAGAIN == res || -EWOULDBLOCK == res)
		{
#ifdef __KERNEL__
			// If it were blocking, it would block again, so wait on the UDP receive queue
			// for something more interesting to happen
			// TODO: change this code to use the helper functions per 
			//       http://lwn.net/Articles/22913/
			add_wait_queue(&udpserver->sock->sk->sk_wq->wait, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
#ifdef DEBUG
			PRINT("[Unbuckle] UDP listener going to sleep as return code was EAGAIN.\n");
#endif
			schedule();
#ifdef DEBUG
			PRINT("[Unbuckle] UDP listener awake. Some data may have just arrived.\n");
#endif
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&udpserver->sock->sk->sk_wq->wait, &wait);
#endif /* __KERNEL__ */
			// Go around the loop again to fetch new data
			continue;
		}
		else if (res < 0)
		{
			// Some other error happened which we were not expecting
#ifdef DEBUG
			PRINTARGS("[Unbuckle] UDP listener quitting. Unknown error %d in conn_wait", 
				res);
#endif
			goto req_error;
		}
		else
		{
#ifdef DEBUG
			PRINTARGS("[Unbuckle] UDP server received a message of %d bytes\n", res);
			PRINTARGS("[Unbuckle] Message contents were %.*s\n", res, req->recvbuf);
#endif
			// Actually got a message -- move on to processing and record the
			// number of bytes in the buffer. Set the start pointer recvbuf_cur
			// to the beginning of the buffer for data to be consumed from.
			req->recvbuf_cur = req->recvbuf;
			req->len_rdata = res;
			req->state = conn_proc_udp;
		}

		process_fastpath(req);
		request_reset(req);
	}

req_error:
	req->state = conn_quit;
	ub_sys_running = 0;
	return -1;
}


