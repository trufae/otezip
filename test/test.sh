#!/bin/sh

D=tmp
MZ=$(pwd)/../mzip

init() {
	rm -rf $D && mkdir -p $D && cd $D
	echo hello > hello.txt
	echo world > world.txt
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

test_unzip() {
	init
	echo "[***] Testing zip+mzip with $1"
	zip -Z $1 test.zip hello.txt world.txt > /dev/null
	$MZ -l test.zip > files.txt
	grep hello.txt files.txt > /dev/null || error "hello.txt not found"
	grep world.txt files.txt > /dev/null || error "world.txt not found"
	mkdir data
	cd data
	$MZ -x ../test.zip > /dev/null
	diff -u hello.txt ../hello.txt || error "uncompressed hello.txt fail"
	diff -u world.txt ../world.txt || error "uncompressed hello.txt fail"
	cd ..
	fini
	return 0
}

test_zip() {
	init
	echo "[***] Testing mzip+unzip with $1 (0 = store, 1 = deflate)"
	$MZ -c test.zip hello.txt world.txt -z$1 > /dev/null
	unzip -l test.zip > files.txt
	grep hello.txt files.txt > /dev/null || error "hello.txt not found"
	grep world.txt files.txt > /dev/null || error "world.txt not found"
	pwd
	{
		mkdir data
		cd data
		unzip ../test.zip
		diff -u hello.txt ../hello.txt || error "uncompressed hello.txt fail"
		diff -u world.txt ../world.txt || error "uncompressed hello.txt fail"
		cd ..
		rm -rf data
	}
	return 0
pwd
	{
		mkdir data
		cd data
		mzip -x ../test.zip
		diff -u hello.txt ../hello.txt || error "uncompressed hello.txt fail"
		diff -u world.txt ../world.txt || error "uncompressed hello.txt fail"
		cd ..
	}
	fini
	return 0
}

# ---- #

test_unzip "deflate" || exit 1
test_unzip "store" || exit 1

test_zip "0" || exit 1
test_zip "1" || exit 1

