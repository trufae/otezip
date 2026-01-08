/* deflate.inc.c - Minimalistic deflate (RFC 1951) implementation compatible with zlib API
 * Version: 0.1 (2025-07-27)
 *
 * This single-file implementation provides a tiny subset of the zlib API:
 *
 *   inflateInit2
 *   inflate
 *   inflateEnd
 *   deflateInit2
 *   deflate
 *   deflateEnd
 *
 * It supports:
 * - Raw inflate/deflate (RFC 1951) with no wrappers
 * - Bare minimum functionality to support ZIP file reading/writing
 * - No checksums, dictionaries, or other advanced features
 *
 * Usage:
 *   #define MDEFLATE_IMPLEMENTATION in one source file before including
 *
 * License: MIT / 0-BSD - do whatever you want; attribution appreciated.
 */

#ifndef MDEFLATE_H
#define MDEFLATE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------- API Constants (compatible with zlib) ------------- */

/* Return codes */
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_NEED_DICT 2
#define Z_ERRNO (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR (-3)
#define Z_MEM_ERROR (-4)
#define Z_BUF_ERROR (-5)
#define Z_VERSION_ERROR (-6)

#define Z_DEFLATED 8

/* Flush values */
#define Z_NO_FLUSH 0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH 2
#define Z_FULL_FLUSH 3
#define Z_FINISH 4

/* Strategy values */
#define Z_FILTERED 1
#define Z_HUFFMAN_ONLY 2
#define Z_RLE 3
#define Z_FIXED 4
#define Z_DEFAULT_STRATEGY 0

/* Compression level */
#define Z_NO_COMPRESSION 0
#define Z_BEST_SPEED 1
#define Z_BEST_COMPRESSION 9
#define Z_DEFAULT_COMPRESSION (-1)

/* Window bits */
#define MAX_WBITS 15 /* 32K window size */

/* ------------- Data Structures ------------- */

/* zlib-like basic typedefs used by callers */
typedef unsigned long uLong;
typedef unsigned int uInt;

/* Conservative upper bound for deflate output size, zlib-compatible API */
static inline uLong compressBound(uLong sourceLen) {
	/* Conservative bound: input + 1/8 + fixed overhead */
	uLong extra = (sourceLen >> 3) + 64u;
	uLong bound = sourceLen + extra + 11u;
	return (bound < sourceLen)? sourceLen: bound;
}

/* Unified z_stream definition */
#include "../include/otezip/zstream.h"

/* ------------- Function Prototypes ------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
int inflateInit2(z_stream *strm, int windowBits);
int inflateInit2_(z_stream *strm, int windowBits, const char *version, int stream_size);
int inflate(z_stream *strm, int flush);
int inflateEnd(z_stream *strm);

int deflateInit2(z_stream *strm, int level, int method, int windowBits,
	int memLevel, int strategy);
int deflateInit2_(z_stream *strm, int level, int method, int windowBits,
	int memLevel, int strategy, const char *version, int stream_size);
int deflate(z_stream *strm, int flush);
int deflateEnd(z_stream *strm);

#ifdef __cplusplus
}
#endif

/* ------------- Implementation ------------- */
#ifdef MDEFLATE_IMPLEMENTATION

/* Huffman code table */
typedef struct {
	uint16_t codes[288]; /* Huffman codes */
	uint8_t lengths[288]; /* Code lengths */
	uint16_t count; /* Number of codes */
} huffman_table;

/* Block type */
typedef enum {
	BLOCK_UNCOMPRESSED = 0,
	BLOCK_FIXED = 1,
	BLOCK_DYNAMIC = 2,
	BLOCK_INVALID = 3
} block_type;

/* ----------- Utility Functions ----------- */

/* Calculate hash for LZ77 */
static uint32_t calculate_hash(const uint8_t *data) {
	return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

/* Include the encoder and decoder files */
#include "deflate-enc.inc.c"
#include "deflate-dec.inc.c"

#endif /* MDEFLATE_IMPLEMENTATION */
#endif /* MDEFLATE_H */
