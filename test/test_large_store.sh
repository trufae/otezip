#!/bin/sh

D=tmp
MZ=./build/mzip

init() {
	rm -rf $D && mkdir -p $D && cd $D
	# Create a 1MB random file
	dd if=/dev/urandom of=large.bin bs=1k count=1024 2>/dev/null || error "cannot create large file"
}

fini() {
	echo "ok"
	cd ..
	rm -rf $D
}

error() {
	echo "$@"
	exit 1
}

init
echo "[***] Testing large file (1MB) with store"
rm -f test.zip
$MZ -c test.zip large.bin -z store || error "mzip failed for -z store"
unzip -l test.zip > files.txt || error "unzip -l failed"
grep "large.bin" files.txt >/dev/null || error "large.bin missing (-z store)"
mkdir -p data && cd data
$MZ -x ../test.zip >/dev/null || error "mzip -x failed (-z store)"
cmp -s large.bin ../large.bin || error "large.bin mismatch (-z store)"
cd .. && rm -rf data
fini
