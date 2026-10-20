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

/* ------------- API Constants ------------- */

/* zlib-compatible constants and z_stream */
#include "../include/otezip/zlib.h"

/* LZMA-specific constants */
#define LZMA_MAGIC 0x5D
#define LZMA_HEADER_SIZE 13 /* LZMA properties + size field */
#define LZMA_PROPS_SIZE 5 /* LZMA properties size */

/* ------------- Data Structures ------------- */

/* Unified z_stream declaration */
#include "../include/otezip/zstream.h"

/* LZMA decompression context. The stream after the header is a sequence of
 * records: 00 <byte> <count> (run) or 01 <count> <count bytes> (literals). */
typedef struct {
	uint8_t properties[LZMA_PROPS_SIZE];
	uint64_t uncompressed_size;
	int header_done;
	/* Header bytes split across calls are staged here */
	uint8_t stage[LZMA_HEADER_SIZE];
	uint32_t stage_len;
	/* Record whose output did not fit in avail_out yet */
	int pending_kind; /* 0 none, 1 run, 2 literals */
	uint8_t pending_byte; /* Byte repeated by a pending run */
	uint32_t pending_left; /* Bytes still to output for the pending record */
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

/* Read 64-bit little endian integer */
static uint64_t read_uint64_le(const uint8_t *p) {
	uint64_t value = 0;
	for (int i = 0; i < 8; i++) {
		value |= ((uint64_t)p[i]) << (i * 8);
	}
	return value;
}

/* Output as much of the pending record as fits */
static void lzma_flush_pending(z_stream *strm, lzma_decompress_context *ctx) {
	uint32_t n = ctx->pending_left;
	if (n > strm->avail_out) {
		n = strm->avail_out;
	}
	if (ctx->pending_kind == 2 && n > strm->avail_in) {
		n = strm->avail_in;
	}
	if (ctx->pending_kind == 1) {
		memset (strm->next_out, ctx->pending_byte, n);
	} else {
		memcpy (strm->next_out, strm->next_in, n);
		strm->next_in += n;
		strm->avail_in -= n;
		strm->total_in += n;
	}
	strm->next_out += n;
	strm->avail_out -= n;
	strm->total_out += n;
	ctx->pending_left -= n;
	if (ctx->pending_left == 0) {
		ctx->pending_kind = 0;
	}
}

/* Collect a header into ctx->stage until it holds need bytes; returns 1 once it does */
static int lzma_stage(z_stream *strm, lzma_decompress_context *ctx, uint32_t need) {
	while (ctx->stage_len < need && strm->avail_in > 0) {
		ctx->stage[ctx->stage_len++] = *strm->next_in++;
		strm->avail_in--;
		strm->total_in++;
	}
	return ctx->stage_len >= need;
}

/* --- LZMA API Implementation --- */

/* Initialize a decompression stream */
int lzmaDecompressInit(z_stream *strm) {
	if (!strm) {
		return Z_STREAM_ERROR;
	}
	lzma_decompress_context *ctx = (lzma_decompress_context *)calloc (1, sizeof (lzma_decompress_context));
	if (!ctx) {
		return Z_MEM_ERROR;
	}
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

	if (!ctx->header_done) {
		if (!lzma_stage (strm, ctx, LZMA_HEADER_SIZE)) {
			return flush == Z_FINISH? Z_DATA_ERROR: Z_BUF_ERROR;
		}
		memcpy (ctx->properties, ctx->stage, LZMA_PROPS_SIZE);
		ctx->uncompressed_size = read_uint64_le (ctx->stage + LZMA_PROPS_SIZE);
		ctx->stage_len = 0;
		ctx->header_done = 1;
	}

	for (;;) {
		if (ctx->pending_kind) {
			lzma_flush_pending (strm, ctx);
			if (ctx->pending_kind) {
				if (strm->avail_out == 0) {
					return Z_OK; /* Need more output space */
				}
				/* Literals cut short by the end of the input */
				return flush == Z_FINISH? Z_DATA_ERROR: Z_BUF_ERROR;
			}
		}
		if (ctx->uncompressed_size != UINT64_MAX && strm->total_out >= ctx->uncompressed_size) {
			return Z_STREAM_END;
		}
		if (strm->avail_in == 0) {
			if (ctx->stage_len == 0 && ctx->uncompressed_size == UINT64_MAX && flush == Z_FINISH) {
				return Z_STREAM_END;
			}
			return flush == Z_FINISH? Z_DATA_ERROR: Z_BUF_ERROR;
		}
		/* Record header: 00 <byte> <count> or 01 <count> */
		if (!lzma_stage (strm, ctx, 1)) {
			return Z_BUF_ERROR;
		}
		uint8_t marker = ctx->stage[0];
		uint32_t hdr = (marker == 0x00)? 3: (marker == 0x01)? 2: 0;
		if (hdr == 0) {
			return Z_DATA_ERROR; /* Invalid marker */
		}
		if (!lzma_stage (strm, ctx, hdr)) {
			return flush == Z_FINISH? Z_DATA_ERROR: Z_BUF_ERROR;
		}
		if (marker == 0x00) {
			ctx->pending_kind = 1;
			ctx->pending_byte = ctx->stage[1];
			ctx->pending_left = ctx->stage[2];
		} else {
			ctx->pending_kind = 2;
			ctx->pending_left = ctx->stage[1];
		}
		ctx->stage_len = 0;
		if (ctx->uncompressed_size != UINT64_MAX &&
			strm->total_out + ctx->pending_left > ctx->uncompressed_size) {
			return Z_DATA_ERROR; /* More data than the header announced */
		}
		if (ctx->pending_left == 0) {
			ctx->pending_kind = 0;
		}
	}
}

/* End a decompression stream */
int lzmaDecompressEnd(z_stream *strm) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}
	free (strm->state);
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
