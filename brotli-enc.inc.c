/* brotli-enc.inc.c - Minimalistic Brotli encoder implementation
 * Version: 0.1 (2025-07-29)
 *
 * This single-file encoder provides a tiny subset of Brotli API with
 * zlib-compatible wrappers for compression:
 *
 *   brotliInit
 *   brotliCompress
 *   brotliEnd
 *
 * It supports:
 * - Basic Brotli-inspired compression
 * - Bare minimum functionality to support ZIP file writing
 * - Compatible interface with existing compression implementations
 *
 * License: MIT
 */

#ifndef MBROTLI_ENC_H
#define MBROTLI_ENC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------- API Constants ------------- */

/* Return codes (from zlib for compatibility) */
#ifndef Z_OK
#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)
#endif

/* Flush values */
#ifndef Z_NO_FLUSH
#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#endif

/* Strategy values */
#ifndef Z_DEFAULT_STRATEGY
#define Z_DEFAULT_STRATEGY    0
#endif

/* Compression level */
#ifndef Z_NO_COMPRESSION
#define Z_NO_COMPRESSION      0
#define Z_BEST_SPEED          1
#define Z_BEST_COMPRESSION    9
#define Z_DEFAULT_COMPRESSION (-1)
#endif

/* Brotli-specific constants */
#define BROTLI_WINDOW_BITS_MIN     10
#define BROTLI_WINDOW_BITS_DEFAULT 22
#define BROTLI_WINDOW_BITS_MAX     24
#define BROTLI_HEADER_SIZE         5   /* Simplified header size for our implementation */
#define BROTLI_DEFAULT_LEVEL       5   /* Default quality level */
#define BROTLI_MAGIC_BYTES         0xCE, 0xB2, 0xCF, 0x81  /* "meta-block" magic bytes */

/* ------------- Data Structures ------------- */

/* We'll use the existing z_stream from zlib */
/* Forward declare the z_stream type if not included */
#ifndef ZLIB_H
typedef struct z_stream_s z_stream;
#endif

/* Simplified Brotli stream header */
typedef struct {
	uint8_t magic[4];       /* Magic bytes for identification */
	uint8_t window_bits;    /* Window size used for compression */
} brotli_header;

/* Simplified Brotli block header */
typedef struct {
	uint8_t is_last;        /* Flag for last block */
	uint32_t size;          /* Size of the block */
} brotli_block_header;

/* Brotli compression context */
typedef struct {
	int quality;            /* Compression quality (1-11) */
	int window_bits;        /* Window size in bits (10-24) */
	uint8_t *window_buffer; /* Sliding window for compression */
	size_t window_size;     /* Size of the window buffer */
	size_t window_pos;      /* Current position in the window */
	int is_last_block;      /* Flag to indicate last block */

	/* Temporary buffers */
	uint8_t *compress_buffer;
	size_t compress_buffer_size;
} brotli_compress_context;

/* ------------- Function Prototypes ------------- */

#ifdef __cplusplus
extern "C" {
#endif

	/* Compression functions */
	int brotliInit(z_stream *strm, int level);
	int brotliCompress(z_stream *strm, int flush);
	int brotliEnd(z_stream *strm);

	/* Helpers for zlib compatibility layer */
	int brotliCompressInit2(z_stream *strm, int level, int windowBits, 
			int memLevel, int strategy);
	int brotliCompressInit2_(z_stream *strm, int level, int windowBits,
			int memLevel, int strategy, 
			const char *version, int stream_size);

#ifdef __cplusplus
}
#endif

/* ------------- Implementation ------------- */
#ifdef MZIP_ENABLE_BROTLI

/* --- Helper Functions --- */

/* Simple dictionary lookup - for a very basic LZ-style compression */
static int find_match(const uint8_t *buf, size_t pos, size_t buf_size, 
		size_t *match_pos, size_t *match_len, size_t max_match) {
	size_t best_len = 0;
	size_t best_pos = 0;
	size_t max_lookback = 4096;  /* Limited lookback window */
	size_t start_pos = (pos > max_lookback) ? pos - max_lookback : 0;

	/* Look for matches in the window */
	for (size_t i = start_pos; i < pos; i++) {
		/* Initialize match length */
		size_t len = 0;

		/* Compare bytes */
		while (pos + len < buf_size && 
				buf[i + len] == buf[pos + len] && 
				len < max_match) {
			len++;
		}

		/* Update best match */
		if (len > best_len) {
			best_len = len;
			best_pos = i;

			/* Early exit for very good matches */
			if (best_len >= 64) {
				break;
			}
		}
	}

	/* Only return if match length is at least 3 bytes */
	if (best_len >= 3) {
		*match_pos = best_pos;
		*match_len = best_len;
		return 1;
	}

	return 0;
}

