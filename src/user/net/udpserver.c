#include <abstract.h>
#include <net/udpserver.h>
#include <request.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

int udpserver_init_sendbuffers(struct request_state* req)
{
	req->len_sendbuf = UDP_SEND_BUFFER;
	req->sendbuf = (char*) ALLOCMEM(req->len_sendbuf, GFP_KERNEL);
	req->sendbuf_cur = req->sendbuf;
	
	if (!req->sendbuf)
		return -1;
	
	return 0;
}

void udpserver_free_sendbuffers(struct request_state* req)
{
	if (req->sendbuf)
	{
		FREEMEM(req->sendbuf);
		req->sendbuf = NULL;
		req->sendbuf_cur = NULL;
		req->len_sendbuf = 0;
		req->len_sendbuf_cur = 0;
	}
	return;
}

static int udpserver_sendmsg(struct request_state* req, struct msghdr* msg, 
	size_t len_data) 
{
	// Sends a message back to the sender of the active request
	int err;
	
#ifdef DEBUG
	PRINT("[Unbuckle] Sending a message.\n");
#endif

	err = sendto(udpserver->sock, req->sendbuf, len_data, 0, 
		(struct sockaddr*) &req->sockaddr, sizeof(struct sockaddr_in));

#ifdef DEBUG
	if (err < 0)
	{
		PRINTARGS("[Unbuckle] encountered error %d writing a UDP response\n", errno);
	}
#endif
	
	return err;
}

int udpserver_sendall(struct request_state* req)
{
	int err;
	struct iovec iov;
	
	add_udp_headers(req);

	iov.iov_base = req->sendbuf;
	iov.iov_len  = req->len_sendbuf_cur;

	req->msg.msg_iov = (struct iovec*) &iov;
	req->msg.msg_iovlen = 1;
	req->msg.msg_name = &req->sockaddr;
	req->msg.msg_namelen = sizeof(struct sockaddr_in);
	
	// Send the message header
	err = udpserver_sendmsg(req, &req->msg, req->len_sendbuf_cur);

	if (err < 0)
	{
		printf("[Unbuckle] Encountered an error sending a message %d", errno);
	}
	
	return err;
}
