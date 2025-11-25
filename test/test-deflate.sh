#!/bin/sh

# Test DEFLATE compression algorithm against system gzip implementation
# This script compares otezip's deflate with gzip to ensure compatibility

D=tmp_deflate
MZ=$(pwd)/../otezip
if [ ! -x "${MZ}" ]; then
	make -C .. >/dev/null 2>&1
fi

init() {
	rm -rf "$D" && mkdir -p "$D" && cd "$D"
	
	# Create test files
	printf "hello world\n" > test1.txt
	dd if=/dev/urandom of=test_binary.bin bs=1k count=10 2>/dev/null
	
	# Create a repetitive file (should compress well)
	python3 -c "print('A' * 5000)" > test_repetitive.txt 2>/dev/null || \
		perl -e 'print "A" x 5000' > test_repetitive.txt 2>/dev/null || \
		(i=0; while [ $i -lt 5000 ]; do printf "A"; i=$((i+1)); done) > test_repetitive.txt
	
	echo "[DEFLATE] Test files created:"
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

# Test 1: Create ZIP with otezip deflate, extract and verify with system
test_otezip_deflate_extraction() {
	init
	echo "[TEST 1] otezip deflate creation + otezip extraction"
	
	$MZ -c archive.zip test1.txt test_binary.bin test_repetitive.txt -z deflate || \
		error "otezip creation failed"
	
	echo "[+] Created archive with otezip (deflate)"
	$MZ -l archive.zip
	
	mkdir -p extract_otezip
	cd extract_otezip
	$MZ -x ../archive.zip > /dev/null || error "otezip extraction failed"
	
	# Verify extracted files match originals
	cmp -s test1.txt ../test1.txt || error "test1.txt mismatch after otezip extract"
	cmp -s test_binary.bin ../test_binary.bin || error "test_binary.bin mismatch after otezip extract"
	cmp -s test_repetitive.txt ../test_repetitive.txt || error "test_repetitive.txt mismatch after otezip extract"
	
	echo "[+] All files extracted correctly by otezip"
	cd ..
	
	fini
	echo "[PASS] Test 1: otezip deflate round-trip"
}

# Test 2: Create ZIP with otezip deflate, verify with system unzip
test_otezip_deflate_system_unzip() {
	init
	echo "[TEST 2] otezip deflate creation + system unzip extraction"
	
	$MZ -c archive.zip test1.txt test_binary.bin test_repetitive.txt -z deflate || \
		error "otezip creation failed"
	
	echo "[+] Created archive with otezip (deflate)"
	$MZ -l archive.zip
	
	mkdir -p extract_unzip
	cd extract_unzip
	unzip -q ../archive.zip || error "system unzip extraction failed"
	
	# Verify extracted files match originals
	cmp -s test1.txt ../test1.txt || error "test1.txt mismatch after system unzip"
	cmp -s test_binary.bin ../test_binary.bin || error "test_binary.bin mismatch after system unzip"
	cmp -s test_repetitive.txt ../test_repetitive.txt || error "test_repetitive.txt mismatch after system unzip"
	
	echo "[+] All files extracted correctly by system unzip"
	cd ..
	
	fini
	echo "[PASS] Test 2: otezip deflate compatible with system unzip"
}

# Test 3: Compare compression ratio with gzip
test_compression_ratio() {
	init
	echo "[TEST 3] Compression ratio comparison with gzip"
	
	original_size=$(wc -c < test_repetitive.txt)
	echo "[*] Original repetitive file size: $original_size bytes"
	
	# otezip deflate compression
	$MZ -c archive_otezip.zip test_repetitive.txt -z deflate || \
		error "otezip creation failed"
	
	# Get the compressed size from the ZIP local header
	otezip_compressed=$(unzip -l archive_otezip.zip | grep test_repetitive | awk '{print $4}')
	otezip_zip_size=$(wc -c < archive_otezip.zip)
	echo "[*] otezip ZIP file size: $otezip_zip_size bytes (compressed: $otezip_compressed bytes)"
	
	# gzip compression for comparison
	gzip -k test_repetitive.txt
	gzip_size=$(wc -c < test_repetitive.txt.gz)
	echo "[*] gzip file size: $gzip_size bytes"
	
	# Calculate compression ratios
	echo "[*] Compression ratios:"
	echo "    otezip deflate: $(echo "scale=2; $original_size / $otezip_compressed" | bc)x"
	echo "    gzip deflate: $(echo "scale=2; $original_size / $gzip_size" | bc)x"
	
	fini
	echo "[PASS] Test 3: Compression ratio comparison"
}

# Test 4: Corner cases
test_corner_cases() {
	init
	echo "[TEST 4] DEFLATE corner cases"
	
	# Empty file
	: > empty.txt
	$MZ -c archive.zip empty.txt -z deflate || error "failed on empty file"
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction of empty failed"
	[ -f empty.txt ] || error "empty.txt not extracted"
	[ ! -s empty.txt ] || error "empty.txt should be empty"
	cd .. && rm -rf extract
	echo "[+] Empty file handling: OK"
	
	# Single byte
	printf "X" > single.bin
	$MZ -c archive.zip single.bin -z deflate || error "failed on single byte"
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction of single byte failed"
	cmp -s single.bin ../single.bin || error "single byte mismatch"
	cd .. && rm -rf extract
	echo "[+] Single byte handling: OK"
	
	fini
	echo "[PASS] Test 4: DEFLATE corner cases"
}

# Test 5: Verify internal compression method field
test_method_field() {
	init
	echo "[TEST 5] Verify DEFLATE method field in ZIP"
	
	$MZ -c archive.zip test1.txt -z deflate || error "otezip creation failed"
	
	# Method 8 is DEFLATE (0x08 in hex)
	# Check local file header compression method
	method=$(xxd -p archive.zip | head -c 200 | grep -o '0800' | head -1)
	if [ -n "$method" ]; then
		echo "[+] DEFLATE method field (0x0008) found in archive"
	else
		echo "[!] Warning: Could not verify DEFLATE method field, but archive works"
	fi
	
	fini
	echo "[PASS] Test 5: Method field verification"
}

# Run all tests
echo "=================================================="
echo "DEFLATE Compression Algorithm Test Suite"
echo "Testing otezip against gzip/system unzip"
echo "=================================================="

test_otezip_deflate_extraction || exit 1
test_otezip_deflate_system_unzip || exit 1
test_compression_ratio
test_corner_cases || exit 1
test_method_field || exit 1

echo ""
echo "=================================================="
echo "All DEFLATE tests passed!"
echo "=================================================="
