# Minimalistic ZIP Implementation (mzip)

This is a minimalistic implementation of the ZIP file format, designed to be small, portable, and dependency-free.

**NOTE** This code has been initially written by Claude Code

## Features

- Header-only library with minimal dependencies
- Full read/write support for ZIP archives
- Zero dependencies (includes a tiny deflate implementation)
- Simple API compatible with a subset of libzip
- Suitable for embedded systems and small applications

## Configuration

The library can be configured by editing `config.h`. You can enable or disable specific compression algorithms based on your needs:

```c
/* Enable/disable compression algorithms */
#define MZIP_ENABLE_STORE   1  /* Always include store method */
#define MZIP_ENABLE_DEFLATE 1  /* Include DEFLATE compression */
#define MZIP_ENABLE_ZSTD    1  /* Include ZSTD compression */
#define MZIP_ENABLE_LZFSE   1  /* LZFSE compression support */
/* #define MZIP_ENABLE_LZ4     1 */  /* LZ4 compression (commented out by default) */
#define MZIP_ENABLE_LZMA    1  /* LZMA compression support */
#define MZIP_ENABLE_BROTLI  1  /* Brotli compression support */
```

## Components

The implementation consists of the following main parts:

1. `mzip.c` - Minimalistic libzip subset replacement
2. `deflate.inc.c` - Minimalistic deflate (RFC 1951) implementation compatible with zlib API
3. `zstd.inc.c` - Minimalistic zstd implementation
4. `lzma.inc.c` - Lightweight LZMA compression implementation
5. `brotli.inc.c` - Brotli compression implementation
6. `lzfse.inc.c` - Apple's LZFSE compression implementation
7. `config.h` - Configuration options for enabling/disabling compression methods

## Building

To build the library:

```bash
# Configure which algorithms to include in config.h first
# Edit config.h to enable or disable specific compression methods

# Then compile
gcc -std=c99 -c -DMZIP_IMPLEMENTATION mzip.c 
gcc -o mzip mzip.o
```

## Usage

```c
#include "mzip.c"  // This includes deflate.c internally

// Open a zip file
int err = 0;
zip_t *za = zip_open("archive.zip", ZIP_RDONLY, &err);

// Read files
zip_uint64_t n = zip_get_num_files(za);
for (zip_uint64_t i = 0; i < n; ++i) {
    zip_file_t *zf = zip_fopen_index(za, i, 0);
    // Use zf->data and zf->size
    zip_fclose(zf);
}

// Add files
zip_t *za_write = zip_open("new.zip", ZIP_CREATE, &err);
void *buffer = malloc(size);
// Fill buffer with data
zip_source_t *src = zip_source_buffer(za_write, buffer, size, 1);
zip_int64_t index = zip_file_add(za_write, "filename.txt", src, 0);

// Set compression method (you can use any supported method)
zip_set_file_compression(za_write, index, MZIP_METHOD_ZSTD, 0);  // For zstd compression
// Other available methods:
// zip_set_file_compression(za_write, index, MZIP_METHOD_LZMA, 0);   // For LZMA compression
// zip_set_file_compression(za_write, index, MZIP_METHOD_BROTLI, 0); // For Brotli compression
// zip_set_file_compression(za_write, index, MZIP_METHOD_LZFSE, 0);  // For LZFSE compression
// zip_set_file_compression(za_write, index, MZIP_METHOD_STORE, 0);   // For no compression
zip_close(za_write);

// Close when done
zip_close(za);
```

## Tool Usage

The included `mzip` tool demonstrates the library's functionality:

```bash
# List contents
./mzip -l archive.zip

# Extract all files
./mzip -x archive.zip

# Create new archive
./mzip -c archive.zip file1 file2

# Add files to existing archive
./mzip -a archive.zip file3 file4
```

## Limitations

- No support for encryption, ZIP64, or data descriptors
- Current implementation has limited support for extracting multiple files
- Limited testing with third-party ZIP utilities when using non-standard compression methods

## License

MIT / 0-BSD – do whatever you want; attribution appreciated.

## Compression

  mzip now supports the following compression algorithms used by Apple and Google for mobile app packaging:

  1. **DEFLATE** (Method ID: 8): Used in both APK and IPA files
    - Default ZIP compression algorithm
    - Good compatibility with all ZIP tools
    - Standard in most ZIP implementations

  2. **Zstandard (zstd)** (Method ID: 93): Increasingly adopted by Google (Android 12+)
    - Better balance of compression ratio and speed
    - Significantly faster decompression than DEFLATE
    - Scalable compression levels

  3. **LZFSE** (Method ID: 100): Apple's proprietary algorithm used in newer IPA files
    - Optimized for mobile hardware
    - Battery efficient
    - Good balance of compression and speed

  4. **Brotli** (Method ID: 97): Used by Google in newer Android versions
    - Better compression ratios than DEFLATE (20-26% better)
    - Good for static content in apps

  5. **LZMA** (Method ID: 14): Occasionally used for specific resources
    - Higher compression ratios
    - Slower decompression
    
  6. **STORE** (Method ID: 0): No compression
    - Raw file storage without compression
    - Used as a fallback when compression doesn't reduce file size

  Support for **LZ4** (Method ID: 94) is included in the codebase but disabled by default. It can be enabled in config.h.
