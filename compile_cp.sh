#!/bin/bash
gcc -I ../coreutils-8.32/lib/ -I ../coreutils-8.32/src/ -I ../coreutils-8.32/ -L ../coreutils-8.32/lib/ -L ../coreutils-8.32/src/ -o cp_uring_multi copy_uring_multi.c cp_uring_multi.c cp-hash.c extent-scan.c force-link.c selinux.c -lcoreutils -lver -lcrypt -laio -lselinux -luring
gcc -I ../coreutils-8.32/lib/ -I ../coreutils-8.32/src/ -I ../coreutils-8.32/ -L ../coreutils-8.32/lib/ -L ../coreutils-8.32/src/ -o cp_aio copy_aio.c cp_aio.c cp-hash.c extent-scan.c force-link.c selinux.c -lcoreutils -lver -lcrypt -laio -lselinux -luring
gcc -I ../coreutils-8.32/lib/ -I ../coreutils-8.32/src/ -I ../coreutils-8.32/ -L ../coreutils-8.32/lib/ -L ../coreutils-8.32/src/ -o cp_uring copy_uring.c cp_uring.c cp-hash.c extent-scan.c force-link.c selinux.c -lcoreutils -lver -lcrypt -laio -lselinux -luring
gcc -o test_uring test_uring.c -luring
