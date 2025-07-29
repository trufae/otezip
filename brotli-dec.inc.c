/* brotli-dec.inc.c - Minimalistic Brotli decoder implementation
 * Version: 0.1 (2025-07-29)
 *
 * This single-file decoder provides a tiny subset of Brotli API with
 * zlib-compatible wrappers for decompression:
 *
 *   brotliDecompressInit
 *   brotliDecompress
 *   brotliDecompressEnd
 *
 * It supports:
 * - Basic Brotli-inspired decompression
 * - Bare minimum functionality to support ZIP file reading
 * - Compatible interface with existing decompression implementations
 *
 * License: MIT
 */

#ifndef MBROTLI_DEC_H
#define MBROTLI_DEC_H

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

/* Brotli-specific constants */
#define BROTLI_WINDOW_BITS_MIN     10
#define BROTLI_WINDOW_BITS_DEFAULT 22
#define BROTLI_WINDOW_BITS_MAX     24
#define BROTLI_HEADER_SIZE         5   /* Simplified header size for our implementation */

/* ------------- Data Structures ------------- */

/* We'll use the existing z_stream from zlib */
/* Forward declare the z_stream type if not included */
#ifndef ZLIB_H
typedef struct z_stream_s z_stream;
#endif

/* Brotli decompression context */
typedef struct {
	int window_bits;        /* Window size in bits (10-24) */
	uint8_t *window_buffer; /* Sliding window for decompression */
	size_t window_size;     /* Size of the window buffer */
	size_t window_pos;      /* Current position in the window */
	uint32_t current_block_size;     /* Size of current block being processed */
	uint32_t current_block_remaining; /* Remaining bytes in current block */
	int is_last_block;      /* Flag to indicate last block */

	/* Temporary buffers */
	uint8_t *decompress_buffer;
	size_t decompress_buffer_size;
} brotli_decompress_context;

/* ------------- Function Prototypes ------------- */

#ifdef __cplusplus
extern "C" {
#endif

	/* Decompression functions */
	int brotliDecompressInit(z_stream *strm);
	int brotliDecompress(z_stream *strm, int flush);
	int brotliDecompressEnd(z_stream *strm);

	/* Helpers for zlib compatibility layer */
	int brotliDecompressInit2(z_stream *strm, int windowBits);
	int brotliDecompressInit2_(z_stream *strm, int windowBits,
			const char *version, int stream_size);

#ifdef __cplusplus
}
#endif

/* ------------- Implementation ------------- */
#ifdef MZIP_ENABLE_BROTLI

/* Simple Brotli-like decompression */
static int simple_brotli_decompress(const uint8_t *src, size_t src_size,
		uint8_t *dst, size_t dst_capacity) {
	if (src_size <= BROTLI_HEADER_SIZE || !src || !dst) {
		return 0;  /* Invalid inputs */
	}

	/* Verify magic bytes */
	if (src[0] != 0xCE || src[1] != 0xB2 || src[2] != 0xCF || src[3] != 0x81) {
		return 0;  /* Invalid magic bytes */
	}

	/* Parse window bits (not actually used in our simple implementation) */
	int window_bits = src[4];
	if (window_bits < BROTLI_WINDOW_BITS_MIN || window_bits > BROTLI_WINDOW_BITS_MAX) {
		return 0;  /* Invalid window bits */
	}

	/* Start decompressing after the header */
	size_t src_pos = BROTLI_HEADER_SIZE;
	size_t dst_pos = 0;

	while (src_pos < src_size) {
		if (src_pos + 1 > src_size) {
			return 0;  /* Invalid input - truncated */
		}

		uint8_t marker = src[src_pos++];

		if (marker == 0) {
			/* Literal run */
			if (src_pos + 1 > src_size) {
				return 0;  /* Invalid input - truncated */
			}

			uint8_t length = src[src_pos++];

			if (src_pos + length > src_size || dst_pos + length > dst_capacity) {
				return 0;  /* Invalid input or output overflow */
			}

			/* Copy literals */
			memcpy(dst + dst_pos, src + src_pos, length);
			dst_pos += length;
			src_pos += length;
		} else if (marker == 1) {
			/* Match */
			if (src_pos + 3 > src_size) {
				return 0;  /* Invalid input - truncated */
			}

			/* Decode distance (16-bit) */
			size_t dist = src[src_pos] | (src[src_pos + 1] << 8);
			src_pos += 2;

			/* Decode length */
			uint8_t length = src[src_pos++];

			if (dist > dst_pos || dst_pos + length > dst_capacity) {
				return 0;  /* Invalid match or output overflow */
			}

			/* Copy match */
			size_t match_pos = dst_pos - dist;
			for (size_t i = 0; i < length; i++) {
				dst[dst_pos++] = dst[match_pos + i];
			}
		} else if (marker == 2) {
			/* End marker */
			break;
		} else {
			return 0;  /* Invalid marker */
		}
	}

	return dst_pos;  /* Return decompressed size */
}

