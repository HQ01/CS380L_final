#!/bin/bash
mkdir recursive_1G;
cd recursive_1G;
for n in {1..1000}; do
	dd if=/dev/urandom of=file$( printf %03d "$n" ).bin bs=1 count=$((1024 * 1024))
done
