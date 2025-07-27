/* config.h - Configuration options for mzip compression algorithms
 * Version: 0.1 (2025-07-27)
 *
 * This file controls which compression algorithms are included in the mzip build.
 * Each algorithm is conditionally compiled based on the defines below.
 */

#ifndef MZIP_CONFIG_H
#define MZIP_CONFIG_H

/*
 * Compression algorithm selection
 * 
 * Define these to include support for specific algorithms
 * Comment out any algorithms you don't want to include
 */

/* Always include STORE method (uncompressed files) */
#define MZIP_ENABLE_STORE 1

/* Include DEFLATE compression (requires zlib) */
#define MZIP_ENABLE_DEFLATE 1

/* Include ZSTD compression */
#define MZIP_ENABLE_ZSTD 1

/* LZFSE compression support */
#define MZIP_ENABLE_LZFSE 1

/* LZ4 compression support (using radare2's rlz4.c) */
/*
#define MZIP_ENABLE_LZ4 1
*/

/* LZMA compression support */
#define MZIP_ENABLE_LZMA 1

/* Brotli compression support */
#define MZIP_ENABLE_BROTLI 1

/* Future algorithms that could be supported */

/* 
 * Compression algorithm ID numbers (from ZIP spec)
 * DO NOT CHANGE - these are standard values
 */
#define MZIP_METHOD_STORE    0
#define MZIP_METHOD_DEFLATE  8
#define MZIP_METHOD_LZMA    14
#define MZIP_METHOD_ZSTD    93
#define MZIP_METHOD_BROTLI  97
#define MZIP_METHOD_LZ4     94
/* LZFSE is not officially in ZIP spec, using Apple-specific range */
#define MZIP_METHOD_LZFSE  100  /* Apple-specific range */

#endif /* MZIP_CONFIG_H */
