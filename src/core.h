#ifndef UNBUCKLE_CORE_H
#define UNBUCKLE_CORE_H

#include <linux/socket.h>
#include <linux/types.h>

#include <request.h>

struct request_state;

int ub_core_run(void);
int process_request(struct request_state*);
int process_fastpath(struct request_state* req);
int process_slowpath(struct request_state* req);

#endif
