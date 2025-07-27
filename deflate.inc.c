/* deflate.c - Minimalistic deflate (RFC 1951) implementation compatible with zlib API
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
#define Z_FILTERED            1
#define Z_HUFFMAN_ONLY        2
#define Z_RLE                 3
#define Z_FIXED               4
#define Z_DEFAULT_STRATEGY    0

/* Compression level */
#define Z_NO_COMPRESSION      0
#define Z_BEST_SPEED          1
#define Z_BEST_COMPRESSION    9
#define Z_DEFAULT_COMPRESSION (-1)

/* Window bits */
#define MAX_WBITS 15      /* 32K window size */

/* ------------- Data Structures ------------- */

typedef struct {
    uint8_t *next_in;   /* Next input byte */
    uint32_t avail_in;  /* Number of bytes available at next_in */
    uint32_t total_in;  /* Total number of input bytes read so far */

    uint8_t *next_out;  /* Next output byte */
    uint32_t avail_out; /* Number of bytes available at next_out */
    uint32_t total_out; /* Total number of bytes output so far */

    void *state;        /* Internal state */
} z_stream;

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
    uint16_t codes[288];     /* Huffman codes */
    uint8_t lengths[288];    /* Code lengths */
    uint16_t count;          /* Number of codes */
} huffman_table;

/* Block type */
typedef enum {
    BLOCK_UNCOMPRESSED = 0,
    BLOCK_FIXED = 1,
    BLOCK_DYNAMIC = 2,
    BLOCK_INVALID = 3
} block_type;

/* Internal state for inflate */
typedef struct {
    /* Input state */
    uint32_t bit_buffer;     /* Bit buffer */
    uint32_t bits_in_buffer; /* Number of bits in buffer */
    uint8_t final_block;     /* Is this the final block? */
    
    /* Huffman tables */
    huffman_table literals;  /* Literal/length codes */
    huffman_table distances; /* Distance codes */
    
    /* Current state */
    int state;              /* Current state of processing */
    block_type btype;       /* Current block type */
    
    /* Output state */
    uint8_t *window;        /* Sliding window for LZ77 */
    uint32_t window_size;   /* Size of window */
    uint32_t window_pos;    /* Current position in window */
} inflate_state;

/* Internal state for deflate */
typedef struct {
    /* Simple state for now - we'll expand this later */
    int level;              /* Compression level */
    int is_last_block;      /* Is this the final block? */
} deflate_state;

/* ----------- Utility Functions ----------- */

/* Get a bit from the input stream */
static int get_bit(z_stream *strm, inflate_state *state) {
    if (state->bits_in_buffer == 0) {
        /* Need to load a new byte */
        if (strm->avail_in == 0) {
            return -1; /* No more input */
        }
        
        state->bit_buffer = *strm->next_in++;
        strm->avail_in--;
        strm->total_in++;
        state->bits_in_buffer = 8;
    }
    
    int bit = state->bit_buffer & 1;
    state->bit_buffer >>= 1;
    state->bits_in_buffer--;
    return bit;
}

/* Get n bits from the input stream (right-aligned) */
static int get_bits(z_stream *strm, inflate_state *state, int n) {
    int result = 0;
    
    for (int i = 0; i < n; i++) {
        int bit = get_bit(strm, state);
        if (bit < 0) {
            return -1; /* Error - not enough input */
        }
        result |= (bit << i);
    }
    
    return result;
}

/* Read an uncompressed block */
static int read_uncompressed_block(z_stream *strm, inflate_state *state) {
    /* Skip any remaining bits in current byte */
    state->bits_in_buffer = 0;
    
    /* Read length and negated length */
    if (strm->avail_in < 4) {
        return Z_DATA_ERROR; /* Not enough input */
    }
    
    uint16_t len = strm->next_in[0] | (strm->next_in[1] << 8);
    uint16_t nlen = strm->next_in[2] | (strm->next_in[3] << 8);
    
    strm->next_in += 4;
    strm->avail_in -= 4;
    strm->total_in += 4;
    
    /* Verify length */
    if (len != (uint16_t)~nlen) {
        return Z_DATA_ERROR; /* Length check failed */
    }
    
    /* Check if we have enough input */
    if (strm->avail_in < len) {
        return Z_DATA_ERROR; /* Not enough input */
    }
    
    /* Check if we have enough output space */
    if (strm->avail_out < len) {
        return Z_BUF_ERROR; /* Not enough output space */
    }
    
    /* Copy data directly to output */
    memcpy(strm->next_out, strm->next_in, len);
    strm->next_in += len;
    strm->avail_in -= len;
    strm->total_in += len;
    strm->next_out += len;
    strm->avail_out -= len;
    strm->total_out += len;
    
    /* Also store in window */
    for (uint16_t i = 0; i < len; i++) {
        state->window[state->window_pos] = strm->next_out[-(len - i)];
        state->window_pos = (state->window_pos + 1) & (state->window_size - 1);
    }
    
    return Z_OK;
}

