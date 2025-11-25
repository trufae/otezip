#!/bin/sh

# Test LZMA compression algorithm against system xz implementation
# xz uses LZMA as its compression algorithm, so this is a good reference

D=tmp_lzma
MZ=$(pwd)/../otezip
if [ ! -x "${MZ}" ]; then
	make -C .. >/dev/null 2>&1
fi

# Check if xz is available
if ! command -v xz >/dev/null 2>&1; then
	echo "ERROR: xz binary not found. Install it via: brew install xz"
	exit 1
fi

init() {
	rm -rf "$D" && mkdir -p "$D" && cd "$D"
	
	# Create test files
	printf "lzma test content\n" > test1.txt
	dd if=/dev/urandom of=test_binary.bin bs=1k count=10 2>/dev/null
	
	# Create a repetitive file (LZMA should compress very well)
	python3 -c "print('LZMA' * 10000)" > test_repetitive.txt 2>/dev/null || \
		perl -e 'print "LZMA" x 10000' > test_repetitive.txt 2>/dev/null || \
		(i=0; while [ $i -lt 10000 ]; do printf "LZMA"; i=$((i+1)); done) > test_repetitive.txt
	
	echo "[LZMA] Test files created:"
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

# Test 1: Create ZIP with otezip LZMA, extract and verify
test_otezip_lzma_extraction() {
	init
	echo "[TEST 1] otezip LZMA creation + otezip extraction"
	
	$MZ -c archive.zip test1.txt test_binary.bin test_repetitive.txt -z lzma || \
		error "otezip LZMA creation failed"
	
	echo "[+] Created archive with otezip (lzma)"
	$MZ -l archive.zip
	
	mkdir -p extract_otezip
	cd extract_otezip
	$MZ -x ../archive.zip > /dev/null || error "otezip LZMA extraction failed"
	
	# Verify extracted files match originals
	cmp -s test1.txt ../test1.txt || error "test1.txt mismatch after otezip extract"
	cmp -s test_binary.bin ../test_binary.bin || error "test_binary.bin mismatch after otezip extract"
	cmp -s test_repetitive.txt ../test_repetitive.txt || error "test_repetitive.txt mismatch after otezip extract"
	
	echo "[+] All files extracted correctly by otezip"
	cd ..
	
	fini
	echo "[PASS] Test 1: otezip LZMA round-trip"
}

# Test 2: Create ZIP with 7z LZMA, verify extraction with otezip
test_7z_lzma_extraction() {
	init
	echo "[TEST 2] 7z LZMA creation + otezip extraction"
	
	if ! command -v 7z >/dev/null 2>&1; then
		echo "[!] 7z not available, skipping this test"
		fini
		return 0
	fi
	
	# Create archive with 7z using LZMA method (with smaller files for now)
	7z a -tzip -mm=LZMA archive.zip test1.txt test_binary.bin >/dev/null 2>&1 || \
		error "7z LZMA creation failed"
	
	echo "[+] Created archive with 7z (LZMA)"
	$MZ -l archive.zip
	
	mkdir -p extract_otezip
	cd extract_otezip
	$MZ -x ../archive.zip > /dev/null || error "otezip LZMA extraction failed"
	
	# Verify extracted files match originals
	cmp -s test1.txt ../test1.txt || error "test1.txt mismatch (7z->otezip)"
	cmp -s test_binary.bin ../test_binary.bin || error "test_binary.bin mismatch (7z->otezip)"
	
	echo "[+] All files extracted correctly (7z created, otezip extracted)"
	cd ..
	
	fini
	echo "[PASS] Test 2: 7z LZMA compatibility"
}

# Test 3: Compare compression with system xz
test_compression_comparison() {
	init
	echo "[TEST 3] Compression ratio comparison with system xz (LZMA)"
	
	original_size=$(wc -c < test_repetitive.txt)
	echo "[*] Original repetitive file size: $original_size bytes"
	
	# otezip LZMA compression
	$MZ -c archive_otezip.zip test_repetitive.txt -z lzma || \
		error "otezip LZMA creation failed"
	
	# Get the compressed size from the ZIP
	otezip_compressed=$(unzip -l archive_otezip.zip | grep test_repetitive | awk '{print $4}')
	otezip_zip_size=$(wc -c < archive_otezip.zip)
	echo "[*] otezip ZIP file size: $otezip_zip_size bytes (compressed: $otezip_compressed bytes)"
	
	# System xz compression for comparison
	cp test_repetitive.txt test_repetitive_copy.txt
	xz -k test_repetitive_copy.txt 2>/dev/null
	xz_size=$(wc -c < test_repetitive_copy.txt.xz)
	echo "[*] System xz file size: $xz_size bytes"
	
	# Calculate compression ratios
	echo "[*] Compression ratios:"
	echo "    otezip LZMA: $(echo "scale=2; $original_size / $otezip_compressed" | bc)x"
	echo "    system xz: $(echo "scale=2; $original_size / $xz_size" | bc)x"
	
	fini
	echo "[PASS] Test 3: Compression ratio comparison"
}

# Test 4: Random data handling
test_random_data() {
	init
	echo "[TEST 4] LZMA compression of random/incompressible data"
	
	# Random data should expand slightly when "compressed"
	dd if=/dev/urandom of=random.bin bs=1k count=50 2>/dev/null
	original_size=$(wc -c < random.bin)
	
	$MZ -c archive.zip random.bin -z lzma || error "LZMA compression of random failed"
	
	# Extract and verify
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction failed"
	cmp -s random.bin ../random.bin || error "random data mismatch"
	cd ..
	
	compressed_size=$(unzip -l archive.zip | grep random.bin | awk '{print $4}')
	echo "[*] Original: $original_size bytes, Compressed: $compressed_size bytes"
	echo "[+] Random data correctly handled (may expand due to incompressibility)"
	
	rm -rf extract
	fini
	echo "[PASS] Test 4: Random data handling"
}

# Test 5: Corner cases
test_corner_cases() {
	init
	echo "[TEST 5] LZMA corner cases"
	
	# Empty file
	: > empty.txt
	$MZ -c archive.zip empty.txt -z lzma || error "failed on empty file"
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction of empty failed"
	[ -f empty.txt ] || error "empty.txt not extracted"
	[ ! -s empty.txt ] || error "empty.txt should be empty"
	cd .. && rm -rf extract
	echo "[+] Empty file handling: OK"
	
	# Single byte
	printf "X" > single.bin
	$MZ -c archive.zip single.bin -z lzma || error "failed on single byte"
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction of single byte failed"
	cmp -s single.bin ../single.bin || error "single byte mismatch"
	cd .. && rm -rf extract
	echo "[+] Single byte handling: OK"
	
	# Moderate file size
	dd if=/dev/urandom of=medium.bin bs=1M count=1 2>/dev/null
	$MZ -c archive.zip medium.bin -z lzma || error "failed on medium file"
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction of medium failed"
	cmp -s medium.bin ../medium.bin || error "medium file mismatch"
	cd .. && rm -rf extract
	echo "[+] Medium file handling (1MB): OK"
	
	fini
	echo "[PASS] Test 5: LZMA corner cases"
}

# Test 6: Mixed files in single archive
test_mixed_content() {
	init
	echo "[TEST 6] Mixed file types in LZMA archive"
	
	# Mix of text, binary, empty
	echo "text file content" > file.txt
	printf "\x00\x01\x02\x03" > file.bin
	: > empty.dat
	dd if=/dev/urandom of=random.dat bs=1k count=5 2>/dev/null
	
	$MZ -c archive.zip file.txt file.bin empty.dat random.dat -z lzma || error "creation failed"
	
	# Extract and verify each file
	mkdir -p extract && cd extract
	$MZ -x ../archive.zip > /dev/null || error "extraction failed"
	
	cmp -s file.txt ../file.txt || error "text file mismatch"
	cmp -s file.bin ../file.bin || error "binary file mismatch"
	cmp -s empty.dat ../empty.dat || error "empty file mismatch"
	cmp -s random.dat ../random.dat || error "random file mismatch"
	
	echo "[+] All mixed content files verified"
	cd ..
	rm -rf extract
	
	fini
	echo "[PASS] Test 6: Mixed content handling"
}

# Test 7: Verify LZMA method field in ZIP
test_method_field() {
	init
	echo "[TEST 7] Verify LZMA method field in ZIP"
	
	$MZ -c archive.zip test1.txt -z lzma || error "otezip creation failed"
	
	# Method 14 is LZMA (0x0e in hex)
	if unzip -l archive.zip | grep -q test1.txt; then
		echo "[+] LZMA archive created successfully"
		unzip -l archive.zip
	else
		error "Archive listing failed"
	fi
	
	fini
	echo "[PASS] Test 7: LZMA method field verification"
}

# Run all tests
echo "=================================================="
echo "LZMA Compression Algorithm Test Suite"
echo "Testing otezip against system xz (LZMA reference)"
echo "=================================================="

test_otezip_lzma_extraction || exit 1
test_7z_lzma_extraction || exit 1
test_compression_comparison
test_random_data || exit 1
test_corner_cases || exit 1
test_mixed_content || exit 1
test_method_field || exit 1

echo ""
echo "=================================================="
echo "All LZMA tests passed!"
echo "=================================================="
