#include <abstract.h>
#include <net/udpserver.h>
#include <request.h>

#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/types.h>
#include <net/sock.h>

#include <kernel/net/udpserver_low.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

struct udpserver_state* udpserver = NULL;

int add_udp_headers(struct request_state* req)
{
	/* fill in headers in the space left for us at req->udpheaders */
	struct memcache_udp_header* udp = req->udpheaders;
	
	// TODO: this needs to deal with fragmentation of UDP packets for > TXBUF size
	udp->req = htons(req->reqid);
	udp->seq = htons(0);
	udp->count = htons(1);
	udp->reserved = 0x0;

#ifdef DEBUG
	PRINT("[Unbuckle] Adding UDP headers to an outbound message.\n");
	PRINTARGS("[Unbuckle] UDP headers: \n"
		"*Req ID: %d\n"
		"*Seq Num: %d\n"
		"*Count: %d\n"
		"*Reserved: %d\n",
		ntohs(udp->req),
		ntohs(udp->seq),
		ntohs(udp->count),
		ntohs(udp->reserved)
	);
#endif

	return 0;
}

// receive a message on the UDP socket (non-blocking); wait on the wait queue 
// until something becomes available, if necessary
int udpserver_recvmsg(struct request_state* req)
{
	struct iovec iov;
	iov.iov_base = req->recvbuf;
	iov.iov_len  = req->len_recvbuf;

	// Set callback data (sender's IP and port etc.) for sending the reply
	req->msg.msg_name = &req->sockaddr;
	req->msg.msg_namelen = sizeof(struct sockaddr_in);
	req->msg.msg_control = 0;
	req->msg.msg_controllen = 0;
		
	// TODO: use smaller buffers and trap errors when recvmsg says the buffer is too small
#ifdef __KERNEL__
	return kernel_recvmsg(udpserver->sock, &req->msg, (struct kvec*) &iov, 1, 
		iov.iov_len, MSG_DONTWAIT);
#else
	/* not non blocking in the case of a userland process which can be killed */
	req->msg.msg_iov = &iov;
	req->msg.msg_iovlen = 1;
	return recvmsg(udpserver->sock, &req->msg, 0);
#endif
}

int udpserver_start(struct request_state* req)
{
	int err;
	
	// wait queue declare -- this used for waiting on the UDP socket 
	// when there is no data available to read in to stop sitting in
	// a tight spin locking loop.
	udpserver = (struct udpserver_state*) ALLOCMEM(sizeof(struct udpserver_state), 
		GFP_KERNEL);
	memset(udpserver, 0, sizeof(struct udpserver_state));
	udpserver->req = req;

	// Create and initialise a UDP socket for this connection
#ifdef __KERNEL__
	err = sock_create_kern(AF_INET, SOCK_DGRAM, IPPROTO_UDP, &udpserver->sock);
	if (err < 0)
	{
		printk(KERN_ERR "[Unbuckle] UDP Server -- %d unable to create socket\n", err);
		kfree(udpserver);
		return err;
	}
#else
	udpserver->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udpserver->sock < 0)
	{
		printf("[Unbuckle] UDP Server -- %d unable to create socket\n", errno);
		free(udpserver);
		return udpserver->sock;
	}
#endif

	udpserver->inet_addr.sin_family      = AF_INET;
	udpserver->inet_addr.sin_port        = htons(UDP_PORT);
	udpserver->inet_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* bind the socket */
#ifdef __KERNEL__
	err = kernel_bind(udpserver->sock, (struct sockaddr*) &udpserver->inet_addr, 
		sizeof(struct sockaddr_in));
	if (err < 0)
	{
		printk(KERN_ERR "[Unbuckle] UDP Server -- %d unable to bind socket\n", err);
		sock_release(udpserver->sock); 
		kfree(udpserver);
		return err;
	}
#else
	err = bind(udpserver->sock, (struct sockaddr*) &udpserver->inet_addr, 
		sizeof(struct sockaddr_in));
	if (err < 0)
	{
		printf("[Unbuckle] UDP Server -- %d unable to bind socket\n", errno);
		close(udpserver->sock);
		free(udpserver);
		return err;
	}
#endif
	
	
	req->len_recvbuf = UDP_RECV_BUFFER;
	req->recvbuf = (char*) ALLOCMEM(req->len_recvbuf, GFP_KERNEL);
#ifdef __KERNEL__
	if (ksize(req->recvbuf) < req->len_recvbuf)
	{
		printk(KERN_WARNING 
			"[Unbuckle] Asked for a receive buffer of %lu, but got %lu.\n",
			req->len_recvbuf, ksize(req->recvbuf)
		);
		req->len_recvbuf = ksize(req->recvbuf);
	}
#endif

	udpserver_init_sendbuffers(req);

	return err;
}

void udpserver_exit(void)
{
	if (udpserver)
	{
		if (udpserver->sock) 
		{
#ifdef DEBUG
			PRINT("[Unbuckle] UDP Server -- releasing open UDP socket.\n");
#endif
#ifdef __KERNEL__
			sock_release(udpserver->sock);
			udpserver->sock = NULL;
#else
			close(udpserver->sock);
			udpserver->sock = 0;
#endif
		}

		// Empty the receive buffer as the state machine has stopped
		if (udpserver->req->recvbuf)
		{
			FREEMEM(udpserver->req->recvbuf);
			udpserver->req->recvbuf = NULL;
		}
		udpserver_free_sendbuffers(udpserver->req);

		FREEMEM(udpserver);
		udpserver = NULL;
	}
}
