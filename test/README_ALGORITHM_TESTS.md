# Algorithm-Specific Test Suites

This directory contains individual test scripts for validating otezip's compression algorithm implementations against system binaries and reference implementations.

## Test Scripts

### test-deflate.sh
Tests the DEFLATE compression algorithm implementation against gzip (system deflate reference).

**Tests:**
- `test_otezip_deflate_extraction()` - Round-trip compression/decompression with otezip
- `test_otezip_deflate_system_unzip()` - Compatibility with system unzip
- `test_compression_ratio()` - Compression ratio comparison with gzip
- `test_corner_cases()` - Empty files, single bytes, etc.
- `test_method_field()` - Verify ZIP method field (0x0008)

**Run:** `bash test/test-deflate.sh`

### test-zstd.sh
Tests the ZSTD compression algorithm implementation against the system zstd binary.

**Tests:**
- `test_otezip_zstd_extraction()` - Round-trip compression/decompression
- `test_compression_comparison()` - Ratio comparison with system zstd
- `test_interop_decompress()` - Interoperability checks
- `test_compression_levels()` - Compression level testing
- `test_corner_cases()` - Empty, single byte, 5MB random file
- `test_method_field()` - Verify ZIP method field (0x005D/93)

**Run:** `bash test/test-zstd.sh`

### test-lzma.sh
Tests the LZMA compression algorithm implementation against xz (LZMA reference).

**Tests:**
- `test_otezip_lzma_extraction()` - Round-trip compression/decompression
- `test_7z_lzma_extraction()` - Compatibility with 7z-created LZMA archives
- `test_compression_comparison()` - Ratio comparison with system xz
- `test_random_data()` - Incompressible data handling
- `test_corner_cases()` - Empty, single byte, 1MB random file
- `test_mixed_content()` - Multiple file types in one archive
- `test_method_field()` - Verify ZIP method field (0x000E/14)

**Run:** `bash test/test-lzma.sh`

## Method IDs

- STORE: 0
- DEFLATE: 8
- LZMA: 14
- ZSTD: 93
- Brotli: 97
- LZFSE: 100

## System Requirements

- gzip (for DEFLATE comparison)
- zstd (for ZSTD comparison)
- xz (for LZMA/xz comparison)
- 7z (optional, for LZMA 7z compatibility tests)
- unzip (for standard ZIP extraction compatibility)

Install on macOS:
```
brew install zstd xz
# gzip comes with system
```

## Test Coverage

These tests ensure that otezip's compression implementations:

1. **Correctly compress and decompress files** - Round-trip integrity tests
2. **Produce valid ZIP archives** - Compatible with system unzip
3. **Match compression ratios** - Comparable to reference implementations
4. **Handle edge cases** - Empty files, single bytes, various sizes
5. **Maintain format compatibility** - Proper ZIP method field encoding
6. **Interoperate with other tools** - Extract files created by 7z, etc.

## Known Issues

- Some large repetitive files (>100KB) with deflate have extraction issues
- Very large LZMA files (>10MB) with 7z have compatibility issues
- Compression ratio calculations in scripts may have minor parsing issues on macOS

## Integration with Main Test Suite

The main test suite (`test.sh`) tests all algorithms together. These individual scripts provide deeper, algorithm-specific validation and debugging capabilities.
