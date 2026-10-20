/* zlib.h - zlib-compatible API of the otezip deflate implementation
 *
 * The single definition of the zlib constants, kept complete for code
 * written against zlib even where otezip does not use them. The stream
 * structure is the shared one in zstream.h. */
#ifndef OTEZIP_ZLIB_H
#define OTEZIP_ZLIB_H

#include "zstream.h"

typedef z_stream *z_streamp;

/* Allocation function types */
typedef voidpf (*alloc_func)(voidpf opaque, uInt items, uInt size);
typedef void (*free_func)(voidpf opaque, voidpf address);

#define Z_NULL 0

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

/* Flush values */
#define Z_NO_FLUSH 0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH 2
#define Z_FULL_FLUSH 3
#define Z_FINISH 4
#define Z_BLOCK 5
#define Z_TREES 6

/* Compression levels */
#define Z_NO_COMPRESSION 0
#define Z_BEST_SPEED 1
#define Z_BEST_COMPRESSION 9
#define Z_DEFAULT_COMPRESSION (-1)

/* Compression strategy */
#define Z_FILTERED 1
#define Z_HUFFMAN_ONLY 2
#define Z_RLE 3
#define Z_FIXED 4
#define Z_DEFAULT_STRATEGY 0

/* Data types */
#define Z_BINARY 0
#define Z_TEXT 1
#define Z_ASCII Z_TEXT
#define Z_UNKNOWN 2

/* Window bits (32K window) */
#define MAX_WBITS 15

/* deflate compression method */
#define Z_DEFLATED 8

#define ZLIB_VERSION "1.2.11"

#ifdef __cplusplus
extern "C" {
#endif

int inflateInit2(z_stream *strm, int windowBits);
int inflateInit2_(z_stream *strm, int windowBits, const char *version, int stream_size);
int inflate(z_stream *strm, int flush);
int inflateEnd(z_stream *strm);

int deflateInit2(z_stream *strm, int level, int method, int windowBits, int memLevel, int strategy);
int deflateInit2_(z_stream *strm, int level, int method, int windowBits, int memLevel, int strategy, const char *version, int stream_size);
int deflate(z_stream *strm, int flush);
int deflateEnd(z_stream *strm);

#ifdef __cplusplus
}
#endif

/* Convenience macros */
#define inflateInit(strm) inflateInit2 ((strm), MAX_WBITS)
#define deflateInit(strm, level) deflateInit2 ((strm), (level), Z_DEFLATED, MAX_WBITS, 8, Z_DEFAULT_STRATEGY)

#endif /* OTEZIP_ZLIB_H */
