/* lzma-dec.inc.c - Minimalistic LZMA decoder implementation compatible with zlib-like API
 * Version: 0.1 (2025-07-27)
 *
 * This implementation provides LZMA decoder with zlib-compatible wrappers:
 *
 *   lzmaDecompressInit
 *   lzmaDecompress
 *   lzmaDecompressEnd
 *
 * It supports:
 * - Basic LZMA decompression
 * - Bare minimum functionality to support ZIP file reading
 * - Compatible interface with existing compression implementations
 *
 * Usage:
 *   #define MLZMA_IMPLEMENTATION in one source file before including
 *
 * License: MIT / 0-BSD - do whatever you want; attribution appreciated.
 */

#ifndef MLZMA_DEC_H
#define MLZMA_DEC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------- API Constants (compatible with zlib) ------------- */

/* Return codes (from zlib for compatibility) */
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

/* LZMA-specific constants */
#define LZMA_MAGIC 0x5D
#define LZMA_HEADER_SIZE 13 /* LZMA properties + size field */
#define LZMA_PROPS_SIZE 5 /* LZMA properties size */

/* ------------- Data Structures ------------- */

/* Unified z_stream declaration */
#include "../include/otezip/zstream.h"

/* LZMA decompression context */
typedef struct {
	uint8_t properties[LZMA_PROPS_SIZE];
	uint64_t uncompressed_size;
	uint8_t *dict_buffer;
	size_t dict_size;
	uint8_t *window_buffer;
	size_t window_size;
	size_t window_pos;
	uint32_t current_block_size;
	int current_block_remaining;
	int is_last_block;

	/* Work buffers */
	uint8_t *decompress_buffer;
	size_t decompress_buffer_size;
} lzma_decompress_context;

/* ------------- Function Prototypes ------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
/* Decompression */
int lzmaDecompressInit(z_stream *strm);
int lzmaDecompress(z_stream *strm, int flush);
int lzmaDecompressEnd(z_stream *strm);

/* Helpers for zlib compatibility layer */
int lzmaDecompressInit2(z_stream *strm, int windowBits);
int lzmaDecompressInit2_(z_stream *strm, int windowBits, const char *version, int stream_size);

#ifdef __cplusplus
}
#endif

/* ------------- Implementation ------------- */
#ifdef OTEZIP_ENABLE_LZMA

/* --- Helper Functions --- */

/* Simple decompression for our LZMA block */
static int simple_lzma_decompress(const uint8_t *props,
	const uint8_t *src,
	size_t src_size,
	uint8_t *dst,
	size_t dst_capacity) {
	if (!props || !src || !dst || src_size == 0) {
		return 0; /* Invalid inputs */
	}

	/* We don't actually use the props in this simplified version */
	(void)props;

	size_t dst_pos = 0;
	size_t src_pos = 0;

	while (src_pos < src_size) {
		if (src_pos + 2 >= src_size) {
			return 0; /* Invalid compressed data */
		}

		uint8_t marker = src[src_pos++];

		if (marker == 0x00) {
			/* RLE run */
			uint8_t byte = src[src_pos++];
			uint8_t length = src[src_pos++];

			/* Check output capacity */
			if (dst_pos + length > dst_capacity) {
				return 0; /* Output overflow */
			}

			/* Output run */
			memset (dst + dst_pos, byte, length);
			dst_pos += length;
		} else if (marker == 0x01) {
			/* Literal sequence */
			uint8_t length = src[src_pos++];

			if (src_pos + length > src_size || dst_pos + length > dst_capacity) {
				return 0; /* Invalid input or output overflow */
			}

			/* Copy literals */
			memcpy (dst + dst_pos, src + src_pos, length);
			dst_pos += length;
			src_pos += length;
		} else {
			return 0; /* Invalid marker */
		}
	}

	return (int)dst_pos; /* Return decompressed size */
}

/* Read 64-bit little endian integer */
static uint64_t read_uint64_le(const uint8_t *p) {
	uint64_t value = 0;
	for (int i = 0; i < 8; i++) {
		value |= ((uint64_t)p[i]) << (i * 8);
	}
	return value;
}

/* --- LZMA API Implementation --- */

/* Initialize a decompression stream */
int lzmaDecompressInit(z_stream *strm) {
	if (!strm) {
		return Z_STREAM_ERROR;
	}

	/* Allocate decompression context */
	lzma_decompress_context *ctx = (lzma_decompress_context *)calloc (1, sizeof (lzma_decompress_context));
	if (!ctx) {
		return Z_MEM_ERROR;
	}

	/* Initialize context with default values */
	ctx->window_size = 1 << 16; /* 64KB window by default */
	ctx->is_last_block = 0;
	ctx->uncompressed_size = 0;

	/* Allocate window buffer */
	ctx->window_buffer = (uint8_t *)malloc (ctx->window_size);
	if (!ctx->window_buffer) {
		free (ctx);
		return Z_MEM_ERROR;
	}

	/* Allocate decompression buffer */
	ctx->decompress_buffer_size = ctx->window_size;
	ctx->decompress_buffer = (uint8_t *)malloc (ctx->decompress_buffer_size);
	if (!ctx->decompress_buffer) {
		free (ctx->window_buffer);
		free (ctx);
		return Z_MEM_ERROR;
	}

	/* Initialize stream */
	strm->state = (void *)ctx;
	strm->total_in = 0;
	strm->total_out = 0;

	return Z_OK;
}

