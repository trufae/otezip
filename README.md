# Minimalistic ZIP Implementation (mzip)

This is a minimalistic implementation of the ZIP file format, designed to be small, portable, and dependency-free.

## Features

- Header-only library with minimal dependencies
- Full read/write support for ZIP archives
- Zero dependencies (includes a tiny deflate implementation)
- Simple API compatible with a subset of libzip
- Suitable for embedded systems and small applications

## Components

The implementation consists of two main parts:

1. `mzip.c` - Minimalistic libzip subset replacement
2. `deflate.c` - Minimalistic deflate (RFC 1951) implementation compatible with zlib API

## Building

To build the library:

```bash
gcc -std=c99 -c -DMDEFLATE_IMPLEMENTATION deflate.c 
gcc -std=c99 -c -DMZIP_IMPLEMENTATION main.c 
gcc -o mzip main.o deflate.o
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
zip_file_add(za_write, "filename.txt", src, 0);
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

- Only supports compression methods 0 (store) and 8 (deflate)
- No support for encryption, ZIP64, or data descriptors
- Current implementation has limited support for extracting multiple files

## License

MIT / 0-BSD – do whatever you want; attribution appreciated.

## Compression

  Here are the most common compression algorithms used by Apple and Google for mobile app packaging:

  1. DEFLATE: Currently supported in mzip, used in both APK and IPA files
  2. Zstandard (zstd): Increasingly adopted by Google (Android 12+)
    - Better balance of compression ratio and speed
    - Significantly faster decompression than DEFLATE
    - Scalable compression levels
  3. LZFSE: Apple's proprietary algorithm used in newer IPA files
    - Optimized for mobile hardware
    - Battery efficient
    - Good balance of compression and speed
  4. LZ4: Used for speed-critical components
    - Extremely fast compression/decompression
    - Lower compression ratio
    - Minimal CPU and memory requirements
  5. Brotli: Used by Google in newer Android versions
    - Better compression ratios than DEFLATE (20-26% better)
    - Good for static content in apps
  6. LZMA/LZMA2: Occasionally used for specific resources
    - Higher compression ratios
    - Slower decompression