/* Initialize fixed Huffman tables */
static void init_fixed_huffman(inflate_state *state) {
    /* Fixed tables as per deflate specification */
    /* Literal/length codes */
    for (int i = 0; i < 144; i++) {
        state->literals.codes[i] = i;
        state->literals.lengths[i] = 8;
    }
    for (int i = 144; i < 256; i++) {
        state->literals.codes[i] = i;
        state->literals.lengths[i] = 9;
    }
    for (int i = 256; i < 280; i++) {
        state->literals.codes[i] = i;
        state->literals.lengths[i] = 7;
    }
    for (int i = 280; i < 288; i++) {
        state->literals.codes[i] = i;
        state->literals.lengths[i] = 8;
    }
    state->literals.count = 288;
    
    /* Distance codes */
    for (int i = 0; i < 32; i++) {
        state->distances.codes[i] = i;
        state->distances.lengths[i] = 5;
    }
    state->distances.count = 32;
}

/* ----------- Main API Functions ----------- */

/* Compatibility wrapper for zlib */
int inflateInit2_(z_stream *strm, int windowBits, const char *version, int stream_size) {
    (void)version;  /* Unused */
    (void)stream_size;  /* Unused */
    return inflateInit2(strm, windowBits);
}

int inflateInit2(z_stream *strm, int windowBits) {
    if (!strm) return Z_STREAM_ERROR;
    
    /* Handle windowBits - negative means no header */
    int window_size = 1 << ((windowBits < 0) ? -windowBits : windowBits);
    
    /* Allocate state */
    inflate_state *state = (inflate_state *)calloc(1, sizeof(inflate_state));
    if (!state) return Z_MEM_ERROR;
    
    /* Allocate window buffer */
    state->window = (uint8_t *)malloc(window_size);
    if (!state->window) {
        free(state);
        return Z_MEM_ERROR;
    }
    
    /* Initialize state */
    state->window_size = window_size;
    state->window_pos = 0;
    state->bit_buffer = 0;
    state->bits_in_buffer = 0;
    state->final_block = 0;
    state->state = 0; /* Start at block header */
    
    strm->state = state;
    strm->total_in = 0;
    strm->total_out = 0;
    
    return Z_OK;
}

