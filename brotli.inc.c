/* brotli.inc.c - Minimalistic Brotli implementation compatible with zlib-like API
 * Version: 0.1 (2025-07-29)
 *
 * This single-file implementation provides a tiny subset of Brotli API with
 * zlib-compatible wrappers:
 *
 *   brotliInit
 *   brotliCompress
 *   brotliEnd
 *   brotliDecompressInit
 *   brotliDecompress
 *   brotliDecompressEnd
 *
 * It supports:
 * - Basic Brotli compression/decompression
 * - Bare minimum functionality to support ZIP file reading/writing
 * - Compatible interface with existing compression implementations
 *
 * Usage:
 *   #define MBROTLI_IMPLEMENTATION in one source file before including
 *
 * License: MIT
 */

#ifndef MBROTLI_H
#define MBROTLI_H

/* Include the encoder and decoder components */
#include "brotli-enc.inc.c"
#include "brotli-dec.inc.c"

#endif /* MBROTLI_H */