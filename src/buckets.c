/** Unbuckle bucket allocation system
    
	This is a slab-allocation like approach to attempting to minimise external 
	fragmentation, and for attempting to amortise the cost of allocating 
	memory to store items in the cache on the fly. Allocation can be considerably 
	expensive if performed for every request and has a significant impact on the
	request latency. Early measurements on a ~3 year old Intel Core i5 "Sandy Bridge" 
	chip reported a ~1000 cycle delay in calling kmalloc for just a small (~1KB) 
	memory request.

	A set of one or more buckets is maintained. Memory requirements are split into
	discrete, disjoint blocks based on an initial size and a growth factor (this 
	is inspired by memcached, and was chosen to allow for direct comparisons more
	fairly). Each bucket maintains a set of pointers to one or more "pages" of 
	memory (this is an internal page concept, and is not a 1:1 correspondence with
	pages of memory at the level of the hardware/elsewhere in the kernel).

	Allocating an object in the cache then involves:

	1. Find the bucket which the data should be placed into, based on the data size
	   being <= item size for that bucket.
	
	2. Find a spare place in one of that bucket's pages, or assign a new page to the
	   bucket if all of its pages are full. (Error if no more pages can be assigned
	   because the memory limit has been reached. Someone else needs to find something
	   to be freed so the new data can be stored).

	3. Write the data to the location specified.

	4. Repeat, ad infinitum.

	*/

#include <abstract.h>
#include <buckets.h>

#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdlib.h>
#endif

#define UB_PAGE_SIZE 1048576
#define UB_MAX_BUCKETS 16
#define UB_MAX_PAGES 32
#define UB_BUCKET_MIN_SIZE 512
#define UB_GROWTH_FACTOR 1.25

static size_t memory_limit;
static size_t memory_used = 0;

struct bucket
{
	size_t itemsize;    /* size of the items to go into this bucket <= itemsize  */

	int    items_max;   /* the maximum number of items per page                  */
	int page_items_cur; /* how many items in the current page?                   */

	void** pages;
	void*  page_cur;    /* which location should be written to next? */
	int    pages_space; /* how much space is in void** pages array for pointers? */
	int    pages_alloc; /* how many pages do we have in the void** pages array?  */
};

/* pointers to pages are stored in this array until they are assigned to a bucket */
static void* pages[UB_MAX_PAGES];
static int pages_avail = 0;

/* Array of buckets */
static struct bucket buckets[UB_MAX_BUCKETS];

/* determine whether the pages allocated so far consume the memory budget */
static inline int pages_out_of_memory(void)
{
	return (memory_used >= memory_limit) ? -1 : 0;
}

/* add more pages to the pages[] array if it is empty */
static int pages_add(void)
{
	int pagecount;

	/* don't allocate if there's still pages to be allocated */
	if (pages_avail > 0)
		return 0;

	/* don't allocate if the system has consumed the memory quota */
	if (pages_out_of_memory())
		return -ENOMEM;

	for (pagecount = 0; pagecount < UB_MAX_PAGES; pagecount++)
	{
		void* page = ALLOCMEM(UB_PAGE_SIZE, GFP_KERNEL);
#ifdef __KERNEL__
		/* ksize(...)-esque checks have no analogue in userspace */
		if (ksize(page) < UB_PAGE_SIZE)
		{
#ifdef DEBUG
			PRINTARGS(KERN_WARNING "[Unbuckle] Asked for pages of size %lu, "
				"but got %lu.", UB_PAGE_SIZE, ksize(page));
#endif
			return -EFAULT;
		}
#endif
		pages[pagecount] = page;

		memory_used += UB_PAGE_SIZE;
		pages_avail++;
	}

	return 0;
}

/* get a page from the back of the free pool, and add more if we're all out */
static void* page_get(void)
{
	if (pages_avail == 0)
		if (pages_add() < 0)
			return NULL;
	return pages[--pages_avail];
}

/* free the pool of scratch pages which have not yet been assigned */
static void pages_free_scratch(void)
{
	int page;

	for (page = 0; page < pages_avail; page++)
	{
		FREEMEM(pages[page]);
		pages[page] = NULL;
	}

	return;
}

/* adds a page to a bucket, possibly because it has used up all of its current
   allocations */
