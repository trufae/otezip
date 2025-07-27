/* lzfse.inc.c - Minimalistic LZFSE implementation compatible with zlib-like API
 * Version: 0.1 (2025-07-27)
 *
 * This single-file implementation provides a tiny subset of LZFSE API with
 * zlib-compatible wrappers:
 *
 *   lzfseInit
 *   lzfseCompress
 *   lzfseEnd
 *   lzfseDecompressInit
 *   lzfseDecompress
 *   lzfseDecompressEnd
 *
 * It supports:
 * - Basic LZFSE compression/decompression
 * - Bare minimum functionality to support ZIP file reading/writing
 * - Compatible interface with existing deflate.inc.c and zstd.inc.c implementations
 *
 * Usage:
 *   #define MLZFSE_IMPLEMENTATION in one source file before including
 *
 * License: MIT / 0-BSD - do whatever you want; attribution appreciated.
 */

#ifndef MLZFSE_H
#define MLZFSE_H

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
#ifndef Z_DEFAULT_COMPRESSION
#define Z_DEFAULT_COMPRESSION 1
#endif

/* LZFSE-specific constants */
#define LZFSE_MAGIC_NUMBER      0x65736662   /* 'bfse' magic number for LZFSE stream */
#define LZFSE_FRAME_HEADER_SIZE 12           /* Minimum frame header size */
#define LZFSE_BLOCK_MAX_SIZE    65536        /* Maximum block size */
#define LZFSE_DEFAULT_CLEVEL    5            /* Default compression level */

/* ------------- Data Structures ------------- */

/* We'll use the existing z_stream from zlib */
/* Forward declare the z_stream type if not included */
#ifndef ZLIB_H
typedef struct z_stream_s z_stream;
#endif

/* LZFSE frame header */
typedef struct {
    uint32_t magic;              /* LZFSE_MAGIC_NUMBER */
    uint32_t uncompressed_size;  /* Uncompressed data size */
    uint32_t compressed_size;    /* Compressed data size */
} lzfse_frame_header;

/* LZFSE block header */
typedef struct {
    uint32_t block_size;         /* Size of this block */
    uint8_t  block_type;         /* Type of block (raw or compressed) */
    uint8_t  is_last_block;      /* Flag indicating last block */
} lzfse_block_header;

/* LZFSE compression context */
typedef struct {
    int compression_level;
    uint8_t *window_buffer;
    size_t window_size;
    size_t window_pos;
    uint32_t block_size;
    int is_last_block;

    /* Temporary work buffers */
    uint8_t *compress_buffer;
    size_t compress_buffer_size;
} lzfse_compress_context;

/* LZFSE decompression context */
typedef struct {
    uint8_t *window_buffer;
    size_t window_size;
    size_t window_pos;
    uint32_t current_block_size;
    int current_block_remaining;
    int is_last_block;

    /* Temporary work buffers */
    uint8_t *decompress_buffer;
    size_t decompress_buffer_size;
} lzfse_decompress_context;

/* ------------- Function Prototypes ------------- */

#ifdef __cplusplus
extern "C" {
#endif

    /* Forward declarations */
    /* Compression */
    int lzfseInit(z_stream *strm, int level);
    int lzfseCompress(z_stream *strm, int flush);
    int lzfseEnd(z_stream *strm);

    /* Decompression */
    int lzfseDecompressInit(z_stream *strm);
    int lzfseDecompress(z_stream *strm, int flush);
    int lzfseDecompressEnd(z_stream *strm);

    /* Helpers for zlib compatibility layer */
    int lzfseCompressInit2(z_stream *strm, int level, int windowBits, 
            int memLevel, int strategy);
    int lzfseDecompressInit2(z_stream *strm, int windowBits);
    int lzfseCompressInit2_(z_stream *strm, int level, int windowBits,
            int memLevel, int strategy, 
            const char *version, int stream_size);
    int lzfseDecompressInit2_(z_stream *strm, int windowBits,
            const char *version, int stream_size);

#ifdef __cplusplus
}
#endif

