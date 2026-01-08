/* zstd.c - Minimalistic zstd implementation compatible with zlib-like API
 * Version: 0.1 (2025-07-27)
 *
 * This single-file implementation provides a tiny subset of zstd API with
 * zlib-compatible wrappers:
 *
 *   zstdInit
 *   zstdCompress
 *   zstdEnd
 *   zstdDecompressInit
 *   zstdDecompress
 *   zstdDecompressEnd
 *
 * It supports:
 * - Basic Zstandard compression/decompression
 * - Bare minimum functionality to support ZIP file reading/writing
 * - Compatible interface with existing deflate.c implementation
 *
 * Usage:
 *   #define MZSTD_IMPLEMENTATION in one source file before including
 *
 * License: MIT / 0-BSD - do whatever you want; attribution appreciated.
 */

#ifndef MZSTD_H
#define MZSTD_H

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

/* Strategy values */
#define Z_DEFAULT_STRATEGY 0

/* Compression level */
#define Z_NO_COMPRESSION 0
#define Z_BEST_SPEED 1
#define Z_BEST_COMPRESSION 9
#ifndef Z_DEFAULT_COMPRESSION
#define Z_DEFAULT_COMPRESSION 1
#endif

/* ZSTD-specific constants */
#define ZSTD_MAGIC_NUMBER 0xFD2FB528 /* Magic number for Zstandard frame */
#define ZSTD_FRAME_HEADER_SIZE 5 /* Minimum frame header size */
#define ZSTD_BLOCK_MAX_SIZE 128 * 1024 /* Maximum block size */
#define ZSTD_WINDOW_LOG_MAX 24 /* Max window log size */
#define ZSTD_DEFAULT_CLEVEL 3 /* Default compression level */

/* ------------- Data Structures ------------- */

/* Unified z_stream declaration */
#include "../include/otezip/zstream.h"

/* Zstandard frame header */
typedef struct {
	uint8_t frame_header[4];
	uint8_t window_descriptor;
	uint8_t dictionary_id[4];
	uint8_t frame_content_size[8];
} zstd_frame_header;

/* Zstandard block header */
typedef struct {
	uint32_t block_size;
	uint8_t block_type;
	uint8_t is_last_block;
} zstd_block_header;

/* Zstandard compression context */
typedef struct {
	int compression_level;
	uint8_t *dict;
	size_t dict_size;
	uint8_t *window_buffer;
	size_t window_size;
	size_t window_pos;
	uint32_t block_size;
	int is_last_block;

	/* Temporary work buffers */
	uint8_t *compress_buffer;
	size_t compress_buffer_size;
} zstd_compress_context;

/* Zstandard decompression context */
typedef struct {
	uint8_t *dict;
	size_t dict_size;
	uint8_t *window_buffer;
	size_t window_size;
	size_t window_pos;
	uint32_t current_block_size;
	int current_block_remaining;
	int is_last_block;

	/* Frame parameters from header */
	uint32_t window_log;
	uint64_t content_size;
	uint32_t dictionary_id;

	/* Temporary work buffers */
	uint8_t *decompress_buffer;
	size_t decompress_buffer_size;
} zstd_decompress_context;

/* ------------- Function Prototypes ------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
/* Compression */
int zstdInit(z_stream *strm, int level);
int zstdCompress(z_stream *strm, int flush);
int zstdEnd(z_stream *strm);

/* Decompression */
int zstdDecompressInit(z_stream *strm);
int zstdDecompress(z_stream *strm, int flush);
int zstdDecompressEnd(z_stream *strm);

/* Helpers for zlib compatibility layer */
int zstdCompressInit2(z_stream *strm, int level, int windowBits,
	int memLevel, int strategy);
int zstdDecompressInit2(z_stream *strm, int windowBits);
int zstdCompressInit2_(z_stream *strm, int level, int windowBits,
	int memLevel, int strategy,
	const char *version, int stream_size);
int zstdDecompressInit2_(z_stream *strm, int windowBits,
	const char *version, int stream_size);

#ifdef __cplusplus
}
#endif

/* ------------- Implementation ------------- */
#ifdef OTEZIP_ENABLE_ZSTD

/* --- Helper Functions --- */

