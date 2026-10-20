#!/bin/sh

D=tmp
MZ=$(pwd)/../build/otezip
if [ ! -x "${MZ}" ]; then
MZ=$(pwd)/../otezip
if [ ! -x "${MZ}" ]; then
	make -C ..
fi
fi


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
	echo "[***] Testing zip+otezip with $1"
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
     echo "[***] Testing otezip $1"
    echo "Creating test.zip with otezip -c test.zip hello.txt world.txt -z $1"
    # Use the compression method specified by the parameter
    $MZ -c test.zip hello.txt world.txt -z $1
    echo "Archive contents:"
    xxd -g 1 test.zip | head -n 20
    unzip -l test.zip > files.txt
    grep hello.txt files.txt > /dev/null || error "hello.txt not found"
    grep world.txt files.txt > /dev/null || error "world.txt not found"
    # unzip does not support extracting some non-standard methods (e.g., brotli=97)
    if [ "$1" != "5" ]; then
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
    else
        echo "[---] Skipping unzip extraction for brotli (method 97)"
    fi
	echo "[---] Decompressing with otezip"
	{
		mkdir -p data
		cd data
		$MZ -x ../test.zip
		echo "otezip-extracted hello.txt:"
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
		echo "Word from otezip: '$WORD1'"
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

# Test LZMA using an existing LZMA-compressed zip
test_unzip_lzma() {
	init
	echo "[***] Testing otezip decompression with LZMA"
	# Create an LZMA-compressed zip file
	$MZ -c test.zip hello.txt world.txt -z lzma
	echo "Created zip file with LZMA method"
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

# Test LZMA with 7z
test_lzma_7z() {
	init
	echo "[***] Testing otezip LZMA decompression with 7z created archive"
	# Create an LZMA-compressed zip file with 7z
	7z a -tzip -mm=LZMA test.zip hello.txt world.txt
	echo "Created zip file with 7z LZMA method"
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

# ---- #

test_unzip "store" || exit 1
test_unzip "deflate" || exit 1
test_unzip_lzma || exit 1
test_lzma_7z || exit 1

MODE="store"; test_zip "store" || exit 1
MODE="deflate"; test_zip "deflate" || exit 1
MODE="lzma"; test_zip "lzma" || exit 1
MODE="brotli"; test_zip "brotli" || exit 1
MODE="zstd"; test_zip "zstd" || exit 1
MODE="lzfse"; test_zip "lzfse" || exit 1

# Additional corner-case tests

test_empty_files() {
     init
     echo "[***] Testing empty files with store/deflate/lzma/brotli/zstd/lzfse"
     : > empty.txt
     for Z in store deflate lzma brotli zstd lzfse; do
        rm -f test.zip
        $MZ -c test.zip empty.txt -z $Z || error "otezip failed for -z $Z"
        unzip -l test.zip > files.txt || error "unzip -l failed"
        grep "empty.txt" files.txt >/dev/null || error "empty.txt missing (-z $Z)"
        mkdir -p data && cd data
        $MZ -x ../test.zip >/dev/null || error "mzip -x failed (-z $Z)"
        [ -f empty.txt ] || error "empty.txt not extracted (-z $Z)"
        cmp -s empty.txt ../empty.txt || error "empty.txt mismatch (-z $Z)"
        cd .. && rm -rf data
    done
    fini
}

test_binary_file() {
     init
     echo "[***] Testing binary file (0..255) with store/deflate/lzma/brotli/zstd/lzfse"
    # create 256-byte binary with values 0..255
    i=0; : > bin.dat
    while [ $i -lt 256 ]; do printf "\\$(printf '%03o' $i)" >> bin.dat; i=$((i+1)); done
     for Z in store deflate lzma brotli zstd lzfse; do
        rm -f test.zip
        $MZ -c test.zip bin.dat -z $Z || error "otezip failed for -z $Z"
        unzip -l test.zip > files.txt || error "unzip -l failed"
        grep "bin.dat" files.txt >/dev/null || error "bin.dat missing (-z $Z)"
        mkdir -p data && cd data
        $MZ -x ../test.zip >/dev/null || error "mzip -x failed (-z $Z)"
        cmp -s bin.dat ../bin.dat || error "binary mismatch (-z $Z)"
        cd .. && rm -rf data
    done
    fini
}

test_large_random_and_fallback() {
    init
    echo "[***] Testing random data with LZMA (validate robust handling)"
    dd if=/dev/urandom of=rand.bin bs=1k count=4 2>/dev/null || error "cannot create random"
    $MZ -c test.zip rand.bin -z lzma || error "otezip failed -z lzma"
    # Validate archive and extraction
    unzip -l test.zip > files.txt || error "unzip -l failed"
    grep "rand.bin" files.txt >/dev/null || error "rand.bin missing"
    mkdir -p data && cd data
    $MZ -x ../test.zip >/dev/null || error "mzip -x failed"
    cmp -s rand.bin ../rand.bin || error "random mismatch after extract"
    cd .. && rm -rf data
    fini
}

test_duplicate_names_listing() {
    init
    echo "[***] Testing duplicate filenames in archive listing"
    mkdir -p a b
    echo one > a/dup.txt
    echo two > b/dup.txt
    # Add both; tool stores base names, creating two entries with same name
    $MZ -c test.zip a/dup.txt b/dup.txt -z deflate || error "otezip failed"
    out=$($MZ -l test.zip)
    echo "$out"
    cnt=$(printf "%s" "$out" | grep -c "dup.txt")
    [ "$cnt" -eq 2 ] || error "expected 2 dup.txt entries, got $cnt"
    fini
}

test_space_in_name() {
     init
     echo "[***] Testing filename with spaces"
     printf "spaced content\n" > "space name.txt"
      for Z in store deflate lzma brotli zstd lzfse; do
         rm -f test.zip
         $MZ -c test.zip "space name.txt" -z $Z || error "otezip failed for -z $Z"
         $MZ -l test.zip | grep "space name.txt" >/dev/null || error "missing spaced name (-z $Z)"
         mkdir -p data && cd data
         $MZ -x ../test.zip >/dev/null || error "mzip -x failed (-z $Z)"
         diff -u "space name.txt" ../"space name.txt" || error "spaced filename mismatch (-z $Z)"
         cd .. && rm -rf data
     done
     fini
 }

test_large_file() {
     init
     echo "[***] Testing large file (1MB) with store/deflate/lzma/brotli/zstd/lzfse"
     # Create a 1MB random file
     dd if=/dev/urandom of=large.bin bs=1k count=1024 2>/dev/null || error "cannot create large file"
     for Z in store deflate lzma brotli zstd lzfse; do
         rm -f test.zip
         $MZ -c test.zip large.bin -z $Z || error "otezip failed for -z $Z"
         unzip -l test.zip > files.txt || error "unzip -l failed"
         grep "large.bin" files.txt >/dev/null || error "large.bin missing (-z $Z)"
         mkdir -p data && cd data
         $MZ -x ../test.zip >/dev/null || error "mzip -x failed (-z $Z)"
         cmp -s large.bin ../large.bin || error "large.bin mismatch (-z $Z)"
         cd .. && rm -rf data
     done
     fini
 }

test_deflate_unzip_compat() {
	init
	echo "[***] Testing otezip deflate output is valid for unzip"
	printf "hello hello hello world\n" > comp.txt
	$MZ -c test.zip comp.txt -z deflate || error "otezip failed for -z deflate"
	unzip -v test.zip | grep "Defl" >/dev/null || error "comp.txt was not deflated"
	unzip -t test.zip || error "unzip rejects otezip deflate data"
	unzip -p test.zip comp.txt | cmp -s - comp.txt || error "unzip output mismatch"
	# Larger than the 32 KiB window, so matches must handle the wrap
	seq 1 30000 > big.txt
	$MZ -c big.zip big.txt -z deflate || error "otezip failed for big.txt"
	unzip -v big.zip | grep "Defl" >/dev/null || error "big.txt was not deflated"
	unzip -t big.zip || error "unzip rejects otezip deflate data over 32 KiB"
	unzip -p big.zip big.txt | cmp -s - big.txt || error "unzip output mismatch over 32 KiB"
	# otezip must read its own output back, whatever bit the stream ends on
	# (these sizes end with symbols decoded from already buffered bits)
	for N in 417 2489 3377; do
		yes abc | head -c $N > rep$N.txt
		$MZ -c rep.zip rep$N.txt -z deflate >/dev/null || error "otezip failed for rep$N.txt"
		mkdir data && cd data
		$MZ -x ../rep.zip >/dev/null || error "otezip -x failed for rep$N.txt"
		cmp -s rep$N.txt ../rep$N.txt || error "rep$N.txt mismatch"
		cd .. && rm -rf data rep.zip
	done
	fini
}

test_nested_paths() {
	init
	echo "[***] Testing extraction of nested directories"
	mkdir -p dir/sub
	printf "deep\n" > dir/sub/f.txt
	zip -r test.zip dir || error "zip failed"
	mkdir data && cd data
	$MZ -x ../test.zip || error "otezip -x failed"
	[ -f dir/sub/f.txt ] || error "dir/sub/f.txt not extracted at the right path"
	cmp -s dir/sub/f.txt ../dir/sub/f.txt || error "dir/sub/f.txt mismatch"
	[ -z "$(find . -name 'f.txt' ! -path './dir/sub/f.txt')" ] || error "mangled paths created"
	cd ..
	fini
}

test_exit_codes() {
	init
	echo "[***] Testing exit codes on failures"
	$MZ -c test.zip hello.txt -z store || error "otezip -c failed"
	if $MZ -c bad.zip hello.txt missing.txt; then
		error "missing input file did not fail"
	fi
	mkdir data && cd data
	$MZ -x ../test.zip || error "clean extraction failed"
	if $MZ -x ../test.zip; then
		error "skipping an existing file did not fail"
	fi
	$MZ -x ../test.zip -f || error "forced extraction failed"
	cd ..
	# Corrupt the stored data (after the 30-byte header and 9-byte name)
	printf 'X' | dd of=test.zip bs=1 seek=39 conv=notrunc 2>/dev/null
	mkdir crc && cd crc
	if $MZ -x ../test.zip --verify-crc; then
		error "CRC mismatch did not fail with --verify-crc"
	fi
	cd ..
	fini
}

test_write_error_cleanup() {
	init
	echo "[***] Testing that a failed write leaves no partial file"
	dd if=/dev/urandom of=big.bin bs=1k count=100 2>/dev/null
	$MZ -c test.zip big.bin -z store >/dev/null || error "otezip -c failed"
	mkdir data && cd data
	# Cap file size at 5 KiB so write() fails with EFBIG instead of a signal
	if (trap '' XFSZ; ulimit -f 10; $MZ -x ../test.zip); then
		error "failed write did not fail"
	fi
	[ ! -e big.bin ] || error "partial big.bin left behind"
	cd ..
	fini
}

test_fifo_target() {
	init
	echo "[***] Testing that -f does not block on a FIFO in place of a file"
	$MZ -c test.zip hello.txt -z store >/dev/null || error "otezip -c failed"
	mkdir data && mkfifo data/hello.txt
	# A blocking open would hang; the alarm kills it after 10 seconds
	(cd data && perl -e 'alarm 10; exec @ARGV' $MZ -x ../test.zip -f)
	[ $? -eq 1 ] || error "extracting over a FIFO did not fail cleanly"
	[ -p data/hello.txt ] || error "FIFO was replaced"
	fini
}

test_strip_policy_symlinks() {
	init
	echo "[***] Testing that -Pstrip does not write through symlinks"
	mkdir -p src/link && printf "pwned\n" > src/link/pwn.txt && printf "new\n" > src/victim
	(cd src && zip -q ../test.zip link/pwn.txt victim) || error "zip failed"
	mkdir -p data outside && printf "original\n" > outside/victim
	ln -s ../outside data/link && ln -s ../outside/victim data/victim
	(cd data && $MZ -x ../test.zip -Pstrip -f)
	[ ! -e outside/pwn.txt ] || error "-Pstrip wrote through a symlinked directory"
	[ "$(cat outside/victim)" = "original" ] || error "-Pstrip -f overwrote a symlink target"
	fini
}

test_cli_option_order() {
	init
	echo "[***] Testing options before, between and after file names"
	for ARGS in "-z store hello.txt world.txt" "hello.txt -z store world.txt" "-zstore hello.txt world.txt"; do
		rm -f test.zip
		$MZ -c test.zip $ARGS || error "otezip -c $ARGS failed"
		[ "$(unzip -Z1 test.zip | tr '\n' ' ')" = "hello.txt world.txt " ] || error "wrong entries for: $ARGS"
		[ "$(unzip -v test.zip | grep -c Stored)" = 2 ] || error "-z store ignored for: $ARGS"
	done
	if $MZ -c bad.zip hello.txt --bogus 2>/dev/null; then
		error "unknown option accepted"
	fi
	[ ! -f bad.zip ] || error "archive created despite unknown option"
	fini
}

test_append() {
	init
	echo "[***] Testing appending entries to existing archives"
	printf "third\n" > third.txt
	$MZ -c test.zip hello.txt -z store || error "otezip -c failed"
	$MZ -a test.zip world.txt -z store || error "otezip -a failed"
	$MZ -a test.zip third.txt -z store || error "second otezip -a failed"
	unzip -t test.zip || error "unzip rejects appended archive"
	# Archive written by zip with a comment after the EOCD record
	zip -q info.zip hello.txt || error "zip failed"
	echo "archive comment" | zip -qz info.zip || error "zip -z failed"
	$MZ -a info.zip world.txt -z store || error "otezip -a on zip archive failed"
	unzip -t info.zip || error "unzip rejects appended zip archive"
	for Z in test.zip info.zip; do
		mkdir data && cd data
		$MZ -x ../$Z || error "otezip -x $Z failed"
		cmp -s hello.txt ../hello.txt || error "hello.txt mismatch in $Z"
		cmp -s world.txt ../world.txt || error "world.txt mismatch in $Z"
		cd .. && rm -rf data
	done
	mkdir data && cd data
	$MZ -x ../test.zip >/dev/null
	cmp -s third.txt ../third.txt || error "third.txt mismatch"
	cd ..
	fini
}

# Run new tests
test_deflate_unzip_compat || exit 1
test_nested_paths || exit 1
test_append || exit 1
test_cli_option_order || exit 1
test_strip_policy_symlinks || exit 1
test_exit_codes || exit 1
test_write_error_cleanup || exit 1
test_fifo_target || exit 1
test_empty_files || exit 1
test_binary_file || exit 1
test_large_random_and_fallback || exit 1
test_duplicate_names_listing || exit 1
test_space_in_name || exit 1
test_large_file || exit 1

# Memory leak tests with Valgrind
check_valgrind() {
    command -v valgrind >/dev/null 2>&1 || { echo "Valgrind not found, skipping memory leak tests"; return 1; }
    return 0
}

if check_valgrind; then
    HAS_VALGRIND=1
else
    HAS_VALGRIND=0
fi

test_valgrind_extract() {
    if [ "$HAS_VALGRIND" -eq 0 ]; then return 0; fi
    init
    echo "[***] Testing extractions under Valgrind"
    $MZ -c test.zip hello.txt world.txt -z deflate
    mkdir data
    cd data
    valgrind --tool=memcheck --leak-check=full --error-exitcode=1 --quiet $MZ -x ../test.zip || error "Valgrind detected leaks in extraction"
    cd ..
    fini
}

test_valgrind_corrupted() {
    if [ "$HAS_VALGRIND" -eq 0 ]; then return 0; fi
    init
    echo "[***] Testing corrupted archive under Valgrind"
    $MZ -c test.zip hello.txt world.txt -z deflate
    dd if=test.zip of=corrupted.zip bs=1 count=100 2>/dev/null
    mkdir data
    cd data
    valgrind --tool=memcheck --leak-check=full --error-exitcode=1 --quiet $MZ -x ../corrupted.zip 2>/dev/null || true
    cd ..
    fini
}

test_valgrind_cycle() {
    if [ "$HAS_VALGRIND" -eq 0 ]; then return 0; fi
    init
    echo "[***] Testing compression/decompression cycle under Valgrind"
    valgrind --tool=memcheck --leak-check=full --error-exitcode=1 --quiet $MZ -c test.zip hello.txt world.txt -z deflate || error "Valgrind detected leaks in compression"
    mkdir data
    cd data
    valgrind --tool=memcheck --leak-check=full --error-exitcode=1 --quiet $MZ -x ../test.zip || error "Valgrind detected leaks in extraction"
    cd ..
    fini
}

test_valgrind_extract || exit 1
test_valgrind_corrupted || exit 1
test_valgrind_cycle || exit 1