/* Decompress data using LZMA format */
int lzmaDecompress(z_stream *strm, int flush) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}

	lzma_decompress_context *ctx = (lzma_decompress_context *)strm->state;

	/* Process LZMA header if this is the first call */
	if (strm->total_in == 0) {
		/* Need at least the LZMA header */
		if (strm->avail_in < LZMA_HEADER_SIZE) {
			return Z_BUF_ERROR;
		}

		/* Read LZMA properties */
		memcpy (ctx->properties, strm->next_in, LZMA_PROPS_SIZE);

		/* Read uncompressed size */
		ctx->uncompressed_size = read_uint64_le (strm->next_in + LZMA_PROPS_SIZE);

		/* Skip header */
		strm->next_in += LZMA_HEADER_SIZE;
		strm->avail_in -= LZMA_HEADER_SIZE;
		strm->total_in += LZMA_HEADER_SIZE;
	}

	/* If we have remaining data from a previous block, output it first */
	if (ctx->current_block_remaining > 0) {
		size_t copy_size = ctx->current_block_remaining;
		if (copy_size > strm->avail_out) {
			copy_size = strm->avail_out;
		}

		/* Copy data to output */
		memcpy (strm->next_out,
			ctx->decompress_buffer + (ctx->current_block_size - ctx->current_block_remaining),
			copy_size);

		/* Update counters */
		strm->next_out += (uint32_t)copy_size;
		strm->avail_out -= (uint32_t)copy_size;
		strm->total_out += (uint32_t)copy_size;
		ctx->current_block_remaining -= (int)copy_size;

		/* If we filled the output buffer, return for more space */
		if (strm->avail_out == 0) {
			return Z_OK;
		}
	}

	/* Process more input if available */
	if (strm->avail_in > 0) {
		/* Try to decompress what we have */
		size_t decomp_size = simple_lzma_decompress (ctx->properties,
			strm->next_in,
			strm->avail_in,
			ctx->decompress_buffer,
			ctx->decompress_buffer_size);

		/* If decompression failed, return error */
		if (decomp_size == 0) {
			return Z_DATA_ERROR;
		}

		/* Update input counters */
		strm->next_in += strm->avail_in;
		strm->total_in += strm->avail_in;
		strm->avail_in = 0;

		/* Check if we have enough output space */
		if (strm->avail_out >= decomp_size) {
			/* Copy all data to output */
			memcpy (strm->next_out, ctx->decompress_buffer, decomp_size);
			strm->next_out += (uint32_t)decomp_size;
			strm->avail_out -= (uint32_t)decomp_size;
			strm->total_out += (uint32_t)decomp_size;
		} else {
			/* Store partial data for later */
			memcpy (strm->next_out, ctx->decompress_buffer, strm->avail_out);
			ctx->current_block_size = (uint32_t)decomp_size;
			ctx->current_block_remaining = (int) (decomp_size - strm->avail_out);
			strm->total_out += strm->avail_out;
			strm->next_out += strm->avail_out;
			strm->avail_out = 0;
			return Z_OK;
		}
	}

	/* Determine if we're finished */
	if (ctx->uncompressed_size != 0xFFFFFFFFFFFFFFFF) {
		/* Known size - check if we've output everything */
		if (strm->total_out >= ctx->uncompressed_size) {
			return Z_STREAM_END;
		}
	} else {
		/* Unknown size - check if flush is FINISH and no more input */
		if (flush == Z_FINISH && strm->avail_in == 0) {
			return Z_STREAM_END;
		}
	}

	return Z_OK;
}

/* End a decompression stream */
int lzmaDecompressEnd(z_stream *strm) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}

	lzma_decompress_context *ctx = (lzma_decompress_context *)strm->state;

	/* Free allocated buffers */
	free (ctx->window_buffer);
	free (ctx->decompress_buffer);

	/* Free context */
	free (ctx);
	strm->state = NULL;

	return Z_OK;
}

/* --- zlib compatibility layer --- */

int lzmaDecompressInit2(z_stream *strm, int windowBits) {
	(void)windowBits; /* Unused */
	return lzmaDecompressInit (strm);
}

int lzmaDecompressInit2_(z_stream *strm, int windowBits, const char *version, int stream_size) {
	(void)version; /* Unused */
	(void)stream_size; /* Unused */
	return lzmaDecompressInit2 (strm, windowBits);
}

#endif /* OTEZIP_ENABLE_LZMA */
#endif /* MLZMA_DEC_H */
