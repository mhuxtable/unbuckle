Unbuckle
====================================

_Unbuckle_ is an in-kernel key-value store based upon the 
[memcached](http://memcached.org/) [protocol](https://github.com/memcached/memcached/blob/master/doc/protocol.txt).
It is implemented as a platform-independent Linux kernel module,
making it compatible with any machine capable of running recent versions of the Linux kernel.
It is designed to improve the performance of existing `memcached` installations by acting as a drop-in replacement
requiring few modifications to existing application code.

The project was originally built in partial fulfilment of the requirements of 
Part II of the Computer Science Tripos for my undergraduate degree at the 
[University of Cambridge](http://www.cam.ac.uk) [Computer Laboratory](http://www.cl.cam.ac.uk/).
The write-up of the work, with lots of gory technical details and a performance analysis against `memcached` and other systems,
is [available online](http://www.cl.cam.ac.uk/~ms705/projects/dissertations/2014-mjh233-unbuckle.pdf).

Special efforts have been made to optimise the key-value store for low-level kernel interfaces
and to benefit from the availability of core system data structures. In particular:

* Network communication is optimised via use of a custom network stack, which bypasses the socket interface and traditional kernel UDP processing code.
  A [_netfilter_ hook](http://en.wikipedia.org/wiki/Netfilter) is used to intercept IP packets destined for the key-value store as they rise up the network stack.
  The transmit path emits packets using the `dev_queue_xmit()` kernel interface. Hence, the TX and RX paths are not specialised to any particular network driver or NIC. 

* Pre-constructed socket buffer structures ([`struct sk_buff`](http://www.linuxfoundation.org/collaborate/workgroups/networking/sk_buff))
  are used for back-end data storage in the hash table,
  unifying the process of retrieving and sending replies on the network.

System Requirements
-------------------

Any hardware which supports both memcached and can run the Linux kernel should be supported, but note the following constraints:

* __NUMA__: no optimisations for Non-Uniform Memory Access (NUMA) memory hierarchies have yet taken place (it's on the TODO list).
	    Our (limited) experience in running _Unbuckle_ on such architectures indicates performance is likely to be suboptimal.

* __Linux kernel__: to the best of our knowledge, we support all recent Linux kernel versions since 3.10.2, and have tested against 3.10.2 and 3.14. 
In particular, there is a dependency on the [Linux kernel hash table](http://lwn.net/Articles/510202/), which was only recently introduced.

Compiling
-------------------

`make` compiles the module into `bin/kernel/unbucklekv.ko`. Insert the module into a running kernel using `insmod bin/kernel/unbucklekv.ko`. 

Note that a __user-space version__ of the store is also available.
This version uses almost identical code,
modulo user-space vs. kernel-specific interface calls and the consequent performance impediment due to the need for user-space to make system calls 
while the kernel does not.
To compile in this mode, execute `make user` to compile and link a binary in `bin/user/unbuckle`.

`make clean` will remove all output files from the source tree.

Notes
-------------------

* __memcached protocol support__: at present, only `GET` and `SET` requests are supported. 
  We later hope to add support for other request types, in particular, multi-`GET`s.

* __UDP only__: implementing a full custom TCP server is a sizable project which is currently relegated to a TODO.
