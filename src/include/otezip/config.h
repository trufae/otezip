/* config.h - Configuration options for otezip */

#ifndef OTEZIP_CONFIG_H
#define OTEZIP_CONFIG_H

#define OTEZIP_VERSION_MAJOR 0
#define OTEZIP_VERSION_MINOR 6
#define OTEZIP_VERSION_PATCH 0

#define OTEZIP_STRINGIFY(x) #x
#define OTEZIP_TOSTRING(x) OTEZIP_STRINGIFY(x)

#define OTEZIP_VERSION OTEZIP_TOSTRING(OTEZIP_VERSION_MAJOR) "." OTEZIP_TOSTRING(OTEZIP_VERSION_MINOR) "." OTEZIP_TOSTRING(OTEZIP_VERSION_PATCH)

// Compression algorithm selection
#define OTEZIP_ENABLE_STORE 1
#define OTEZIP_ENABLE_DEFLATE 1
#define OTEZIP_ENABLE_ZSTD 1
#define OTEZIP_ENABLE_LZFSE 1
// #define OTEZIP_ENABLE_LZ4 1
#define OTEZIP_ENABLE_LZMA 1
#define OTEZIP_ENABLE_BROTLI 1

// ---------------------------------------------- //

// Compression algorithm ID numbers (from ZIP spec)
// ** DO NOT CHANGE - these are standard values **
#define OTEZIP_METHOD_STORE    0
#define OTEZIP_METHOD_DEFLATE  8
#define OTEZIP_METHOD_LZMA    14
#define OTEZIP_METHOD_ZSTD    93
/* Brotli and LZ4 have no APPNOTE IDs; 97 is WavPack and 94 is MP3 there */
#define OTEZIP_METHOD_BROTLI  97
#define OTEZIP_METHOD_LZ4     94
/* Not in the ZIP spec. Apple reportedly writes LZFSE as 99 (shared with
 * AE-x encryption), but our minimal codec emits raw blocks without the
 * "bvx" framing, so its output must not claim Apple's ID */
#define OTEZIP_METHOD_LZFSE  100

// Compression buffer sizing constants
#define OTEZIP_LZMA_HEADER_SIZE       13 /* LZMA header size */
#define OTEZIP_LZMA_OVERHEAD_RATIO    8  /* 1/8 overhead for incompressible data */

#endif /* OTEZIP_CONFIG_H */