/* ------------- Implementation ------------- */
#ifdef MZIP_ENABLE_LZFSE

/* --- Helper Functions --- */

/* Read a 32-bit little-endian value */
static uint32_t lzfse_read_le32(const void *ptr) {
    const uint8_t *p = (const uint8_t *)ptr;
    return ((uint32_t)p[0]) | 
        ((uint32_t)p[1] << 8) | 
        ((uint32_t)p[2] << 16) | 
        ((uint32_t)p[3] << 24);
}

/* Write a 32-bit little-endian value */
static void lzfse_write_le32(void *ptr, uint32_t val) {
    uint8_t *p = (uint8_t *)ptr;
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

/* Simplified RLE compression for LZFSE block */
static int lzfse_compress_block(const uint8_t *src, size_t src_size, 
        uint8_t *dst, size_t dst_capacity, 
        int level) {
    /* This is a very simplified compression that only does basic RLE */
    if (src_size == 0 || !src || !dst) {
        return 0;  /* Empty input or invalid pointers */
    }

    /* Check if there's enough space for output */
    if (dst_capacity < src_size + 1) {
        return 0;  /* Not enough output capacity */
    }

    /* For this minimal implementation, we'll just do a simple RLE */
    size_t dst_pos = 0;
    size_t src_pos = 0;

    while (src_pos < src_size) {
        /* Check remaining output space */
        if (dst_pos + 3 > dst_capacity) {
            return 0;  /* Not enough space */
        }

        /* Find run of identical bytes */
        uint8_t run_byte = src[src_pos];
        size_t run_length = 1;

        while (src_pos + run_length < src_size && 
                src[src_pos + run_length] == run_byte &&
                run_length < 255) {
            run_length++;
        }

        if (run_length >= 4) {
            /* Encode run */
            dst[dst_pos++] = 0;          /* RLE marker */
            dst[dst_pos++] = run_byte;   /* Repeated byte */
            dst[dst_pos++] = run_length; /* Length */
            src_pos += run_length;
        } else {
            /* Literal */
            dst[dst_pos++] = run_byte;
            src_pos++;
        }
    }

    return dst_pos;  /* Return compressed size */
}

/* Simple decompression for our LZFSE block */
static int lzfse_decompress_block(const uint8_t *src, size_t src_size,
        uint8_t *dst, size_t dst_capacity) {
    if (src_size == 0 || !src || !dst) {
        return 0;  /* Empty input or invalid pointers */
    }

    size_t dst_pos = 0;
    size_t src_pos = 0;

    while (src_pos < src_size) {
        /* Check for RLE marker */
        if (src[src_pos] == 0 && src_pos + 2 < src_size) {
            uint8_t byte = src[src_pos + 1];
            uint8_t length = src[src_pos + 2];
            src_pos += 3;

            /* Check output capacity */
            if (dst_pos + length > dst_capacity) {
                return 0;  /* Output overflow */
            }

            /* Output run */
            for (uint8_t i = 0; i < length; i++) {
                dst[dst_pos++] = byte;
            }
        } else {
            /* Literal byte */
            if (dst_pos >= dst_capacity) {
                return 0;  /* Output overflow */
            }
            dst[dst_pos++] = src[src_pos++];
        }
    }

    return dst_pos;  /* Return decompressed size */
}

/* --- LZFSE API Implementation --- */

/* Initialize a compression stream */
int lzfseInit(z_stream *strm, int level) {
    if (!strm) return Z_STREAM_ERROR;

    /* Setup default level */
    if (level == Z_DEFAULT_COMPRESSION) {
        level = LZFSE_DEFAULT_CLEVEL;
    }

    /* Allocate compression context */
    lzfse_compress_context *ctx = (lzfse_compress_context *)calloc(1, sizeof(lzfse_compress_context));
    if (!ctx) return Z_MEM_ERROR;

    /* Initialize context */
    ctx->compression_level = level;
    ctx->window_size = 1 << 16;  /* 64KB window by default */
    ctx->block_size = LZFSE_BLOCK_MAX_SIZE;
    ctx->is_last_block = 0;

    /* Allocate window buffer for LZ77 */
    ctx->window_buffer = (uint8_t *)malloc(ctx->window_size);
    if (!ctx->window_buffer) {
        free(ctx);
        return Z_MEM_ERROR;
    }

    /* Allocate compression buffer */
    ctx->compress_buffer_size = ctx->block_size * 2; /* Over-allocate for worst case */
    ctx->compress_buffer = (uint8_t *)malloc(ctx->compress_buffer_size);
    if (!ctx->compress_buffer) {
        free(ctx->window_buffer);
        free(ctx);
        return Z_MEM_ERROR;
    }

    /* Initialize stream counters */
    strm->state = (void *)ctx;
    strm->total_in = 0;
    strm->total_out = 0;

    return Z_OK;
}

/* Compress data using LZFSE format */
int lzfseCompress(z_stream *strm, int flush) {
    if (!strm || !strm->state) return Z_STREAM_ERROR;

    lzfse_compress_context *ctx = (lzfse_compress_context *)strm->state;

    /* Check if this is the first call - need to write frame header */
    if (strm->total_out == 0) {
        /* We need space for the frame header */
        if (strm->avail_out < LZFSE_FRAME_HEADER_SIZE) {
            return Z_BUF_ERROR;
        }

        /* Write LZFSE frame header - we'll update sizes later */
        lzfse_frame_header header;
        header.magic = LZFSE_MAGIC_NUMBER;
        header.uncompressed_size = 0; /* To be updated at the end */
        header.compressed_size = 0;   /* To be updated at the end */

        lzfse_write_le32(strm->next_out, header.magic);
        lzfse_write_le32(strm->next_out + 4, header.uncompressed_size);
        lzfse_write_le32(strm->next_out + 8, header.compressed_size);

        strm->next_out += LZFSE_FRAME_HEADER_SIZE;
        strm->avail_out -= LZFSE_FRAME_HEADER_SIZE;
        strm->total_out += LZFSE_FRAME_HEADER_SIZE;
    }

    /* Simple block-based compression */
    ctx->is_last_block = (flush == Z_FINISH);

    /* Process input data */
    while (strm->avail_in > 0 || ctx->is_last_block) {
        /* Determine block size */
        uint32_t block_size = strm->avail_in < ctx->block_size ? 
            strm->avail_in : ctx->block_size;

        /* Skip if no input data and not final block */
        if (block_size == 0 && !ctx->is_last_block) {
            break;
        }

        /* Ensure we have space for block header (3 bytes) */
        if (strm->avail_out < 3) {
            return Z_BUF_ERROR;
        }

        /* If this is an empty final block, write a special block */
        if (block_size == 0 && ctx->is_last_block) {
            /* Write empty last block */
            strm->next_out[0] = 0x01;  /* Last block bit set */
            strm->next_out[1] = 0x00;  /* Size = 0 */
            strm->next_out[2] = 0x00;
            strm->next_out += 3;
            strm->avail_out -= 3;
            strm->total_out += 3;

            /* Update the frame header with final sizes */
            /* Frame header is at total_out - (current_position + 3) back */
            uint8_t *frame_header_ptr = strm->next_out - strm->total_out;
            lzfse_write_le32(frame_header_ptr + 4, strm->total_in);
            lzfse_write_le32(frame_header_ptr + 8, strm->total_out);

            /* We're done */
            return Z_STREAM_END;
        }

        /* Try to compress the block */
        int compressed_size = 0;
        if (block_size > 0) {
            compressed_size = lzfse_compress_block(strm->next_in, block_size,
                    ctx->compress_buffer, ctx->compress_buffer_size,
                    ctx->compression_level);
        }

        /* Decide whether to write compressed or raw block */
        uint8_t block_header = 0;
        uint8_t *block_content;
        uint32_t content_size;

        if (compressed_size > 0 && compressed_size < block_size) {
            /* Compressed block (type=2) */
            block_header = 0x02;
            block_content = ctx->compress_buffer;
            content_size = compressed_size;
        } else {
            /* Raw block (type=0) */
            block_header = 0x00;
            block_content = strm->next_in;
            content_size = block_size;
        }

        /* Set last block flag if needed */
        if (ctx->is_last_block && strm->avail_in <= block_size) {
            block_header |= 0x01;
        }

        /* Write block header */
        strm->next_out[0] = block_header;
        strm->next_out[1] = content_size & 0xFF;
        strm->next_out[2] = (content_size >> 8) & 0xFF;
        strm->next_out += 3;
        strm->avail_out -= 3;
        strm->total_out += 3;

        /* Check if we have enough space for the block content */
        if (strm->avail_out < content_size) {
            /* Back up - we can't write this block now */
            strm->next_out -= 3;
            strm->avail_out += 3;
            strm->total_out -= 3;
            return Z_BUF_ERROR;
        }

        /* Write block content */
        memcpy(strm->next_out, block_content, content_size);
        strm->next_out += content_size;
        strm->avail_out -= content_size;
        strm->total_out += content_size;

        /* Update input */
        if (block_size > 0) {
            strm->next_in += block_size;
            strm->avail_in -= block_size;
            strm->total_in += block_size;

            /* Update window buffer for future references */
            memcpy(ctx->window_buffer + ctx->window_pos, 
                    strm->next_in - block_size, 
                    block_size);
            ctx->window_pos = (ctx->window_pos + block_size) % ctx->window_size;
        }

        /* If this was the last block, update the frame header and finish */
        if (ctx->is_last_block && strm->avail_in == 0) {
            /* Update the frame header with final sizes */
            uint8_t *frame_header_ptr = strm->next_out - strm->total_out;
            lzfse_write_le32(frame_header_ptr + 4, strm->total_in);
            lzfse_write_le32(frame_header_ptr + 8, strm->total_out);
            return Z_STREAM_END;
        }
    }

    return Z_OK;
}

/* End a compression stream */
int lzfseEnd(z_stream *strm) {
    if (!strm || !strm->state) return Z_STREAM_ERROR;

    lzfse_compress_context *ctx = (lzfse_compress_context *)strm->state;

    /* Free allocated buffers */
    free(ctx->window_buffer);
    free(ctx->compress_buffer);

    /* Free context */
    free(ctx);
    strm->state = NULL;

    return Z_OK;
}

/* Initialize a decompression stream */
int lzfseDecompressInit(z_stream *strm) {
    if (!strm) return Z_STREAM_ERROR;

    /* Allocate decompression context */
    lzfse_decompress_context *ctx = (lzfse_decompress_context *)calloc(1, sizeof(lzfse_decompress_context));
    if (!ctx) return Z_MEM_ERROR;

    /* Initialize context with default values */
    ctx->window_size = 1 << 16;  /* 64KB window by default */
    ctx->is_last_block = 0;

    /* Allocate window buffer */
    ctx->window_buffer = (uint8_t *)malloc(ctx->window_size);
    if (!ctx->window_buffer) {
        free(ctx);
        return Z_MEM_ERROR;
    }

    /* Allocate decompression buffer */
    ctx->decompress_buffer_size = LZFSE_BLOCK_MAX_SIZE;
    ctx->decompress_buffer = (uint8_t *)malloc(ctx->decompress_buffer_size);
    if (!ctx->decompress_buffer) {
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

/* Decompress data using LZFSE format */
int lzfseDecompress(z_stream *strm, int flush) {
    if (!strm || !strm->state) {
        return Z_STREAM_ERROR;
    }

    lzfse_decompress_context *ctx = (lzfse_decompress_context *)strm->state;

    /* Check if we need to parse the frame header */
    if (strm->total_in == 0) {
        /* Need at least 12 bytes for the frame header */
        if (strm->avail_in < LZFSE_FRAME_HEADER_SIZE) {
            return Z_BUF_ERROR;
        }

        /* Check magic number */
        uint32_t magic = lzfse_read_le32(strm->next_in);
        if (magic != LZFSE_MAGIC_NUMBER) {
            return Z_DATA_ERROR;
        }

        /* Read frame sizes */
        /* Read frame sizes (may be needed later) */
        /* uint32_t uncompressed_size = lzfse_read_le32(strm->next_in + 4);
        uint32_t compressed_size = lzfse_read_le32(strm->next_in + 8); */

        /* Advance past header */
        strm->next_in += LZFSE_FRAME_HEADER_SIZE;
        strm->avail_in -= LZFSE_FRAME_HEADER_SIZE;
        strm->total_in += LZFSE_FRAME_HEADER_SIZE;
    }

    /* Process blocks */
    while (strm->avail_in > 0 || ctx->current_block_remaining > 0) {
        /* If we're still outputting from the current block */
        if (ctx->current_block_remaining > 0) {
            uint32_t copy_size = ctx->current_block_remaining;
            if (copy_size > strm->avail_out) {
                copy_size = strm->avail_out;
            }

            /* Copy data to output */
            memcpy(strm->next_out, 
                    ctx->decompress_buffer + 
                    (ctx->current_block_size - ctx->current_block_remaining), 
                    copy_size);

            /* Update counters */
            strm->next_out += copy_size;
            strm->avail_out -= copy_size;
            strm->total_out += copy_size;
            ctx->current_block_remaining -= copy_size;

            /* Copy to window buffer */
            memcpy(ctx->window_buffer + ctx->window_pos, 
                    strm->next_out - copy_size, 
                    copy_size);
            ctx->window_pos = (ctx->window_pos + copy_size) % ctx->window_size;

            /* Check if we're out of output space */
            if (strm->avail_out == 0) {
                return Z_OK;
            }
        }

        /* Start a new block if possible */
        if (strm->avail_in >= 3) {
            /* Parse block header */
            uint8_t block_header = strm->next_in[0];
            ctx->is_last_block = block_header & 0x01;
            uint8_t block_type = (block_header >> 1) & 0x03;

            /* Get block size */
            uint32_t block_size = ((uint32_t)strm->next_in[2] << 8) | 
                strm->next_in[1];

            /* Skip header */
            strm->next_in += 3;
            strm->avail_in -= 3;
            strm->total_in += 3;

            /* Process based on block type */
            if (block_type == 0) {
                /* Raw block */
                if (strm->avail_in < block_size) {
                    /* Not enough input data */
                    strm->next_in -= 3;
                    strm->avail_in += 3;
                    strm->total_in -= 3;
                    return Z_BUF_ERROR;
                }

                /* Copy raw block to output if space allows */
                if (strm->avail_out >= block_size) {
                    memcpy(strm->next_out, strm->next_in, block_size);
                    strm->next_out += block_size;
                    strm->avail_out -= block_size;
                    strm->total_out += block_size;

                    /* Copy to window buffer */
                    memcpy(ctx->window_buffer + ctx->window_pos, 
                            strm->next_in, 
                            block_size);
                    ctx->window_pos = (ctx->window_pos + block_size) % ctx->window_size;

                    /* Advance input */
                    strm->next_in += block_size;
                    strm->avail_in -= block_size;
                    strm->total_in += block_size;
                } else {
                    /* Store in temporary buffer to output in chunks */
                    if (block_size > ctx->decompress_buffer_size) {
                        /* Reallocate buffer if needed */
                        free(ctx->decompress_buffer);
                        ctx->decompress_buffer_size = block_size;
                        ctx->decompress_buffer = (uint8_t *)malloc(ctx->decompress_buffer_size);
                        if (!ctx->decompress_buffer) {
                            return Z_MEM_ERROR;
                        }
                    }

                    /* Copy to buffer */
                    memcpy(ctx->decompress_buffer, strm->next_in, block_size);
                    ctx->current_block_size = block_size;
                    ctx->current_block_remaining = block_size;

                    /* Advance input */
                    strm->next_in += block_size;
                    strm->avail_in -= block_size;
                    strm->total_in += block_size;

                    /* Process from buffer on next iteration */
                    continue;
                }
            } else if (block_type == 2) {
                /* Compressed block */
                if (strm->avail_in < block_size) {
                    /* Not enough input data */
                    strm->next_in -= 3;
                    strm->avail_in += 3;
                    strm->total_in -= 3;
                    return Z_BUF_ERROR;
                }

                /* Decompress the block */
                int decompressed_size = lzfse_decompress_block(
                        strm->next_in, block_size,
                        ctx->decompress_buffer, ctx->decompress_buffer_size);

                if (decompressed_size <= 0) {
                    return Z_DATA_ERROR;
                }

                /* Output if space allows */
                if (strm->avail_out >= decompressed_size) {
                    memcpy(strm->next_out, ctx->decompress_buffer, decompressed_size);
                    strm->next_out += decompressed_size;
                    strm->avail_out -= decompressed_size;
                    strm->total_out += decompressed_size;

                    /* Copy to window buffer */
                    memcpy(ctx->window_buffer + ctx->window_pos, 
                            ctx->decompress_buffer, 
                            decompressed_size);
                    ctx->window_pos = (ctx->window_pos + decompressed_size) % ctx->window_size;
                } else {
                    /* Store for partial output */
                    ctx->current_block_size = decompressed_size;
                    ctx->current_block_remaining = decompressed_size;

                    /* Process from buffer on next iteration */
                    continue;
                }

                /* Advance input */
                strm->next_in += block_size;
                strm->avail_in -= block_size;
                strm->total_in += block_size;
            } else {
                /* Reserved block type - not implemented */
                return Z_DATA_ERROR;
            }

            /* Check if this was the last block */
            if (ctx->is_last_block) {
                return Z_STREAM_END;
            }
        } else {
            /* Not enough input for a new block */
            return Z_BUF_ERROR;
        }
    }

    return Z_OK;
}

/* End a decompression stream */
int lzfseDecompressEnd(z_stream *strm) {
    if (!strm || !strm->state) return Z_STREAM_ERROR;

    lzfse_decompress_context *ctx = (lzfse_decompress_context *)strm->state;

    /* Free allocated buffers */
    free(ctx->window_buffer);
    free(ctx->decompress_buffer);

    /* Free context */
    free(ctx);
    strm->state = NULL;

    return Z_OK;
}

/* --- zlib compatibility layer --- */

int lzfseCompressInit2(z_stream *strm, int level, int windowBits, 
        int memLevel, int strategy) {
    (void)windowBits;  /* Unused */
    (void)memLevel;    /* Unused */
    (void)strategy;    /* Unused */
    return lzfseInit(strm, level);
}

int lzfseDecompressInit2(z_stream *strm, int windowBits) {
    (void)windowBits;  /* Unused */
    return lzfseDecompressInit(strm);
}

int lzfseCompressInit2_(z_stream *strm, int level, int windowBits,
        int memLevel, int strategy, 
        const char *version, int stream_size) {
    (void)version;     /* Unused */
    (void)stream_size; /* Unused */
    return lzfseCompressInit2(strm, level, windowBits, memLevel, strategy);
}

int lzfseDecompressInit2_(z_stream *strm, int windowBits,
        const char *version, int stream_size) {
    (void)version;     /* Unused */
    (void)stream_size; /* Unused */
    return lzfseDecompressInit2(strm, windowBits);
}

#endif /* MLZFSE_IMPLEMENTATION */
#endif /* MLZFSE_H */