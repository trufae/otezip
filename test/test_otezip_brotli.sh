#!/bin/bash
# test_otezip_brotli.sh - Test Brotli compression/decompression in otezip
#
# This script tests the Brotli compression and decompression functionality
# in otezip by:
# 1. Creating test files
# 2. Adding them to a ZIP with Brotli compression
# 3. Extracting the files from the ZIP
# 4. Comparing the extracted files to the originals
#
# License: MIT

set -e  # Exit on error

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Create test directory
TEST_DIR="$(pwd)/test_brotli_tmp"
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

echo "=== Testing otezip with Brotli compression ==="
echo

# Create test files
echo "Creating test files..."
echo "Hello, world!" > test1.txt
echo "The quick brown fox jumps over the lazy dog." > test2.txt
dd if=/dev/urandom of=test3.bin bs=1k count=10 2>/dev/null

# Binary with repeated pattern (highly compressible)
perl -e 'print "A" x 10000' > test4.bin

# Build otezip with Brotli support if not already built
if [ ! -x "../otezip" ]; then
    echo "Building otezip with Brotli support..."
    cd ..
    make clean
    make OTEZIP_ENABLE_BROTLI=1
    cd "$TEST_DIR"
fi

# Path to otezip executable
OTEZIP="../otezip"

# Create ZIP file with Brotli compression
echo "Creating ZIP file with Brotli compression..."
$OTEZIP -c brotli test.zip test1.txt test2.txt test3.bin test4.bin

# Extract the files to a different directory
echo "Extracting files from ZIP..."
mkdir -p extract
cd extract
$OTEZIP -x ../test.zip

# Compare the extracted files with the originals
echo "Comparing extracted files with originals..."
errors=0

for file in test1.txt test2.txt test3.bin test4.bin; do
    if diff -q "../$file" "$file" >/dev/null; then
        echo -e "${GREEN}✓${NC} $file matches original"
    else
        echo -e "${RED}✗${NC} $file differs from original!"
        errors=$((errors + 1))
    fi
done

# Print summary
echo
if [ $errors -eq 0 ]; then
    echo -e "${GREEN}All tests passed successfully!${NC}"
    exit_code=0
else
    echo -e "${RED}$errors test(s) failed!${NC}"
    exit_code=1
fi

# Clean up
cd ../../
rm -rf "$TEST_DIR"

exit $exit_code