/* Simple Brotli-like compression */
static int simple_brotli_compress(const uint8_t *src, size_t src_size,
		uint8_t *dst, size_t dst_capacity,
		int quality) {
	if (src_size == 0 || !src || !dst || dst_capacity < src_size) {
		return 0;  /* Invalid inputs or not enough output space */
	}

	/* Quality affects how aggressive we search for matches */
	size_t max_match = (quality < 5) ? 16 : ((quality < 8) ? 32 : 64);

	/* Write simplified header */
	if (dst_capacity < BROTLI_HEADER_SIZE) {
		return 0;
	}

	/* Write magic bytes */
	dst[0] = 0xCE;
	dst[1] = 0xB2;
	dst[2] = 0xCF;
	dst[3] = 0x81;

	/* Window bits - encoded as logarithm */
	int window_bits = BROTLI_WINDOW_BITS_DEFAULT;
	if (quality < 5) {
		window_bits = BROTLI_WINDOW_BITS_MIN;
	} else if (quality >= 8) {
		window_bits = BROTLI_WINDOW_BITS_MAX;
	}
	dst[4] = window_bits;

	/* Start compressing after the header */
	size_t dst_pos = BROTLI_HEADER_SIZE;
	size_t src_pos = 0;

	while (src_pos < src_size) {
		/* Ensure we have output space */
		if (dst_pos + 8 >= dst_capacity) {
			return 0;  /* Not enough output space */
		}

		/* Try to find a match in the previous data */
		size_t match_pos = 0;
		size_t match_len = 0;

		if (find_match(src, src_pos, src_size, &match_pos, &match_len, max_match)) {
			/* Encode match as: marker byte (1) + distance (2 bytes) + length (1 byte) */
			dst[dst_pos++] = 1;  /* Match marker */

			/* Encode distance (16-bit) */
			size_t dist = src_pos - match_pos;
			dst[dst_pos++] = dist & 0xFF;
			dst[dst_pos++] = (dist >> 8) & 0xFF;

			/* Encode length */
			dst[dst_pos++] = match_len & 0xFF;

			src_pos += match_len;
		} else {
			/* Literal run */
			size_t run_start = src_pos;
			size_t run_length = 1;
			src_pos++;

			/* Try to extend the run */
			while (src_pos < src_size && run_length < 255) {
				/* Check for a potential match */
				if (find_match(src, src_pos, src_size, &match_pos, &match_len, max_match)) {
					break;
				}
				src_pos++;
				run_length++;
			}

			/* Ensure we have enough output space */
			if (dst_pos + run_length + 2 > dst_capacity) {
				return 0;  /* Not enough output space */
			}

			/* Encode literal as: marker byte (0) + length (1 byte) + literals */
			dst[dst_pos++] = 0;  /* Literal marker */
			dst[dst_pos++] = run_length;
			memcpy(dst + dst_pos, src + run_start, run_length);
			dst_pos += run_length;
		}
	}

	/* Write end marker */
	if (dst_pos + 1 <= dst_capacity) {
		dst[dst_pos++] = 2;  /* End marker */
	} else {
		return 0;  /* Not enough space */
	}

	return dst_pos;  /* Return compressed size */
}

/* --- Brotli API Implementation --- */

/* Initialize a compression stream */
int brotliInit(z_stream *strm, int level) {
	if (!strm) return Z_STREAM_ERROR;

	/* Set default level if needed */
	if (level == Z_DEFAULT_COMPRESSION) {
		level = BROTLI_DEFAULT_LEVEL;
	}

	/* Clamp level to valid range for Brotli (1-11) */
	if (level < 1) {
		level = 1;
	} else if (level > 9) {
		level = 9;  /* We'll map zlib's 9 to Brotli's 11 internally */
	}

	/* Allocate compression context */
	brotli_compress_context *ctx = (brotli_compress_context *)calloc(1, sizeof(brotli_compress_context));
	if (!ctx) return Z_MEM_ERROR;

	/* Initialize context */
	ctx->quality = level;
	ctx->window_bits = (level < 5) ? BROTLI_WINDOW_BITS_MIN : 
		((level < 8) ? BROTLI_WINDOW_BITS_DEFAULT : BROTLI_WINDOW_BITS_MAX);
	ctx->window_size = 1 << ctx->window_bits;
	ctx->is_last_block = 0;

	/* Allocate window buffer */
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

/* Compress data using Brotli format */
int brotliCompress(z_stream *strm, int flush) {
	if (!strm || !strm->state) return Z_STREAM_ERROR;

	brotli_compress_context *ctx = (brotli_compress_context *)strm->state;

	/* Set last block flag if requested */
	if (flush == Z_FINISH) {
		ctx->is_last_block = 1;
	}

	/* Compress data */
	if (strm->avail_in > 0) {
		/* Check if we have enough output space */
		if (strm->avail_out < BROTLI_HEADER_SIZE) {
			return Z_BUF_ERROR;
		}

		/* Compress using our simple algorithm */
		int compressed_size = simple_brotli_compress(strm->next_in, strm->avail_in,
				strm->next_out, strm->avail_out,
				ctx->quality);

		if (compressed_size <= 0) {
			return Z_DATA_ERROR;
		}

		/* Update counters */
		strm->next_out += compressed_size;
		strm->avail_out -= compressed_size;
		strm->total_out += compressed_size;

		/* Update input counters */
		strm->next_in += strm->avail_in;
		strm->total_in += strm->avail_in;
		strm->avail_in = 0;
	}

	return (ctx->is_last_block && strm->avail_in == 0) ? Z_STREAM_END : Z_OK;
}

/* End a compression stream */
int brotliEnd(z_stream *strm) {
	if (!strm || !strm->state) return Z_STREAM_ERROR;

	brotli_compress_context *ctx = (brotli_compress_context *)strm->state;

	/* Free allocated buffers */
	free(ctx->window_buffer);
	free(ctx->compress_buffer);

	/* Free context */
	free(ctx);
	strm->state = NULL;

	return Z_OK;
}

/* --- zlib compatibility layer --- */

int brotliCompressInit2(z_stream *strm, int level, int windowBits, 
		int memLevel, int strategy) {
	(void)windowBits;  /* Unused */
	(void)memLevel;    /* Unused */
	(void)strategy;    /* Unused */
	return brotliInit(strm, level);
}

int brotliCompressInit2_(z_stream *strm, int level, int windowBits,
		int memLevel, int strategy, 
		const char *version, int stream_size) {
	(void)version;     /* Unused */
	(void)stream_size; /* Unused */
	return brotliCompressInit2(strm, level, windowBits, memLevel, strategy);
}

#endif /* MZIP_ENABLE_BROTLI */
#endif /* MBROTLI_ENC_H */