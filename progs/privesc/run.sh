#!/bin/bash

set -eu

mkdir -p out

cflags="-Wall -Werror -O2 -static"
g++ $cflags privesc.cc -o out/init
g++ $cflags target_prog.c -o out/target_prog

(cd out && (echo init; echo target_prog) | cpio -H newc -o) | gzip >out/initrd.gz

python qemu_runner.py
python find_ptes.py
