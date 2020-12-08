Optimize cp -r with io_uring: an empirical study
====
This is a course project of **CS380L Advanced Operating System** in UT Austin. 

Abstract
----
We implemented a modified version of GNU Core Utilities' cp program that supports asynchronous, non-blocking I/O by leveraging Linux's latest io_uring interface. By vectoring I/O submissions, processing I/O submission/completion events asynchronously, and reducing memory copying across the kernel boundary, the modified cp program (cp_uring) can process multiple I/O events concurrently in an efficient way, even across files. Our experiments show that cp_uring enables visible performance improvement over coreutil's cp both in copying single file and copying directory recursively. We also investigated hyper-parameter choice's influence on cp_uring's performance, as well as the scalability of its performance advantage over cp.

Requirements
----
- kernel version >= 5.1
- [liburing](https://github.com/axboe/liburing)

How to run it
----
Change ```DIR_COREUTILS``` to the directory of compiled coreutils, ```DIR_SRC``` to the directory of this repo, and then run the following script to compile
```
#!/bin/bash
DIR_COREUTILS=~/coreutils-8.32
DIR_SRC=~/project/CS380L_final
gcc -I ${DIR_COREUTILS}/lib/ -I ${DIR_COREUTILS}/src/ -I ${DIR_COREUTILS} -L ${DIR_COREUTILS}/lib/ -L ${DIR_COREUTILS}/src/ -o cp_uring_multi ${DIR_SRC}/copy_uring_multi.c ${DIR_SRC}/cp_uring_multi.c ${DIR_SRC}/cp-hash.c ${DIR_SRC}/extent-scan.c ${DIR_SRC}/force-link.c ${DIR_SRC}/selinux.c -lcoreutils -lver -lcrypt -laio -lselinux -luring
```
Run ```./cp_uring_multi``` with same arguments and options as ```cp```

Patches
----
The patches for ```cp_uring``` are in patch directory
