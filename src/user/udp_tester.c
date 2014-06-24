#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#include <prot/memcached.h>

volatile int stop = 1;

void INThandler(int sig)
{
	stop = 0;
}

int main(int argc, char* argv[])
{
	int sock;
	struct sockaddr_in their_addr;
	struct sockaddr_in our_addr;
	struct timeval time;
	
	long microsec_end, microsec_start, microsec_avg = 0;
	
	int loop = 100000;
	
	signal(SIGINT, INThandler);

	if (argc < 3)
	{
		printf("ERROR: Usage is %s server_IP OP key [value]\n"
			" where OP is one of SET or GET \n"
			" with SET, a key and a value is required\n"
			" with GET, only a key is required (any value supplied will be ignored)\n", 
			argv[0]);
		return -1;
	}

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	struct timeval ts;
	ts.tv_sec = 1;
	ts.tv_usec = 0;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &ts, sizeof(ts)) < 0) {
		perror("Error");
		return -1;
	}

	their_addr.sin_family = AF_INET;
	their_addr.sin_addr.s_addr = inet_addr(argv[1]);
	their_addr.sin_port = htons(11211);

	our_addr.sin_family = AF_INET;
	our_addr.sin_addr.s_addr = INADDR_ANY;
	our_addr.sin_port = 0;

	bind(sock, (struct sockaddr*)&our_addr, sizeof(struct sockaddr_in));
	connect(sock, (struct sockaddr*) &their_addr, sizeof(their_addr));
	
	const char* key = argv[3];
	int lkey = strlen(key);
	
	int opcode;
	if (!strcmp(argv[2], "SET"))
		opcode = MEMCACHED_OPCODE_SET;
	else if (!strcmp(argv[2], "GET"))
		opcode = MEMCACHED_OPCODE_GET;
	else 
	{
		// Fail
		printf("OP must be one of GET or SET\n");
		return -10;
	}
	
	char* extras = 0;
	int  lextras = 0;
	
	char* val = (opcode == MEMCACHED_OPCODE_SET ? argv[4] : 0);
	int  lval = (val ? strlen(val) : 0);

	loop = (MEMCACHED_OPCODE_GET == opcode ? 1 : loop);
	printf("looping %d times\n", loop);
	int i;	
	for (i = 0; i < loop; i++)
	{
		if (!stop)
			break;
		char key_thisiter[30];
		if (MEMCACHED_OPCODE_SET == opcode) 
			sprintf(key_thisiter, "%s.%d", key, i);
		else
			strncpy((char*)&key_thisiter, key, 30);
			
		lkey = strlen(key_thisiter);
		printf("%s\n", key_thisiter);
		
		struct memcache_hdr_req* req = 
			memcached_produce_request(opcode,extras,lextras,key_thisiter,lkey,val,lval);

		char buf[64];
		
		gettimeofday(&time, NULL);
		microsec_start = ((unsigned long long) time.tv_sec * 1000000) + time.tv_usec;
		printf("%lu Sending message to kernel \n", microsec_start);

		sendto(sock, req, MEMCACHED_PKT_REQ_LEN(req->len_body), 0, 
			(struct sockaddr*) &their_addr, sizeof(their_addr)); 
		int bytes = recv(sock, &buf, 64, 0);
		
		gettimeofday(&time, NULL);
		microsec_end = ((unsigned long long) time.tv_sec * 1000000) + time.tv_usec;
		printf("%lu Received a reply from kernel\n"
			"** TIME TAKEN: %lu ** \n", microsec_end, (microsec_end - microsec_start));
		
		microsec_avg += (microsec_end - microsec_start);

		char mybuf[64];
		memcpy(&mybuf, &buf, 64);

		if (bytes > 0)
		{
			printf("Received %d bytes from kernel\n\n", bytes);
			struct memcache_hdr_res* res = (struct memcache_hdr_res*) &mybuf;
			printf(" Received reply \n"
				"  opcode %d \n"
				"  magic %#08X \n"
				"  len_key %d \n "
				"  len_extras %d \n "
				"  status %d \n "
				"  len_body %d \n "
				"  key %.*s \n "
				"  val %.*s \n ",
				res->opcode,
				res->magic,
				res->len_key,
				res->len_extras,
				res->status,
				res->len_body,
				res->len_key, (char*) MEMCACHED_PKT_KEY(res, res->len_extras),
				MEMCACHED_LEN_VAL(res), 
					(char*) MEMCACHED_PKT_VALUE(res, res->len_extras, res->len_key)
			);
		}

		free(req);
	}

	printf("\n ----------------------------------------- \n"
		" +++ Average time: %lu +++ \n ",
		microsec_avg / i);

	close(sock);
	
	return 0;
}
