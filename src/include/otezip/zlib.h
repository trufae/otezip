/* zlib.h - minimal zlib compatibility header for otezip */
#ifndef OTEZIP_ZLIB_H
#define OTEZIP_ZLIB_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* zlib types */
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef void *voidpf;

/* Allocation function types */
typedef voidpf (*alloc_func)(voidpf opaque, uInt items, uInt size);
typedef void (*free_func)(voidpf opaque, voidpf address);

#define Z_NULL 0

/* zlib stream structure */
typedef struct z_stream_s {
    const Bytef *next_in;   /* next input byte */
    uInt avail_in;          /* number of bytes available at next_in */
    uLong total_in;         /* total number of input bytes read so far */

    Bytef *next_out;        /* next output byte will go here */
    uInt avail_out;         /* remaining free space at next_out */
    uLong total_out;        /* total number of bytes output so far */

    const char *msg;        /* last error message, NULL if no error */
    void *state;            /* internal state, not visible by applications */

    alloc_func zalloc;      /* used to allocate the internal state */
    free_func zfree;        /* used to free the internal state */
    voidpf opaque;          /* private data object passed to zalloc and zfree */

    int data_type;          /* best guess about the data type */
    uLong adler;            /* Adler-32 or CRC-32 value of uncompressed data */
    uLong reserved;         /* reserved for future use */
} z_stream;

typedef z_stream *z_streamp;

/* Return codes */
#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)

/* Flush values */
#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#define Z_BLOCK         5
#define Z_TREES         6

/* Compression levels */
#define Z_NO_COMPRESSION      0
#define Z_BEST_SPEED          1
#define Z_BEST_COMPRESSION    9
#define Z_DEFAULT_COMPRESSION (-1)

/* Compression strategy */
#define Z_FILTERED         1
#define Z_HUFFMAN_ONLY     2
#define Z_RLE              3
#define Z_FIXED            4
#define Z_DEFAULT_STRATEGY 0

/* Data types */
#define Z_BINARY   0
#define Z_TEXT     1
#define Z_ASCII    Z_TEXT
#define Z_UNKNOWN  2

/* Window bits */
#define MAX_WBITS 15

/* deflate compression method */
#define Z_DEFLATED 8

/* Function declarations - these are stubs, implement as needed or link real zlib */
int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size);
int inflate(z_streamp strm, int flush);
int inflateEnd(z_streamp strm);

int deflateInit2_(z_streamp strm, int level, int method, int windowBits,
                  int memLevel, int strategy, const char *version, int stream_size);
int deflate(z_streamp strm, int flush);
int deflateEnd(z_streamp strm);

/* Convenience macros */
#define ZLIB_VERSION "1.2.11"
#define inflateInit(strm) inflateInit2_((strm), MAX_WBITS, ZLIB_VERSION, (int)sizeof(z_stream))
#define inflateInit2(strm, windowBits) inflateInit2_((strm), (windowBits), ZLIB_VERSION, (int)sizeof(z_stream))
#define deflateInit(strm, level) deflateInit2_((strm), (level), Z_DEFLATED, MAX_WBITS, 8, Z_DEFAULT_STRATEGY, ZLIB_VERSION, (int)sizeof(z_stream))
#define deflateInit2(strm, level, method, windowBits, memLevel, strategy) \
    deflateInit2_((strm), (level), (method), (windowBits), (memLevel), (strategy), ZLIB_VERSION, (int)sizeof(z_stream))

#endif /* OTEZIP_ZLIB_H */
