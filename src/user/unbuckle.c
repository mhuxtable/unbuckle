#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <buckets.h>
#include <core.h>
#include <db/hashtable.h>
#include <unbuckle.h>

volatile int ub_sys_running = 0;
static unsigned int ub_global_memory_limit = 65535;

static void unbuckle_exit(int sig)
{
	ub_sys_running = 0;
	return;
}

int main()
{	
	signal(SIGINT | SIGTERM, unbuckle_exit);

	printf("Unbuckle Key-Value Store starting up...\n");	

	printf("Limiting memory usage to %u MB.\n", ub_global_memory_limit);

#ifdef STORE_HASHTABLE
	ub_hashtbl_init();
#endif
	
	ub_buckets_init(ub_global_memory_limit);

	ub_sys_running = 1;

	ub_core_run();

	printf("Unloading Unbuckle...\n");
	ub_sys_running = 0;
	
#ifdef STORE_HASHTABLE
	ub_hashtbl_exit();
#endif

	ub_buckets_exit();

	return 0;
}
