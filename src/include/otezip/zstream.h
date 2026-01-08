/* zstream.h - unified zlib-like z_stream struct for mzip */
#ifndef MZIP_ZSTREAM_H
#define MZIP_ZSTREAM_H

#include <stdint.h>

/* Minimal zlib-compatible stream structure used by our codecs */
typedef struct {
    uint8_t *next_in;
    uint32_t avail_in;
    uint32_t total_in;

    uint8_t *next_out;
    uint32_t avail_out;
    uint32_t total_out;

    void *state;
} z_stream;

#endif /* MZIP_ZSTREAM_H */

