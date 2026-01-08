/* zstream.h - unified zlib-like z_stream struct for mzip */
#ifndef MZIP_ZSTREAM_H
#define MZIP_ZSTREAM_H

#include <stdint.h>

/* Match system zlib types for compatibility */
typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef void *voidpf;

/* Minimal zlib-compatible stream structure - must match system zlib layout exactly */
typedef struct {
    const Bytef *next_in;   /* next input byte */
    uInt avail_in;          /* number of bytes available at next_in */
    uLong total_in;         /* total number of input bytes read so far */

    Bytef *next_out;        /* next output byte will go here */
    uInt avail_out;         /* remaining free space at next_out */
    uLong total_out;        /* total number of bytes output so far */

    const char *msg;        /* last error message, NULL if no error */
    void *state;            /* internal state, not visible by applications */

    void *zalloc;           /* used to allocate the internal state */
    void *zfree;            /* used to free the internal state */
    voidpf opaque;          /* private data object passed to zalloc and zfree */

    int data_type;          /* best guess about the data type */
    uLong adler;            /* Adler-32 or CRC-32 value of uncompressed data */
    uLong reserved;         /* reserved for future use */
} z_stream;

#endif /* MZIP_ZSTREAM_H */