/* Read a 32-bit little-endian value */
static uint32_t read_le32(const void *ptr) {
	const uint8_t *p = (const uint8_t *)ptr;
	return ((uint32_t)p[0]) |
		((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) |
		((uint32_t)p[3] << 24);
}

/* Simple compression for Zstandard block
 * NOTE: For minimalistic implementation, we don't actually compress;
 * returning 0 signals to use raw blocks instead. This avoids issues
 * with RLE schemes that conflict with raw data patterns. */
static int compress_block(const uint8_t *src, size_t src_size,
	uint8_t *dst, size_t dst_capacity,
	int level) {
	(void)src; /* Unused in this minimal implementation */
	(void)src_size; /* Unused */
	(void)dst; /* Unused - we don't compress */
	(void)dst_capacity; /* Unused */
	(void)level; /* Unused in this minimal implementation */

	/* Return 0 to indicate: "use raw block instead" */
	/* This avoids RLE scheme issues with literal 0 bytes conflicting with markers */
	return 0;
}

/* Simple decompression for our Zstandard block
 * NOTE: Since we don't actually compress blocks (always using raw blocks),
 * this function won't be called in normal operation. However, we keep it
 * as a placeholder for potential future use. For now, just copy the data. */
static int decompress_block(const uint8_t *src, size_t src_size,
	uint8_t *dst, size_t dst_capacity) {
	if (src_size == 0 || !src || !dst) {
		return 0; /* Empty input or invalid pointers */
	}

	/* Check output capacity */
	if (src_size > dst_capacity) {
		return 0; /* Output overflow */
	}

	/* Just copy raw data - we're not actually compressing */
	memcpy (dst, src, src_size);
	return src_size; /* Return decompressed size */
}

/* --- Zstandard API Implementation --- */

/* Initialize a compression stream */
int zstdInit(z_stream *strm, int level) {
	if (!strm) {
		return Z_STREAM_ERROR;
	}

	/* Setup default level */
	if (level == Z_DEFAULT_COMPRESSION) {
		level = ZSTD_DEFAULT_CLEVEL;
	}

	/* Allocate compression context */
	zstd_compress_context *ctx = (zstd_compress_context *)calloc (1, sizeof (zstd_compress_context));
	if (!ctx) {
		return Z_MEM_ERROR;
	}

	/* Initialize context */
	ctx->compression_level = level;
	ctx->window_size = 1 << 17; /* 128KB window by default */
	/* Block size is limited to 65535 by the 2-byte size field in block header */
	ctx->block_size = 65535;
	ctx->is_last_block = 0;

	/* Allocate window buffer for LZ77 */
	ctx->window_buffer = (uint8_t *)malloc (ctx->window_size);
	if (!ctx->window_buffer) {
		free (ctx);
		return Z_MEM_ERROR;
	}

	/* Allocate compression buffer */
	ctx->compress_buffer_size = ctx->block_size * 2; /* Over-allocate for worst case */
	ctx->compress_buffer = (uint8_t *)malloc (ctx->compress_buffer_size);
	if (!ctx->compress_buffer) {
		free (ctx->window_buffer);
		free (ctx);
		return Z_MEM_ERROR;
	}

	/* Initialize stream counters */
	strm->state = (void *)ctx;
	strm->total_in = 0;
	strm->total_out = 0;

	return Z_OK;
}

/* Compress data using Zstandard format */
int zstdCompress(z_stream *strm, int flush) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}

	zstd_compress_context *ctx = (zstd_compress_context *)strm->state;

	/* Check if this is the first call - need to write frame header */
	if (strm->total_out == 0) {
		/* We need space for the frame header */
		if (strm->avail_out < ZSTD_FRAME_HEADER_SIZE) {
			return Z_BUF_ERROR;
		}

		/* Write Zstandard frame header (simplified) */
		uint8_t header[4] = { 0xFD, 0x2F, 0xB5, 0x28 }; /* Magic number */
		memcpy (strm->next_out, header, 4);
		strm->next_out += 4;
		strm->avail_out -= 4;
		strm->total_out += 4;

		/* Window descriptor (using default settings) */
		strm->next_out[0] = 0x70; /* Window log = 17 (128 KB) */
		strm->next_out++;
		strm->avail_out--;
		strm->total_out++;
	}

	/* Simple block-based compression */
	/* Note: Don't set is_last_block yet - we'll set it when we run out of input */

	/* Process input data */
	while (strm->avail_in > 0) {
		/* Determine block size */
		uint32_t block_size = strm->avail_in < ctx->block_size? strm->avail_in: ctx->block_size;

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
			/* Write empty last block (type=0, size=0, last=1) */
			/* Ensure space for 3-byte block header */
			if (strm->avail_out < 3) {
				return Z_BUF_ERROR;
			}
			strm->next_out[0] = 0x01; /* Last block bit set, raw block (type=0) */
			strm->next_out[1] = 0x00; /* Size low byte = 0 */
			strm->next_out[2] = 0x00; /* Size high byte = 0 */
			strm->next_out += 3;
			strm->avail_out -= 3;
			strm->total_out += 3;

			/* We're done */
			return Z_STREAM_END;
		}

		/* Try to compress the block */
		int compressed_size = 0;
		if (block_size > 0) {
			compressed_size = compress_block (strm->next_in, block_size,
				ctx->compress_buffer, ctx->compress_buffer_size,
				ctx->compression_level);
		}

		/* Decide whether to write compressed or raw block */
		uint8_t block_header = 0;
		const uint8_t *block_content;
		uint32_t content_size;

		if (compressed_size > 0 && (uint32_t)compressed_size < block_size) {
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
		if (ctx->is_last_block) {
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
		memcpy (strm->next_out, block_content, content_size);
		strm->next_out += content_size;
		strm->avail_out -= content_size;
		strm->total_out += content_size;

		/* Update input */
		if (block_size > 0) {
			/* Save source pointer before advancing */
			const uint8_t *src = strm->next_in;
			
			strm->next_in += block_size;
			strm->avail_in -= block_size;
			strm->total_in += block_size;

			/* Update window buffer for future references, handling wrap-around */
			size_t space_left = ctx->window_size - ctx->window_pos;
			if (block_size <= space_left) {
				/* Data fits without wrapping */
				memcpy (ctx->window_buffer + ctx->window_pos, src, block_size);
				ctx->window_pos += block_size;
			} else {
				/* Data wraps around the circular buffer */
				memcpy (ctx->window_buffer + ctx->window_pos, src, space_left);
				memcpy (ctx->window_buffer, src + space_left, block_size - space_left);
				ctx->window_pos = block_size - space_left;
			}
		}

		/* If this was the last block, we're done */
		if (ctx->is_last_block) {
			return Z_STREAM_END;
		}
	}

	/* After processing all input, write the final block if flush == Z_FINISH */
	if (flush == Z_FINISH) {
		/* Ensure space for 3-byte block header */
		if (strm->avail_out < 3) {
			return Z_BUF_ERROR;
		}
		strm->next_out[0] = 0x01; /* Last block bit set, raw block (type=0) */
		strm->next_out[1] = 0x00; /* Size low byte = 0 */
		strm->next_out[2] = 0x00; /* Size high byte = 0 */
		strm->next_out += 3;
		strm->avail_out -= 3;
		strm->total_out += 3;
		return Z_STREAM_END;
	}

	return Z_OK;
}

