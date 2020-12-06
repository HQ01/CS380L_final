#!/bin/bash
rm dst*
sync
echo 1 > /proc/sys/vm/drop_caches
/usr/bin/time -v ./test_uring src_1G dst_1G