int inflate(z_stream *strm, int flush) {
    if (!strm || !strm->state) return Z_STREAM_ERROR;
    
    inflate_state *state = (inflate_state *)strm->state;
    int ret = Z_OK;
    
    /* Main decompression loop */
    while (strm->avail_in > 0 && strm->avail_out > 0) {
        /* Process based on current state */
        switch (state->state) {
            case 0: /* Block header */
                /* Read block header */
                state->final_block = get_bit(strm, state);
                if (state->final_block < 0) return Z_DATA_ERROR;
                
                /* Get block type */
                state->btype = (block_type)get_bits(strm, state, 2);
                if (state->btype < 0) return Z_DATA_ERROR;
                
                /* Move to block data state */
                state->state = 1;
                break;
                
            case 1: /* Block data */
                /* Process based on block type */
                switch (state->btype) {
                    case BLOCK_UNCOMPRESSED:
                        ret = read_uncompressed_block(strm, state);
                        if (ret != Z_OK) return ret;
                        /* Move to next block header */
                        state->state = 0;
                        break;
                        
                    case BLOCK_FIXED:
                        /* Initialize fixed Huffman tables */
                        init_fixed_huffman(state);
                        /* Go to process literals state */
                        state->state = 2;
                        break;
                        
                    case BLOCK_DYNAMIC:
                        /* Currently not implemented - would need to read code lengths */
                        return Z_DATA_ERROR;
                        
                    case BLOCK_INVALID:
                        return Z_DATA_ERROR;
                }
                break;
                
            case 2: /* Process literals/lengths */
                /* This is a simplified implementation to handle fixed Huffman codes */
                {
                    /* Get a code */
                    int code = 0;
                    int length = 0;
                    
                    /* For fixed Huffman, we know the bit patterns */
                    if (get_bit(strm, state) == 0) {
                        /* 0xxx xxxx - 8 bit code for values 0-143 */
                        code = get_bits(strm, state, 7);
                        if (code < 0) return Z_DATA_ERROR;
                        code = (0 << 7) | code; /* Add the 0 bit we already read */
                    } else {
                        int next_bit = get_bit(strm, state);
                        if (next_bit < 0) return Z_DATA_ERROR;
                        
                        if (next_bit == 0) {
                            /* 10xx xxxx - 8 bit code for values 144-255 */
                            code = get_bits(strm, state, 6);
                            if (code < 0) return Z_DATA_ERROR;
                            code = (0b10 << 6) | code; /* Add the 10 bits we already read */
                            code += 144; /* Adjust to actual value */
                        } else {
                            int third_bit = get_bit(strm, state);
                            if (third_bit < 0) return Z_DATA_ERROR;
                            
                            if (third_bit == 0) {
                                /* 110x xxxx - 7 bit code for values 256-279 */
                                code = get_bits(strm, state, 4);
                                if (code < 0) return Z_DATA_ERROR;
                                code = (0b110 << 4) | code; /* Add the 110 bits we already read */
                                code = code - (0b110 << 4) + 256; /* Adjust to actual value */
                            } else {
                                /* 111x xxxx - 8 bit code for values 280-287 */
                                code = get_bits(strm, state, 4);
                                if (code < 0) return Z_DATA_ERROR;
                                code = (0b111 << 4) | code; /* Add the 111 bits we already read */
                                code = code - (0b111 << 4) + 280; /* Adjust to actual value */
                            }
                        }
                    }
                    
                    /* Process the code */
                    if (code < 256) {
                        /* Literal byte */
                        *strm->next_out++ = (uint8_t)code;
                        strm->avail_out--;
                        strm->total_out++;
                        
                        /* Add to window */
                        state->window[state->window_pos] = (uint8_t)code;
                        state->window_pos = (state->window_pos + 1) & (state->window_size - 1);
                    } else if (code == 256) {
                        /* End of block */
                        state->state = 0; /* Back to block header */
                        if (state->final_block) {
                            return Z_STREAM_END; /* We're done */
                        }
                    } else if (code <= 285) {
                        /* Length code */
                        /* Determine base length and extra bits */
                        static const uint16_t length_base[] = {
                            3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
                            15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                            67, 83, 99, 115, 131, 163, 195, 227, 258
                        };
                        static const uint8_t length_extra[] = {
                            0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                            1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                            4, 4, 4, 4, 5, 5, 5, 5, 0
                        };
                        
                        int length_idx = code - 257;
                        int length = length_base[length_idx];
                        int extra_bits = length_extra[length_idx];
                        
                        if (extra_bits > 0) {
                            int extra = get_bits(strm, state, extra_bits);
                            if (extra < 0) return Z_DATA_ERROR;
                            length += extra;
                        }
                        
                        /* Now read distance code - for fixed Huffman this is always 5 bits */
                        int distance_code = get_bits(strm, state, 5);
                        if (distance_code < 0) return Z_DATA_ERROR;
                        
                        /* Convert to actual distance */
                        static const uint16_t dist_base[] = {
                            1, 2, 3, 4, 5, 7, 9, 13, 17, 25,
                            33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                            1025, 1537, 2049, 3073, 4097, 6145,
                            8193, 12289, 16385, 24577
                        };
                        static const uint8_t dist_extra[] = {
                            0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
                            4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
                            9, 9, 10, 10, 11, 11,
                            12, 12, 13, 13
                        };
                        
                        int distance = dist_base[distance_code];
                        int dist_extra_bits = dist_extra[distance_code];
                        
                        if (dist_extra_bits > 0) {
                            int extra = get_bits(strm, state, dist_extra_bits);
                            if (extra < 0) return Z_DATA_ERROR;
                            distance += extra;
                        }
                        
                        /* Copy bytes from window */
                        if (distance > state->window_size) {
                            return Z_DATA_ERROR; /* Distance too far back */
                        }
                        
                        /* Make sure we have enough output space */
                        if (strm->avail_out < length) {
                            return Z_BUF_ERROR;
                        }
                        
                        /* Copy the bytes */
                        for (int i = 0; i < length; i++) {
                            uint8_t byte = state->window[(state->window_pos - distance) & (state->window_size - 1)];
                            *strm->next_out++ = byte;
                            strm->avail_out--;
                            strm->total_out++;
                            
                            /* Add to window */
                            state->window[state->window_pos] = byte;
                            state->window_pos = (state->window_pos + 1) & (state->window_size - 1);
                        }
                    } else {
                        /* Invalid code */
                        return Z_DATA_ERROR;
                    }
                }
                break;
                
            default:
                return Z_STREAM_ERROR; /* Invalid state */
        }
        
        /* Check if we're done */
        if (state->final_block && state->state == 0) {
            return Z_STREAM_END;
        }
        
        /* Check if we should stop */
        if (flush == Z_FINISH && strm->avail_in == 0) {
            if (!state->final_block || state->state != 0) {
                return Z_BUF_ERROR; /* Need more input to finish */
            }
            return Z_STREAM_END;
        }
    }
    
    /* If we get here, we've run out of input or output space */
    if (strm->avail_out == 0) {
        return Z_BUF_ERROR;
    }
    
    return ret;
}

