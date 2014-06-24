

#ifdef __KERNEL__

#define ALLOCMEM(size, flags) kmalloc(size, flags)
#define REALLOCMEM(ptr, size, flags) krealloc(ptr, size, flags)
#define FREEMEM(ptr) kfree(ptr)
#define PRINT(msg) printk(msg)
#define PRINTARGS(msg, ...) printk(msg, __VA_ARGS__)
#define STRNICMP(...) strnicmp(__VA_ARGS__)
#define UNLIKELY(arg) unlikely(arg)

#else
/* following are just for flags in kmalloc calls so we have a definition of something */
#define GFP_KERNEL 0

#define ALLOCMEM(size, flags) malloc(size)
#define REALLOCMEM(ptr, size, flags) realloc(ptr, size)
#define FREEMEM(ptr) free(ptr)
#define PRINT(msg) printf(msg)
#define PRINTARGS(msg, ...) printf(msg, __VA_ARGS__)
#define STRNICMP(...) strncasecmp(__VA_ARGS__)
#define UNLIKELY(arg) arg

#endif