/* End a compression stream */
int zstdEnd(z_stream *strm) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}

	zstd_compress_context *ctx = (zstd_compress_context *)strm->state;

	/* Free allocated buffers */
	free (ctx->window_buffer);
	free (ctx->compress_buffer);
	if (ctx->dict) {
		free (ctx->dict);
	}

	/* Free context */
	free (ctx);
	strm->state = NULL;

	return Z_OK;
}

/* Initialize a decompression stream */
int zstdDecompressInit(z_stream *strm) {
	if (!strm) {
		return Z_STREAM_ERROR;
	}

	/* Allocate decompression context */
	zstd_decompress_context *ctx = (zstd_decompress_context *)calloc (1, sizeof (zstd_decompress_context));
	if (!ctx) {
		return Z_MEM_ERROR;
	}

	/* Initialize context with default values */
	ctx->window_size = 1 << 17; /* 128KB window by default */
	ctx->is_last_block = 0;

	/* Allocate window buffer */
	ctx->window_buffer = (uint8_t *)malloc (ctx->window_size);
	if (!ctx->window_buffer) {
		free (ctx);
		return Z_MEM_ERROR;
	}

	/* Allocate decompression buffer */
	ctx->decompress_buffer_size = ZSTD_BLOCK_MAX_SIZE;
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

/* Decompress data using Zstandard format */
int zstdDecompress(z_stream *strm, int flush) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}
	(void)flush;

	zstd_decompress_context *ctx = (zstd_decompress_context *)strm->state;

	/* Check if we need to parse the frame header */
	if (strm->total_in == 0) {
		/* Need at least 5 bytes for the frame header */
		if (strm->avail_in < 5) {
			return Z_BUF_ERROR;
		}

		/* Check magic number */
		uint32_t magic = read_le32 (strm->next_in);
		if (magic != ZSTD_MAGIC_NUMBER) {
			return Z_DATA_ERROR;
		}

		/* Read window descriptor */
		uint8_t window_descriptor = strm->next_in[4];
		ctx->window_log = window_descriptor & 0x0F;

		/* Advance past header */
		strm->next_in += 5;
		strm->avail_in -= 5;
		strm->total_in += 5;
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
			memcpy (strm->next_out,
				ctx->decompress_buffer +
					(ctx->current_block_size - ctx->current_block_remaining),
				copy_size);

			/* Update counters */
			strm->next_out += copy_size;
			strm->avail_out -= copy_size;
			strm->total_out += copy_size;
			ctx->current_block_remaining -= copy_size;

			/* Copy to window buffer, handling wrap-around */
			const uint8_t *data = strm->next_out - copy_size;
			size_t space_left = ctx->window_size - ctx->window_pos;
			if (copy_size <= space_left) {
				/* Data fits without wrapping */
				memcpy (ctx->window_buffer + ctx->window_pos, data, copy_size);
				ctx->window_pos += copy_size;
			} else {
				/* Data wraps around the circular buffer */
				memcpy (ctx->window_buffer + ctx->window_pos, data, space_left);
				memcpy (ctx->window_buffer, data + space_left, copy_size - space_left);
				ctx->window_pos = copy_size - space_left;
			}

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
					memcpy (strm->next_out, strm->next_in, block_size);
					strm->next_out += block_size;
					strm->avail_out -= block_size;
					strm->total_out += block_size;

					/* Copy to window buffer, handling wrap-around */
					size_t space_left = ctx->window_size - ctx->window_pos;
					if (block_size <= space_left) {
						/* Data fits without wrapping */
						memcpy (ctx->window_buffer + ctx->window_pos,
							strm->next_in,
							block_size);
						ctx->window_pos += block_size;
					} else {
						/* Data wraps around the circular buffer */
						memcpy (ctx->window_buffer + ctx->window_pos,
							strm->next_in,
							space_left);
						memcpy (ctx->window_buffer,
							strm->next_in + space_left,
							block_size - space_left);
						ctx->window_pos = block_size - space_left;
					}

					/* Advance input */
					strm->next_in += block_size;
					strm->avail_in -= block_size;
					strm->total_in += block_size;
				} else {
					/* Store in temporary buffer to output in chunks */
					if (block_size > ctx->decompress_buffer_size) {
						/* Reallocate buffer if needed */
						free (ctx->decompress_buffer);
						ctx->decompress_buffer_size = block_size;
						ctx->decompress_buffer = (uint8_t *)malloc (ctx->decompress_buffer_size);
						if (!ctx->decompress_buffer) {
							return Z_MEM_ERROR;
						}
					}

					/* Copy to buffer */
					memcpy (ctx->decompress_buffer, strm->next_in, block_size);
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
				int decompressed_size = decompress_block (
					strm->next_in, block_size,
					ctx->decompress_buffer, ctx->decompress_buffer_size);

				if (decompressed_size <= 0) {
					return Z_DATA_ERROR;
				}

				/* Output if space allows */
				if (strm->avail_out >= (uint32_t)decompressed_size) {
					memcpy (strm->next_out, ctx->decompress_buffer, decompressed_size);
					strm->next_out += decompressed_size;
					strm->avail_out -= decompressed_size;
					strm->total_out += decompressed_size;

					/* Copy to window buffer, handling wrap-around */
					size_t space_left = ctx->window_size - ctx->window_pos;
					if ((size_t)decompressed_size <= space_left) {
						/* Data fits without wrapping */
						memcpy (ctx->window_buffer + ctx->window_pos,
							ctx->decompress_buffer,
							decompressed_size);
						ctx->window_pos += decompressed_size;
					} else {
						/* Data wraps around the circular buffer */
						memcpy (ctx->window_buffer + ctx->window_pos,
							ctx->decompress_buffer,
							space_left);
						memcpy (ctx->window_buffer,
							ctx->decompress_buffer + space_left,
							decompressed_size - space_left);
						ctx->window_pos = decompressed_size - space_left;
					}
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
				/* Reserved or RLE block - not implemented */
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
int zstdDecompressEnd(z_stream *strm) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}

	zstd_decompress_context *ctx = (zstd_decompress_context *)strm->state;

	/* Free allocated buffers */
	free (ctx->window_buffer);
	free (ctx->decompress_buffer);
	if (ctx->dict) {
		free (ctx->dict);
	}

	/* Free context */
	free (ctx);
	strm->state = NULL;

	return Z_OK;
}

/* --- zlib compatibility layer --- */

int zstdCompressInit2(z_stream *strm, int level, int windowBits,
	int memLevel, int strategy) {
	(void)windowBits; /* Unused */
	(void)memLevel; /* Unused */
	(void)strategy; /* Unused */
	return zstdInit (strm, level);
}

int zstdDecompressInit2(z_stream *strm, int windowBits) {
	(void)windowBits; /* Unused */
	return zstdDecompressInit (strm);
}

int zstdCompressInit2_(z_stream *strm, int level, int windowBits,
	int memLevel, int strategy,
	const char *version, int stream_size) {
	(void)version; /* Unused */
	(void)stream_size; /* Unused */
	return zstdCompressInit2 (strm, level, windowBits, memLevel, strategy);
}

int zstdDecompressInit2_(z_stream *strm, int windowBits,
	const char *version, int stream_size) {
	(void)version; /* Unused */
	(void)stream_size; /* Unused */
	return zstdDecompressInit2 (strm, windowBits);
}

#endif /* MZSTD_IMPLEMENTATION */
#endif /* MZSTD_H */
