KERNEL_OBJ = bin/kernel/unbucklekv

obj-m = $(KERNEL_OBJ).o
EXTRA_CFLAGS += -I$(PWD)/src 

include $(PWD)/Unbuckle.makeopts

$(KERNEL_OBJ)-objs += src/kernel/unbuckle.o
$(KERNEL_OBJ)-objs += src/buckets.o
$(KERNEL_OBJ)-objs += src/core.o
$(KERNEL_OBJ)-objs += src/kernel/core.o
$(KERNEL_OBJ)-objs += src/kernel/db/linklist.o
$(KERNEL_OBJ)-objs += src/kernel/db/spooky/spooky_hash.o
$(KERNEL_OBJ)-objs += src/kernel/entry.o
$(KERNEL_OBJ)-objs += src/kernel/net/udpserver.o
$(KERNEL_OBJ)-objs += src/kernel/net/udpserver_low.o
$(KERNEL_OBJ)-objs += src/kernel/net/udpserver_send.o
$(KERNEL_OBJ)-objs += src/net/udpserver.o
$(KERNEL_OBJ)-objs += src/prot/memcached.o

ifeq ($(HASHTABLE_VERSION),KHASH)
	UB_C_OPTS += -D HASHTABLE_KHASH
	$(KERNEL_OBJ)-objs += src/kernel/db/khash.o
else
	UB_C_OPTS += -D HASHTABLE_UTHASH
	$(KERNEL_OBJ)-objs += src/kernel/db/uthash.o
endif
	
KDIR=/lib/modules/$(shell uname -r)/build

EXTRA_CFLAGS += $(UB_C_OPTS)

all: 
	mkdir -p bin/kernel
	mkdir -p bin/user
	make -C $(KDIR) M=$(PWD) modules

user:
	make --file Makefile.user

clean:
	find bin/kernel/ -mindepth 1 -delete
	find bin/user/ -mindepth 1 -delete
	find src/ -name "*.o" -delete
	find src/ -name ".*.o.cmd" -delete
	rm -Rf .tmp_versions/
	rm -f modules.order
	rm -f Module.symvers
