#ifndef UDPSERVER_H
#define UDPSERVER_H

#ifdef __KERNEL__
#include <linux/in.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/wait.h>
#else
#include <netinet/in.h>
#endif

#include <request.h>

#define UDP_MAX_SEND_BYTES	65535
#define UDP_PORT	11211
#define UDP_RECV_BUFFER 65536
#define UDP_SEND_BUFFER 1500


struct udpserver_state {
#ifdef __KERNEL__
	struct socket* sock;
	struct task_struct* listener;
#else
	int sock;
#endif
	struct sockaddr_in inet_addr;
	struct request_state* req;
};

// State of the UDP server for tracking threads and sockets etc.
extern struct udpserver_state* udpserver;

int udpserver_recvmsg(struct request_state* req);
int udpserver_sendall(struct request_state* req);
int add_udp_headers(struct request_state* req);
int  udpserver_start(struct request_state* req);
void udpserver_exit(void);

int udpserver_init_sendbuffers(struct request_state* req);
void udpserver_free_sendbuffers(struct request_state* req);

#endif	/* UDPSERVER_H */