static int bucket_add_page(int bucket)
{
	void* page;

	if (bucket < 0 || bucket > UB_MAX_BUCKETS)
		return -EFBIG;
	
	/* can't do it if we've used the memory quota and have no spare pages */
	if (pages_out_of_memory() && pages_avail == 0)
		return -ENOMEM;
	
	/* enough space in the bucket for another page pointer? */
	if (buckets[bucket].pages_space == buckets[bucket].pages_alloc)
	{
		buckets[bucket].pages = REALLOCMEM(buckets[bucket].pages, 
			buckets[bucket].pages_space * sizeof(void*) * 2, GFP_KERNEL);
		buckets[bucket].pages_space *= 2;
	}

	/* add a page to the next position available in the bucket */
	page = page_get();

	if (!page)
		return -ENOMEM;
	
	buckets[bucket].pages[buckets[bucket].pages_alloc] = page;
	buckets[bucket].pages_alloc++;
	buckets[bucket].page_cur = page;
	buckets[bucket].page_items_cur = 0;

	return 0;
}
	
static int buckets_init(void)
{
	int bucket;
	size_t bucket_size = UB_BUCKET_MIN_SIZE;

	for (bucket = 0; bucket < UB_MAX_BUCKETS; bucket++)
	{
		buckets[bucket].itemsize = bucket_size;

		/* the maximum number of items which can be stored in a single page is 
		   the integer part of the following division */
		buckets[bucket].items_max = UB_PAGE_SIZE / bucket_size;

		buckets[bucket].page_items_cur = 0;
		
		/* pointer to an array of pointers to pages, initially allow 8 pages to
		   be tracked in this array*/
		buckets[bucket].pages_space = 8;
		buckets[bucket].pages_alloc = 0;
		buckets[bucket].pages = 
			ALLOCMEM(sizeof(void*) * buckets[bucket].pages_space, GFP_KERNEL);

		/* assign a single page to this bucket to begin with to optimise the 
		   first case */
		bucket_add_page(bucket);

		bucket_size *= UB_GROWTH_FACTOR;
	}
	return 0;
}

/* get the index of the bucket which should store some data of a given size */
static int bucket_get_id(size_t len_data)
{
	int bucket;
	size_t bucket_size = UB_BUCKET_MIN_SIZE;

	for (bucket = 0; bucket < UB_MAX_BUCKETS; bucket++)
	{
		if (bucket_size >= len_data)
			return bucket;

		bucket_size *= UB_GROWTH_FACTOR;
	}
	return -1;
}

/* free all the bucket data, including assigned pages and the pages array */
static void buckets_free_all(void)
{
	int bucket;
	for (bucket = 0; bucket < UB_MAX_BUCKETS; bucket++)
	{
		int page;
		for (page = 0; page < buckets[bucket].pages_alloc; page++)
		{
			FREEMEM(buckets[bucket].pages[page]);
			buckets[bucket].pages[page] = NULL;
		}

		FREEMEM(buckets[bucket].pages);
		buckets[bucket].pages = NULL;

		buckets[bucket].pages_space = 0;
		buckets[bucket].pages_alloc = 0;
		buckets[bucket].page_cur = NULL;
		buckets[bucket].page_items_cur = 0;
	}
	return;
}

/* External interface to the bucket allocator, which returns a pointer to a 
   location into which data may be written provided the size of data written
   does not exceed the value specified in the function call. (Behaviour undefined
   if this is not respected, but your data will most likely get lost at best,
   and the kernel will panic at worst) */
int ub_buckets_alloc(size_t len_buffer, void** location)
{
	int bucket = bucket_get_id(len_buffer);

	if (bucket < 0)
		return -EFBIG;
	
	/* The bucket must have free space, or we must be able to expand it by 
	   adding another page. Otherwise, the cache is out of space. */
	if (buckets[bucket].items_max == buckets[bucket].page_items_cur)
	{
		int err = bucket_add_page(bucket);
		if (err < 0)
			return err;
	}
	
	*location = buckets[bucket].page_cur;
	buckets[bucket].page_items_cur++;
	buckets[bucket].page_cur += buckets[bucket].itemsize;

	return 0;
}

/* initialises buckets and pages at startup, limiting memory to somewhere 
   approximately around memory_limit */
int ub_buckets_init(size_t memlim)
{
	/* note that the module takes this size in MiB for convenience purposes, but
	   it needs to be in bytes internally within the bucket allocator */
	memory_limit = memlim * 1024 * 1024;
	buckets_init();
	return 0;
}

/* deallocate pages and buckets which were allocated throughout the running of the 
   cache -- called on exit and might take a while */
void ub_buckets_exit(void)
{
	buckets_free_all();
	pages_free_scratch();
	return;
}
