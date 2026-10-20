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

/* zlib-compatible constants, z_stream and prototypes */
#include "../include/otezip/zlib.h"

/* ------------- Data Structures ------------- */

/* Conservative upper bound for deflate output size, zlib-compatible API */
static inline unsigned long compressBound(unsigned long sourceLen) {
	/* Conservative bound: input + 1/8 + fixed overhead */
	unsigned long extra = (sourceLen >> 3) + 64u;
	unsigned long bound = sourceLen + extra + 11u;
	return (bound < sourceLen)? sourceLen: bound;
}

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
