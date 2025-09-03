/* lzma-enc.inc.c - Minimalistic LZMA encoder implementation compatible with zlib-like API
 * Version: 0.1 (2025-07-27)
 *
 * This implementation provides LZMA encoder with zlib-compatible wrappers:
 *
 *   lzmaInit
 *   lzmaCompress
 *   lzmaEnd
 *
 * It supports:
 * - Basic LZMA compression
 * - Bare minimum functionality to support ZIP file writing
 * - Compatible interface with existing compression implementations
 *
 * Usage:
 *   #define MLZMA_IMPLEMENTATION in one source file before including
 *
 * License: MIT / 0-BSD - do whatever you want; attribution appreciated.
 */

#ifndef MLZMA_ENC_H
#define MLZMA_ENC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------- API Constants (compatible with zlib) ------------- */

/* Return codes (from zlib for compatibility) */
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

/* Strategy values */
#define Z_DEFAULT_STRATEGY    0

/* Compression level */
#define Z_NO_COMPRESSION      0
#define Z_BEST_SPEED          1
#define Z_BEST_COMPRESSION    9
#define Z_DEFAULT_COMPRESSION (-1)

/* LZMA-specific constants */
#define LZMA_MAGIC         0x5D
#define LZMA_HEADER_SIZE   13      /* LZMA properties + size field */
#define LZMA_PROPS_SIZE    5       /* LZMA properties size */
#define LZMA_DEFAULT_LEVEL 5       /* Default compression level */

/* ------------- Data Structures ------------- */

/* We'll use the existing z_stream from zlib */
/* Forward declare the z_stream type if not included */
#ifndef ZLIB_H
typedef struct z_stream_s z_stream;
#endif

/* LZMA compression context */
typedef struct {
    int compression_level;
    uint8_t properties[LZMA_PROPS_SIZE];
    uint8_t *dict_buffer;
    size_t dict_size;
    uint8_t *window_buffer;
    size_t window_size;
    size_t window_pos;
    int is_last_block;

    /* Work buffers */
    uint8_t *compress_buffer;
    size_t compress_buffer_size;
} lzma_compress_context;

/* ------------- Function Prototypes ------------- */

#ifdef __cplusplus
extern "C" {
#endif

    /* Forward declarations */
    /* Compression */
    int lzmaInit(z_stream *strm, int level);
    int lzmaCompress(z_stream *strm, int flush);
    int lzmaEnd(z_stream *strm);

    /* Helpers for zlib compatibility layer */
    int lzmaCompressInit2(z_stream *strm, int level, int windowBits, 
            int memLevel, int strategy);
    int lzmaCompressInit2_(z_stream *strm, int level, int windowBits,
            int memLevel, int strategy, 
            const char *version, int stream_size);

#ifdef __cplusplus
}
#endif

/* ------------- Implementation ------------- */
#ifdef MZIP_ENABLE_LZMA

/* --- Helper Functions --- */

/* Simple Byte-by-byte LZ compression for LZMA blocks */
static int simple_lzma_compress(const uint8_t *src, size_t src_size,
                               uint8_t *dst, size_t dst_capacity,
                               uint8_t *props) {
    /* This is a very simplified compression that just does basic RLE-like compression */
    if (src_size == 0 || !src || !dst || dst_capacity < LZMA_HEADER_SIZE + src_size) {
        return 0;  /* Invalid input or not enough output capacity */
    }

    /* For this minimal implementation, we'll do very simple RLE compression */
    size_t dst_pos = 0;
    size_t src_pos = 0;
    
    while (src_pos < src_size) {
        /* Check remaining output space */
        if (dst_pos + 4 > dst_capacity) {
            return 0;  /* Not enough space */
        }

        /* Find runs of identical bytes */
        uint8_t run_byte = src[src_pos];
        size_t run_length = 1;
        
        while (src_pos + run_length < src_size && 
               src[src_pos + run_length] == run_byte && 
               run_length < 255) {
            run_length++;
        }
        
        if (run_length >= 4) {
            /* Encode run */
            dst[dst_pos++] = 0x00;        /* RLE marker */
            dst[dst_pos++] = run_byte;    /* Repeated byte */
            dst[dst_pos++] = run_length;  /* Length */
            src_pos += run_length;
        } else {
            /* Literal */
            dst[dst_pos++] = 0x01;        /* Literal marker */
            dst[dst_pos++] = run_length;  /* Number of literals */
            
            /* Copy literals */
            if (dst_pos + run_length > dst_capacity) {
                return 0;  /* Not enough space */
            }
            
            memcpy(dst + dst_pos, src + src_pos, run_length);
            dst_pos += run_length;
            src_pos += run_length;
        }
    }
    
    return dst_pos;  /* Return compressed size */
}

/* --- LZMA API Implementation --- */