int inflateEnd(z_stream *strm) {
    if (!strm || !strm->state) return Z_STREAM_ERROR;
    
    inflate_state *state = (inflate_state *)strm->state;
    
    /* Free window buffer */
    free(state->window);
    
    /* Free state */
    free(state);
    strm->state = NULL;
    
    return Z_OK;
}

/* 
 * Simplified deflate implementation - just stores data uncompressed
 * for now. A complete implementation would use LZ77 + Huffman coding.
 */

/* Compatibility wrapper for zlib */
int deflateInit2_(z_stream *strm, int level, int method, int windowBits,
                 int memLevel, int strategy, const char *version, int stream_size) {
    (void)version;  /* Unused */
    (void)stream_size;  /* Unused */
    return deflateInit2(strm, level, method, windowBits, memLevel, strategy);
}

int deflateInit2(z_stream *strm, int level, int method, int windowBits,
                int memLevel, int strategy) {
    if (!strm) return Z_STREAM_ERROR;
    
    /* Allocate state */
    deflate_state *state = (deflate_state *)calloc(1, sizeof(deflate_state));
    if (!state) return Z_MEM_ERROR;
    
    /* Initialize state */
    state->level = level;
    state->is_last_block = 0;
    
    strm->state = state;
    strm->total_in = 0;
    strm->total_out = 0;
    
    return Z_OK;
}

int deflate(z_stream *strm, int flush) {
    if (!strm || !strm->state) return Z_STREAM_ERROR;
    
    deflate_state *state = (deflate_state *)strm->state;
    
    /* Check if this is the final block */
    if (flush == Z_FINISH) {
        state->is_last_block = 1;
    }
    
    /* Simple implementation - just store data uncompressed */
    if (strm->avail_in > 0) {
        /* Calculate block header size - 3 bits for header + padding to byte boundary */
        uint32_t header_size = 3;
        if (header_size % 8 != 0) {
            header_size = ((header_size / 8) + 1) * 8; /* Round up to next byte */
        }
        header_size = header_size / 8; /* Convert to bytes */
        
        /* Calculate block size - header + length fields + data */
        uint32_t block_size = header_size + 4 + strm->avail_in;
        
        /* Check if we have enough output space */
        if (strm->avail_out < block_size) {
            return Z_BUF_ERROR;
        }
        
        /* Write block header - type 00 (uncompressed) */
        *strm->next_out++ = state->is_last_block ? 1 : 0; /* Final block bit + type 00 */
        strm->avail_out--;
        strm->total_out++;
        
        /* Pad to byte boundary if needed */
        for (uint32_t i = 1; i < header_size; i++) {
            *strm->next_out++ = 0;
            strm->avail_out--;
            strm->total_out++;
        }
        
        /* Write length and inverted length */
        uint16_t len = strm->avail_in > 0xFFFF ? 0xFFFF : strm->avail_in;
        *strm->next_out++ = len & 0xFF;
        *strm->next_out++ = (len >> 8) & 0xFF;
        *strm->next_out++ = (~len) & 0xFF;
        *strm->next_out++ = ((~len) >> 8) & 0xFF;
        strm->avail_out -= 4;
        strm->total_out += 4;
        
        /* Write data */
        memcpy(strm->next_out, strm->next_in, len);
        strm->next_in += len;
        strm->avail_in -= len;
        strm->total_in += len;
        strm->next_out += len;
        strm->avail_out -= len;
        strm->total_out += len;
    }
    
    return flush == Z_FINISH ? Z_STREAM_END : Z_OK;
}

int deflateEnd(z_stream *strm) {
    if (!strm || !strm->state) return Z_STREAM_ERROR;
    
    /* Free state */
    free(strm->state);
    strm->state = NULL;
    
    return Z_OK;
}

#endif /* MDEFLATE_IMPLEMENTATION */
#endif /* MDEFLATE_H */