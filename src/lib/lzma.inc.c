/* lzma.inc.c - Minimalistic LZMA implementation compatible with zlib-like API
 * Version: 0.1 (2025-07-27)
 *
 * This implementation provides LZMA compression/decompression with zlib-compatible API
 * by including separate encoder and decoder implementations.
 *
 * Usage:
 *   #define OTEZIP_ENABLE_LZMA in one source file before including
 *
 * License: MIT / 0-BSD - do whatever you want; attribution appreciated.
 */

#ifndef MLZMA_H
#define MLZMA_H

/* Include encoder and decoder implementations */
#include "lzma-enc.inc.c"
#include "lzma-dec.inc.c"

#endif /* MLZMA_H */