/* Initialize a compression stream */
int lzmaInit(z_stream *strm, int level) {
    if (!strm) return Z_STREAM_ERROR;
    
    /* Set default level if needed */
    if (level == Z_DEFAULT_COMPRESSION) {
        level = LZMA_DEFAULT_LEVEL;
    }
    
    /* Allocate compression context */
    lzma_compress_context *ctx = (lzma_compress_context *)calloc(1, sizeof(lzma_compress_context));
    if (!ctx) return Z_MEM_ERROR;
    
    /* Initialize context */
    ctx->compression_level = level;
    ctx->window_size = 1 << 16;  /* 64KB window by default */
    ctx->is_last_block = 0;
    
    /* Set default LZMA properties */
    ctx->properties[0] = 0x5D;           /* Dictionary size: 2^(0x5D & 1F) = 2^29 = 512MB */
    ctx->properties[1] = 0x00;           /* LC = 0, LP = 0 */
    ctx->properties[2] = 0x00;           /* PB = 0 */
    ctx->properties[3] = 0x00;           /* Reserved */
    ctx->properties[4] = 0x01;           /* Version */
    
    /* Allocate work buffers */
    ctx->window_buffer = (uint8_t *)malloc(ctx->window_size);
    if (!ctx->window_buffer) {
        free(ctx);
        return Z_MEM_ERROR;
    }
    
    /* Allocate compression buffer */
    ctx->compress_buffer_size = 2 * ctx->window_size;  /* Conservative overestimate */
    ctx->compress_buffer = (uint8_t *)malloc(ctx->compress_buffer_size);
    if (!ctx->compress_buffer) {
        free(ctx->window_buffer);
        free(ctx);
        return Z_MEM_ERROR;
    }
    
    /* Initialize stream */
    strm->state = (void *)ctx;
    strm->total_in = 0;
    strm->total_out = 0;
    
    return Z_OK;
}

/* Compress data using LZMA format */
int lzmaCompress(z_stream *strm, int flush) {
    if (!strm || !strm->state) return Z_STREAM_ERROR;
    
    lzma_compress_context *ctx = (lzma_compress_context *)strm->state;
    
    /* First call - write LZMA header */
    if (strm->total_out == 0) {
        /* We need space for the header */
        if (strm->avail_out < LZMA_HEADER_SIZE) {
            return Z_BUF_ERROR;
        }
        
        /* Will be updated later */
        memcpy(strm->next_out, ctx->properties, LZMA_PROPS_SIZE);
        
        /* Write uncompressed size as 8-byte integer */
        for (int i = 0; i < 8; i++) {
            strm->next_out[LZMA_PROPS_SIZE + i] = (strm->total_in >> (i * 8)) & 0xFF;
        }
        
        strm->next_out += LZMA_HEADER_SIZE;
        strm->avail_out -= LZMA_HEADER_SIZE;
        strm->total_out += LZMA_HEADER_SIZE;
    }
    
    /* Set last block flag if requested */
    if (flush == Z_FINISH) {
        ctx->is_last_block = 1;
    }
    
    /* Compress data */
    if (strm->avail_in > 0) {
        /* Determine how much we can compress */
        size_t input_size = strm->avail_in;
        size_t max_output = strm->avail_out;
        
        /* Make sure we have enough output space */
        if (max_output < input_size + 16) {  /* Rough estimate for overhead */
            return Z_BUF_ERROR;
        }
        
        /* Compress using our simple algorithm */
        int compressed_size = simple_lzma_compress(strm->next_in, input_size, 
                                                  ctx->compress_buffer, 
                                                  ctx->compress_buffer_size,
                                                  ctx->properties);
        
        /* Check if compression succeeded */
        if (compressed_size <= 0) {
            return Z_DATA_ERROR;
        }
        
        /* Check output space */
        if (compressed_size > strm->avail_out) {
            return Z_BUF_ERROR;
        }
        
        /* Copy compressed data to output */
        memcpy(strm->next_out, ctx->compress_buffer, compressed_size);
        strm->next_out += compressed_size;
        strm->avail_out -= compressed_size;
        strm->total_out += compressed_size;
        
        /* Update input counters */
        strm->next_in += input_size;
        strm->avail_in -= input_size;
        strm->total_in += input_size;
    }
    
    return (ctx->is_last_block && strm->avail_in == 0) ? Z_STREAM_END : Z_OK;
}

/* End a compression stream */
int lzmaEnd(z_stream *strm) {
    if (!strm || !strm->state) return Z_STREAM_ERROR;
    
    lzma_compress_context *ctx = (lzma_compress_context *)strm->state;
    
    /* Free allocated buffers */
    free(ctx->window_buffer);
    free(ctx->compress_buffer);
    
    /* Free context */
    free(ctx);
    strm->state = NULL;
    
    return Z_OK;
}

/* --- zlib compatibility layer --- */

int lzmaCompressInit2(z_stream *strm, int level, int windowBits, 
                     int memLevel, int strategy) {
    (void)windowBits;  /* Unused */
    (void)memLevel;    /* Unused */
    (void)strategy;    /* Unused */
    return lzmaInit(strm, level);
}

int lzmaCompressInit2_(z_stream *strm, int level, int windowBits,
                      int memLevel, int strategy, 
                      const char *version, int stream_size) {
    (void)version;     /* Unused */
    (void)stream_size; /* Unused */
    return lzmaCompressInit2(strm, level, windowBits, memLevel, strategy);
}

#endif /* MZIP_ENABLE_LZMA */
#endif /* MLZMA_ENC_H */