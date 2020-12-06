#!/bin/bash
rm dst*
sync
echo 1 > /proc/sys/vm/drop_caches
/usr/bin/time -v ./cp src_1G dst_1G
