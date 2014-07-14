#include <db/hashtable.h>
#include <core.h>
#include <entry.h>
#include <kernel/locks.h>
#include <kernel/net/skbs.h>
#include <kernel/net/udpserver_rx.h>
#include <request.h>
#include <uberrors.h>

#include <linux/rwlock.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>

/* leave this here for now even though it's not used (stop compiler complaining) */
DEFINE_SPINLOCK(ub_kernlock);

//static DEFINE_RWLOCK(ub_kernrwlock);
struct rw_semaphore rwlock;

static int
process_get(struct request_state* req)
{
	struct ub_entry* e;
	struct sk_buff* skb;
	
	spinlock_t *bucket_lock = ub_hashtbl_get_bucket_lock(req->key, req->len_key);
	ub_hashtbl_lock_bucket(bucket_lock);
	
	e = ub_cache_find(req->key, req->len_key);

	if (!e)
	{
		req->err = -EUBKEYNOTFOUND;
		ub_hashtbl_unlock_bucket(bucket_lock);
		return req->err;
	}

	/* If we found a suitable ub_entry* e,
	   it will contain the skb to be emitted on the wire.
	   Clone the copy from the hash table under the lock so that headers can be added
	   and responsibility for it can be handed over to the NIC during the send process.
	   This keeps the critical region under the read lock as short and contained as possible. */
	skb = skb_clone(e->skb, GFP_ATOMIC);
	if (unlikely(!skb))
	{
		printk(KERN_ALERT "Couldn't clone the SKB.\n");
		req->err = -EUBKEYNOTFOUND;
		ub_hashtbl_unlock_bucket(bucket_lock);
		return req->err;
	}
	ub_hashtbl_unlock_bucket(bucket_lock);

	/* We should have found an entry, and this means we have a pointer to an skb
	   within the ub_entry struct which we can now use to send directly on the 
	   wire (after pushing some headers on the front). */
	req->skb_tx = skb;
	return 0;
}

static int
process_set(struct request_state* req)
{
	spinlock_t *bucket_lock = ub_hashtbl_get_bucket_lock(req->key, req->len_key);
	
	ub_hashtbl_lock_bucket(bucket_lock);
	req->err = ub_cache_replace(req->key, req->len_key, req->data, req->len_data);
	ub_hashtbl_unlock_bucket(bucket_lock);

	/* TODO: this need not generate a new skb on every run, but for now it's simpler
	         to do it this way */
	req->skb_tx = ub_skb_set_up(32);
	if (req->err == 0)
		ub_push_data_to_skb(req->skb_tx, "STORED\r\n", strlen("STORED\r\n"));
	else if (req->err == -ENOMEM)
		ub_push_data_to_skb(req->skb_tx, "NOT_STORED\r\n", strlen("NOT_STORED\r\n"));
	else
	{
		char errstring[20];
		int len_errstring = snprintf(&errstring[0], 20, 
			"SERVER ERROR %d\r\n", req->err);
		ub_push_data_to_skb(req->skb_tx, errstring, len_errstring);
	}

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

	struct request_state* req = kmalloc(sizeof(struct request_state), 
		GFP_KERNEL);
	allow_signal(SIGKILL | SIGSTOP);

	if (!req
#ifdef __KERNEL__
		|| ksize(req) < sizeof(struct request_state)
#endif
		)
		return -ENOMEM;
	memset(req, 0, sizeof(struct request_state));
	
	req->len_recvbuf = 4096;
	req->recvbuf = (char*) kmalloc(req->len_recvbuf, GFP_KERNEL);
	if (ksize(req->recvbuf) < req->len_recvbuf)
	{
		printk(KERN_WARNING 
			"[Unbuckle] Asked for a receive buffer of %lu, but got %lu.\n",
			req->len_recvbuf, ksize(req->recvbuf)
		);
		req->len_recvbuf = ksize(req->recvbuf);
	}

	do_kernel_rx_worker(req);

	if (req)
	{
		if (req->recvbuf)
			kfree(req->recvbuf);
		kfree(req);
		req = NULL;
	}
	res = 0;
	return res;
}
