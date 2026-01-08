#ifndef OBROTLI_H
#define OBROTLI_H

#ifdef OTEZIP_ENABLE_BROTLI

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "otezip/zstream.h"
#include "otezip/zip.h" /* for read/write little-endian helpers */

typedef struct {
	int quality;
	int window_bits;
	uint32_t crc32;
} brotli_encoder_state;

typedef struct {
	uint32_t crc32;
} brotli_decoder_state;

static int clamp_quality(int lvl) {
	if (lvl < 0) {
		return 5;
	}
	if (lvl > 11) {
		return 11;
	}
	return lvl;
}

static uint32_t my_crc32(const uint8_t *data, size_t len, uint32_t crc) {
	extern const uint32_t crc32_table[256];
	crc = ~crc;
	for (size_t i = 0; i < len; i++) {
		crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	}
	return ~crc;
}

static size_t simple_compress(const uint8_t *input, size_t input_len,
	uint8_t *output, size_t output_len) {
	if (output_len < input_len + 8) {
		return 0;
	}
	memcpy (output, "BROT", 4);
	output[4] = 1;
	/* Store length/checksum in little-endian fixed widths to be portable
	 * across architectures (avoid writing host-endian `size_t`). */
	otezip_write_le64 (output + 5, (uint64_t)input_len);
	uint32_t checksum = my_crc32 (input, input_len, 0);
	otezip_write_le32 (output + 5 + 8, checksum);
	memcpy (output + 5 + 8 + 4, input, input_len);
	return (size_t) (5 + 8 + 4 + input_len);
}

static size_t simple_decompress(const uint8_t *input, size_t input_len,
	uint8_t *output, size_t output_len) {
	if (input_len < 5 + sizeof (size_t) + sizeof (uint32_t)) {
		return 0;
	}
	if (memcmp (input, "BROT", 4) != 0) {
		return 0;
	}
	/* Read fixed-width little-endian length and checksum */
	uint64_t stored_len = otezip_read_le64 (input + 5);
	if (stored_len > output_len) {
		return 0;
	}
	if (input_len < 5 + 8 + 4 + stored_len) {
		return 0;
	}
	uint32_t stored_crc = otezip_read_le32 (input + 5 + 8);
	const uint8_t *data = input + 5 + sizeof (size_t) + sizeof (uint32_t);
	uint32_t computed_crc = my_crc32 (data, stored_len, 0);
	if (stored_crc != computed_crc) {
		return 0;
	}
	memcpy (output, data, stored_len);
	return stored_len;
}

int brotliInit(z_stream *strm, int level) {
	if (!strm) {
		return Z_STREAM_ERROR;
	}
	brotli_encoder_state *state = (brotli_encoder_state *)malloc (sizeof (brotli_encoder_state));
	if (!state) {
		return Z_MEM_ERROR;
	}
	state->quality = clamp_quality (level);
	state->window_bits = 22;
	state->crc32 = 0;
	strm->state = (struct internal_state *)state;
	strm->total_in = 0;
	strm->total_out = 0;
	return Z_OK;
}

int brotliCompress(z_stream *strm, int flush) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}
	if (flush == Z_FINISH) {
		size_t compressed_size = simple_compress (
			strm->next_in, strm->avail_in,
			strm->next_out, strm->avail_out);
		if (compressed_size == 0) {
			return Z_BUF_ERROR;
		}
		strm->next_in += strm->avail_in;
		strm->total_in += strm->avail_in;
		strm->avail_in = 0;
		strm->next_out += compressed_size;
		strm->total_out += compressed_size;
		strm->avail_out -= compressed_size;
		return Z_STREAM_END;
	}
	size_t copy_size = strm->avail_in < strm->avail_out? strm->avail_in: strm->avail_out;
	if (copy_size > 0) {
		memcpy (strm->next_out, strm->next_in, copy_size);
		strm->next_in += copy_size;
		strm->next_out += copy_size;
		strm->avail_in -= copy_size;
		strm->avail_out -= copy_size;
		strm->total_in += copy_size;
		strm->total_out += copy_size;
	}
	return Z_OK;
}

int brotliEnd(z_stream *strm) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}
	free (strm->state);
	strm->state = NULL;
	return Z_OK;
}

int brotliDecompressInit(z_stream *strm) {
	if (!strm) {
		return Z_STREAM_ERROR;
	}
	brotli_decoder_state *state = (brotli_decoder_state *)malloc (sizeof (brotli_decoder_state));
	if (!state) {
		return Z_MEM_ERROR;
	}
	state->crc32 = 0;
	strm->state = (struct internal_state *)state;
	strm->total_in = 0;
	strm->total_out = 0;
	return Z_OK;
}

int brotliDecompress(z_stream *strm, int flush) {
	(void)flush;
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}
	size_t decompressed_size = simple_decompress (
		strm->next_in, strm->avail_in,
		strm->next_out, strm->avail_out);
	if (decompressed_size == 0) {
		if (strm->avail_in >= 5 + 8) {
			uint64_t stored_len_tmp = otezip_read_le64 (strm->next_in + 5);
			if (stored_len_tmp == 0) {
				strm->next_in += strm->avail_in;
				strm->total_in += strm->avail_in;
				strm->avail_in = 0;
				return Z_STREAM_END;
			}
		}
		return Z_DATA_ERROR;
	}
	strm->next_in += strm->avail_in;
	strm->total_in += strm->avail_in;
	strm->avail_in = 0;
	strm->next_out += decompressed_size;
	strm->total_out += decompressed_size;
	strm->avail_out -= decompressed_size;
	return Z_STREAM_END;
}

int brotliDecompressEnd(z_stream *strm) {
	if (!strm || !strm->state) {
		return Z_STREAM_ERROR;
	}
	free (strm->state);
	strm->state = NULL;
	return Z_OK;
}

int brotliCompressInit2(z_stream *strm, int level, int windowBits, int memLevel, int strategy) {
	(void)windowBits;
	(void)memLevel;
	(void)strategy;
	return brotliInit (strm, level);
}

int brotliCompressInit2_(z_stream *strm, int level, int windowBits, int memLevel, int strategy, const char *version, int stream_size) {
	(void)version;
	(void)stream_size;
	return brotliCompressInit2 (strm, level, windowBits, memLevel, strategy);
}

int brotliDecompressInit2(z_stream *strm, int windowBits) {
	(void)windowBits;
	return brotliDecompressInit (strm);
}

int brotliDecompressInit2_(z_stream *strm, int windowBits, const char *version, int stream_size) {
	(void)windowBits;
	(void)version;
	(void)stream_size;
	return brotliDecompressInit (strm);
}

#endif /* MZIP_ENABLE_BROTLI */

#endif /* MBROTLI_H */
