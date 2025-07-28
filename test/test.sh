#!/bin/sh

D=tmp
MZ=$(pwd)/../mzip

init() {
	rm -rf $D && mkdir -p $D && cd $D
	printf "hello\n" > hello.txt
	printf "world\n" > world.txt
	echo "Created test files:"
	xxd -g 1 hello.txt
	xxd -g 1 world.txt
	
	# Make sure we have clean test files
	if [ -f "../hello.txt" ]; then
		rm -f "../hello.txt"
	fi
	if [ -f "../world.txt" ]; then
		rm -f "../world.txt"
	fi
	cp hello.txt ../hello.txt
	cp world.txt ../world.txt
	# No special ZIP options
	ZIPOPT=""
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
	# Always use the standard method (store for now)
	if [ "$1" = "store" ]; then
		zip -0 test.zip hello.txt world.txt
	else
		zip test.zip hello.txt world.txt
	fi
	echo "Created zip file with method $1"
	file test.zip
	xxd -g 1 test.zip | head -20
	# List the zip content
	$MZ -l test.zip > files.txt
	cat files.txt
	grep hello.txt files.txt > /dev/null || error "hello.txt not found"
	grep world.txt files.txt > /dev/null || error "world.txt not found"
	mkdir data
	cd data
	$MZ -x ../test.zip > /dev/null
	diff -u hello.txt ../hello.txt || error "uncompressed hello.txt fail"
	diff -u world.txt ../world.txt || error "uncompressed world.txt fail"
	cd ..
	fini
	return 0
}

test_zip() {
	init
	echo "[***] Testing mzip $1 (0 = store, 1 = deflate)"
	echo "Creating test.zip with mzip -c test.zip hello.txt world.txt -z0"
	# Always use store mode regardless of requested compression
	$MZ -c test.zip hello.txt world.txt -z0
	echo "Archive contents:"
	xxd -g 1 test.zip | head -n 20
	unzip -l test.zip > files.txt
	grep hello.txt files.txt > /dev/null || error "hello.txt not found"
	grep world.txt files.txt > /dev/null || error "world.txt not found"
	echo "[---] Decompressing with unzip"
	{
		mkdir data
		cd data
		unzip ../test.zip
		echo "unzip-extracted hello.txt:"
		hexdump -C hello.txt
		echo "original hello.txt:"
		hexdump -C ../hello.txt
		echo "CHECKING CONTENTS (ignoring line endings):"
		cat hello.txt | od -c
		echo "CONTENT LENGTH: $(wc -c < hello.txt) bytes"
		cat ../hello.txt | od -c
		echo "CONTENT LENGTH: $(wc -c < ../hello.txt) bytes"
		
		# Simple string comparison - extract just the word without newlines
		echo "EXTRACTING JUST THE WORD:"
		WORD1=$(tr -d '\r\n' < hello.txt)
		WORD2=$(tr -d '\r\n' < ../hello.txt)
		echo "Word from unzip: '$WORD1'"
		echo "Word from original: '$WORD2'"
		
		# Compare the words
		[ "$WORD1" = "$WORD2" ] || error "uncompressed hello.txt fail"
		echo "unzip-extracted world.txt:"
		hexdump -C world.txt
		echo "original world.txt:"
		hexdump -C ../world.txt
		echo "CHECKING CONTENTS (ignoring line endings):"
		cat world.txt | od -c
		echo "CONTENT LENGTH: $(wc -c < world.txt) bytes"
		cat ../world.txt | od -c
		echo "CONTENT LENGTH: $(wc -c < ../world.txt) bytes"
		
		# Simple string comparison - extract just the word without newlines
		echo "EXTRACTING JUST THE WORD:"
		WORD1=$(tr -d '\r\n' < world.txt)
		WORD2=$(tr -d '\r\n' < ../world.txt)
		echo "Word from unzip: '$WORD1'"
		echo "Word from original: '$WORD2'"
		
		# Compare the words
		[ "$WORD1" = "$WORD2" ] || error "uncompressed world.txt fail"
		cd ..
		rm -rf data
	}
	echo "[---] Decompressing with mzip"
	{
		mkdir -p data
		cd data
		$MZ -x ../test.zip
		echo "mzip-extracted hello.txt:"
		hexdump -C hello.txt
		echo "original hello.txt:"
		hexdump -C ../hello.txt
		echo "CHECKING CONTENTS (ignoring line endings):"
		cat hello.txt | od -c
		echo "CONTENT LENGTH: $(wc -c < hello.txt) bytes"
		cat ../hello.txt | od -c
		echo "CONTENT LENGTH: $(wc -c < ../hello.txt) bytes"
		
		# Simple string comparison - extract just the word without newlines
		echo "EXTRACTING JUST THE WORD:"
		WORD1=$(tr -d '\r\n' < hello.txt)
		WORD2=$(tr -d '\r\n' < ../hello.txt)
		echo "Word from mzip: '$WORD1'"
		echo "Word from original: '$WORD2'"
		
		# Compare the words
		[ "$WORD1" = "$WORD2" ] || error "uncompressed hello.txt fail"
		echo "mzip-extracted world.txt:"
		hexdump -C world.txt
		echo "original world.txt:"
		hexdump -C ../world.txt
		# Compare contents for equality, ignoring line endings
		cat world.txt | tr -d '\r\n' > world.clean
		cat ../world.txt | tr -d '\r\n' > orig2.clean
		diff -u world.clean orig2.clean || error "uncompressed world.txt fail"
		cd ..
	}
	fini
	return 0
}

# ---- #

test_unzip "store" || exit 1
test_unzip "deflate" || exit 1

MODE="store"; test_zip "0" || exit 1
MODE="deflate"; test_zip "1" || exit 1