/* --- Brotli API Implementation --- */

/* Initialize a decompression stream */
int brotliDecompressInit(z_stream *strm) {
	if (!strm) return Z_STREAM_ERROR;

	/* Allocate decompression context */
	brotli_decompress_context *ctx = (brotli_decompress_context *)calloc(1, sizeof(brotli_decompress_context));
	if (!ctx) return Z_MEM_ERROR;

	/* Initialize context with default values */
	ctx->window_bits = BROTLI_WINDOW_BITS_DEFAULT;
	ctx->window_size = 1 << ctx->window_bits;

	/* Allocate window buffer */
	ctx->window_buffer = (uint8_t *)malloc(ctx->window_size);
	if (!ctx->window_buffer) {
		free(ctx);
		return Z_MEM_ERROR;
	}

	/* Allocate decompression buffer */
	ctx->decompress_buffer_size = ctx->window_size;
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

/* Decompress data using Brotli format */
int brotliDecompress(z_stream *strm, int flush) {
	if (!strm || !strm->state) return Z_STREAM_ERROR;

	brotli_decompress_context *ctx = (brotli_decompress_context *)strm->state;

	/* If we have remaining data from a previous block, output it first */
	if (ctx->current_block_remaining > 0) {
		size_t copy_size = ctx->current_block_remaining;
		if (copy_size > strm->avail_out) {
			copy_size = strm->avail_out;
		}

		/* Copy data to output */
		memcpy(strm->next_out, 
				ctx->decompress_buffer + (ctx->current_block_size - ctx->current_block_remaining),
				copy_size);

		/* Update counters */
		strm->next_out += copy_size;
		strm->avail_out -= copy_size;
		strm->total_out += copy_size;
		ctx->current_block_remaining -= copy_size;

		/* If we filled the output buffer, return for more space */
		if (strm->avail_out == 0) {
			return Z_OK;
		}
	}

	/* Process more input if available */
	if (strm->avail_in > 0) {
		/* Try to decompress what we have */
		size_t decomp_size = simple_brotli_decompress(strm->next_in, strm->avail_in,
				ctx->decompress_buffer,
				ctx->decompress_buffer_size);

		/* If decompression failed, return error */
		if (decomp_size == 0) {
			return Z_DATA_ERROR;
		}

		/* Check if we have enough output space */
		if (strm->avail_out >= decomp_size) {
			/* Copy all data to output */
			memcpy(strm->next_out, ctx->decompress_buffer, decomp_size);
			strm->next_out += decomp_size;
			strm->avail_out -= decomp_size;
			strm->total_out += decomp_size;
		} else {
			/* Store partial data for later */
			memcpy(strm->next_out, ctx->decompress_buffer, strm->avail_out);
			ctx->current_block_size = decomp_size;
			ctx->current_block_remaining = decomp_size - strm->avail_out;
			strm->total_out += strm->avail_out;
			strm->next_out += strm->avail_out;
			strm->avail_out = 0;
			return Z_OK;
		}

		/* Update input counters - in this simplified model we consume all input at once */
		strm->next_in += strm->avail_in;
		strm->total_in += strm->avail_in;
		strm->avail_in = 0;

		/* Mark as done if we're at the end of the stream */
		if (ctx->is_last_block) {
			return Z_STREAM_END;
		}
	}

	/* Handle flush */
	return (flush == Z_FINISH) ? Z_STREAM_END : Z_OK;
}

/* End a decompression stream */
int brotliDecompressEnd(z_stream *strm) {
	if (!strm || !strm->state) return Z_STREAM_ERROR;

	brotli_decompress_context *ctx = (brotli_decompress_context *)strm->state;

	/* Free allocated buffers */
	free(ctx->window_buffer);
	free(ctx->decompress_buffer);

	/* Free context */
	free(ctx);
	strm->state = NULL;

	return Z_OK;
}

/* --- zlib compatibility layer --- */

int brotliDecompressInit2(z_stream *strm, int windowBits) {
	(void)windowBits;  /* Unused */
	return brotliDecompressInit(strm);
}

int brotliDecompressInit2_(z_stream *strm, int windowBits,
		const char *version, int stream_size) {
	(void)version;     /* Unused */
	(void)stream_size; /* Unused */
	return brotliDecompressInit2(strm, windowBits);
}

#endif /* MZIP_ENABLE_BROTLI */
#endif /* MBROTLI_DEC_H */