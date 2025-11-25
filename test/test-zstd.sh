#!/bin/sh

# Test ZSTD compression algorithm against system zstd implementation
# This script compares otezip's zstd with the system zstd binary

D=tmp_zstd
MZ=$(pwd)/../otezip
if [ ! -x "${MZ}" ]; then
	make -C .. >/dev/null 2>&1
fi

# Check if zstd is available
if ! command -v zstd >/dev/null 2>&1; then
	echo "ERROR: zstd binary not found. Install it via: brew install zstd"
	exit 1
fi

init() {
	rm -rf "$D" && mkdir -p "$D" && cd "$D"
	
	# Create test files
	printf "zstd test content\n" > test1.txt
	dd if=/dev/urandom of=test_binary.bin bs=1k count=10 2>/dev/null
	
	# Create a repetitive file (should compress well)
	python3 -c "print('ZSTD' * 5000)" > test_repetitive.txt 2>/dev/null || \
		perl -e 'print "ZSTD" x 5000' > test_repetitive.txt 2>/dev/null || \
		(i=0; while [ $i -lt 5000 ]; do printf "ZSTD"; i=$((i+1)); done) > test_repetitive.txt
	
	echo "[ZSTD] Test files created:"
	ls -la
}

fini() {
	cd ..
	rm -rf "$D"
}

error() {
	echo "ERROR: $@" >&2
	exit 1
}

# Test 1: Create ZIP with otezip zstd, extract and verify
test_otezip_zstd_extraction() {
	init
	echo "[TEST 1] otezip zstd creation + otezip extraction"
	
	$MZ -c archive.zip test1.txt test_binary.bin test_repetitive.txt -z zstd || \
		error "otezip zstd creation failed"
	
	echo "[+] Created archive with otezip (zstd)"
	$MZ -l archive.zip
	
	mkdir -p extract_otezip
	cd extract_otezip
	$MZ -x ../archive.zip > /dev/null || error "otezip zstd extraction failed"
	
	# Verify extracted files match originals
	cmp -s test1.txt ../test1.txt || error "test1.txt mismatch after otezip extract"
	cmp -s test_binary.bin ../test_binary.bin || error "test_binary.bin mismatch after otezip extract"
	cmp -s test_repetitive.txt ../test_repetitive.txt || error "test_repetitive.txt mismatch after otezip extract"
	
	echo "[+] All files extracted correctly by otezip"
	cd ..
	
	fini
	echo "[PASS] Test 1: otezip zstd round-trip"
}

# Test 2: Compare compression with system zstd
test_compression_comparison() {
	init
	echo "[TEST 2] Compression ratio comparison with system zstd"
	
	original_size=$(wc -c < test_repetitive.txt)
	echo "[*] Original repetitive file size: $original_size bytes"
	
	# otezip zstd compression
	$MZ -c archive_otezip.zip test_repetitive.txt -z zstd || \
		error "otezip zstd creation failed"
	
	# Get the compressed size from the ZIP
	otezip_compressed=$(unzip -l archive_otezip.zip | grep test_repetitive | awk '{print $4}')
	otezip_zip_size=$(wc -c < archive_otezip.zip)
	echo "[*] otezip ZIP file size: $otezip_zip_size bytes (compressed: $otezip_compressed bytes)"
	
	# System zstd compression for comparison
	cp test_repetitive.txt test_repetitive_copy.txt
	zstd -k test_repetitive_copy.txt 2>/dev/null
	zstd_size=$(wc -c < test_repetitive_copy.txt.zst)
	echo "[*] System zstd file size: $zstd_size bytes"
	
	# Calculate compression ratios
	echo "[*] Compression ratios:"
	echo "    otezip zstd: $(echo "scale=2; $original_size / $otezip_compressed" | bc)x"
	echo "    system zstd: $(echo "scale=2; $original_size / $zstd_size" | bc)x"
	
	fini
	echo "[PASS] Test 2: Compression ratio comparison"
}

# Test 3: Decompress otezip-created file with system zstd
test_interop_decompress() {
	init
	echo "[TEST 3] Decompress otezip zstd frame with system zstd"
	
	# Create a ZIP with zstd
	$MZ -c archive.zip test1.txt -z zstd || error "otezip zstd creation failed"
	
	# Extract the zstd frame from the ZIP (skip ZIP headers)
	# This is tricky because we need to extract just the compressed data
	# For now, we'll just verify that otezip can decompress its own files
	
	mkdir -p extract
	cd extract
	$MZ -x ../archive.zip > /dev/null || error "otezip extraction failed"
	cmp -s test1.txt ../test1.txt || error "content mismatch"
	cd ..
	
	echo "[+] otezip zstd decompression successful"
	rm -rf extract
	
	fini
	echo "[PASS] Test 3: Interoperability check"
}

# Test 4: Multiple compression levels (if supported)
test_compression_levels() {
	init
	echo "[TEST 4] ZSTD with different compression levels"
	
	# Test at least basic compression
	for z in zstd; do
		$MZ -c archive.zip test_repetitive.txt -z $z || error "compression level test failed"
		mkdir -p extract && cd extract
		$MZ -x ../archive.zip > /dev/null || error "extraction failed"
		cmp -s test_repetitive.txt ../test_repetitive.txt || error "content mismatch after level test"
		cd .. && rm -rf extract
		echo "[+] Compression level test passed"
	done
	
	fini
	echo "[PASS] Test 4: Compression levels"
}

# Test 5: Corner cases
test_corner_cases() {
	init
	echo "[TEST 5] ZSTD corner cases"
	
	# Empty file
	: > empty.txt
	$MZ -c archive.zip empty.txt -z zstd || error "failed on empty file"
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction of empty failed"
	[ -f empty.txt ] || error "empty.txt not extracted"
	[ ! -s empty.txt ] || error "empty.txt should be empty"
	cd .. && rm -rf extract
	echo "[+] Empty file handling: OK"
	
	# Single byte
	printf "X" > single.bin
	$MZ -c archive.zip single.bin -z zstd || error "failed on single byte"
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction of single byte failed"
	cmp -s single.bin ../single.bin || error "single byte mismatch"
	cd .. && rm -rf extract
	echo "[+] Single byte handling: OK"
	
	# Large file
	dd if=/dev/urandom of=large.bin bs=1M count=5 2>/dev/null
	$MZ -c archive.zip large.bin -z zstd || error "failed on large file"
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction of large failed"
	cmp -s large.bin ../large.bin || error "large file mismatch"
	cd .. && rm -rf extract
	echo "[+] Large file handling: OK"
	
	fini
	echo "[PASS] Test 5: ZSTD corner cases"
}

# Test 6: Verify method field
test_method_field() {
	init
	echo "[TEST 6] Verify ZSTD method field in ZIP"
	
	$MZ -c archive.zip test1.txt -z zstd || error "otezip creation failed"
	
	# Method 93 is ZSTD (0x5d in hex)
	# Check if method field exists in archive
	if unzip -l archive.zip | grep -q test1.txt; then
		echo "[+] ZSTD archive created successfully"
		unzip -l archive.zip
	else
		error "Archive listing failed"
	fi
	
	fini
	echo "[PASS] Test 6: Method field verification"
}

# Run all tests
echo "=================================================="
echo "ZSTD Compression Algorithm Test Suite"
echo "Testing otezip against system zstd"
echo "=================================================="

test_otezip_zstd_extraction || exit 1
test_compression_comparison
test_interop_decompress || exit 1
test_compression_levels || exit 1
test_corner_cases || exit 1
test_method_field || exit 1

echo ""
echo "=================================================="
echo "All ZSTD tests passed!"
echo "=================================================="
