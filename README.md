# OpTimizEd ZIP libzip compatible (otezip)

[![CI](https://github.com/trufae/otezip/actions/workflows/ci.yml/badge.svg)](https://github.com/trufae/otezip/actions/workflows/ci.yml)

A minimalistic, dependency-free ZIP implementation with support for multiple compression algorithms.

## Features

- Small, portable library with zero dependencies
- Full ZIP read/write support
- Multiple compression algorithms (DEFLATE, ZSTD, LZMA, Brotli, LZFSE)
- Simple API compatible with libzip subset
- Suitable for embedded systems

## Building

```bash
# Build default binary
make

# Build with all compression algorithms
make all-compression

# Install
make install

# Run tests
make -C test
```

## Usage

### Library API

```c
#include "otezip/zip.h"

// Open ZIP file
zip_t *za = zip_open("archive.zip", ZIP_RDONLY, &err);

// Read files
zip_uint64_t num_files = zip_get_num_files(za);
for (zip_uint64_t i = 0; i < num_files; ++i) {
    zip_file_t *zf = zip_fopen_index(za, i, 0);
    // Process zf->data and zf->size
    zip_fclose(zf);
}

// Create ZIP file
zip_t *za_write = zip_open("new.zip", ZIP_CREATE, &err);
zip_source_t *src = zip_source_buffer(za_write, buffer, size, 1);
zip_file_add(za_write, "file.txt", src, 0);
zip_set_file_compression(za_write, index, OTEZIP_METHOD_ZSTD, 0);
zip_close(za_write);
```

### Command Line Tool

```bash
# List contents
./otezip -l archive.zip

# Extract files
./otezip -x archive.zip

# Create archive
./otezip -c archive.zip file1 file2

# Add files
./otezip -a archive.zip file3
```

## Configuration

Edit `config.h` to enable/disable compression algorithms:

```c
#define OTEZIP_ENABLE_DEFLATE 1
#define OTEZIP_ENABLE_ZSTD    1
#define OTEZIP_ENABLE_LZMA    1
#define OTEZIP_ENABLE_BROTLI  1
#define OTEZIP_ENABLE_LZFSE   1
```

## Supported Compression Algorithms

- **DEFLATE** (ID: 8): Standard ZIP compression
- **ZSTD** (ID: 93): Fast, high compression
- **LZMA** (ID: 14): High compression ratio
- **Brotli** (ID: 97): Better than DEFLATE for static content
- **LZFSE** (ID: 100): Apple's mobile-optimized algorithm
- **STORE** (ID: 0): No compression

## Limitations

- No encryption or ZIP64 support
- Limited multi-file extraction
- Non-standard methods may not work with all ZIP tools

## License

MIT / 0-BSD
