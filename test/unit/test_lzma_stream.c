#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define OTEZIP_ENABLE_LZMA
#include "../../src/lib/lzma.inc.c"

#define DATA_SIZE (300 * 1024) /* Several 64 KiB encoder chunks */

static uint8_t data[DATA_SIZE];
static uint8_t comp[DATA_SIZE * 2];
static uint8_t out[DATA_SIZE + 16];

static size_t compress_data(void) {
	z_stream strm = { 0 };
	if (lzmaInit (&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
		return 0;
	}
	strm.next_in = data;
	strm.avail_in = DATA_SIZE;
	strm.next_out = comp;
	strm.avail_out = sizeof (comp);
	int ret = lzmaCompress (&strm, Z_FINISH);
	size_t len = strm.total_out;
	lzmaEnd (&strm);
	return ret == Z_STREAM_END? len: 0;
}

/* Decompress feeding at most in_step input and out_step output bytes per call */
static int decompress(const uint8_t *in, size_t in_len, size_t in_step, size_t out_step, size_t *out_len) {
	z_stream strm = { 0 };
	if (lzmaDecompressInit (&strm) != Z_OK) {
		return Z_MEM_ERROR;
	}
	size_t in_pos = 0;
	size_t out_pos = 0;
	int ret = Z_OK;
	for (int guard = 0; guard < 10000000; guard++) {
		size_t in_now = in_len - in_pos;
		if (in_now > in_step) {
			in_now = in_step;
		}
		size_t out_now = sizeof (out) - out_pos;
		if (out_now > out_step) {
			out_now = out_step;
		}
		strm.next_in = (uint8_t *)in + in_pos;
		strm.avail_in = in_now;
		strm.next_out = out + out_pos;
		strm.avail_out = out_now;
		int last = in_pos + in_now == in_len;
		ret = lzmaDecompress (&strm, last? Z_FINISH: Z_NO_FLUSH);
		in_pos += in_now - strm.avail_in;
		out_pos += out_now - strm.avail_out;
		if (ret == Z_STREAM_END || ret == Z_DATA_ERROR) {
			break;
		}
		if (ret == Z_BUF_ERROR && last) {
			break;
		}
	}
	lzmaDecompressEnd (&strm);
	*out_len = out_pos;
	return ret;
}

static int check_roundtrip(const char *what, size_t comp_len, size_t in_step, size_t out_step) {
	size_t out_len = 0;
	int ret = decompress (comp, comp_len, in_step, out_step, &out_len);
	if (ret != Z_STREAM_END || out_len != DATA_SIZE || memcmp (out, data, DATA_SIZE)) {
		printf ("FAIL: %s (ret=%d len=%zu)\n", what, ret, out_len);
		return 1;
	}
	return 0;
}

int main(void) {
	/* Runs and literals, so both record types cross chunk and call boundaries */
	srand (1);
	for (size_t i = 0; i < DATA_SIZE;) {
		size_t n = 1 + rand () % 600;
		uint8_t b = rand () & 0xff;
		for (size_t j = 0; j < n && i < DATA_SIZE; j++, i++) {
			data[i] = (rand () % 4)? b: (uint8_t)rand ();
		}
	}
	size_t comp_len = compress_data ();
	if (comp_len == 0) {
		printf ("FAIL: lzmaCompress\n");
		return 1;
	}

	int failed = 0;
	failed |= check_roundtrip ("single call", comp_len, comp_len, sizeof (out));
	failed |= check_roundtrip ("7-byte output steps", comp_len, comp_len, 7);
	failed |= check_roundtrip ("5-byte input steps", comp_len, 5, sizeof (out));
	failed |= check_roundtrip ("1-byte input and output steps", comp_len, 1, 1);

	size_t out_len = 0;
	int ret = decompress (comp, comp_len - 1, comp_len, sizeof (out), &out_len);
	if (ret != Z_DATA_ERROR) {
		printf ("FAIL: truncated stream not rejected (ret=%d)\n", ret);
		failed = 1;
	}
	uint8_t bad[LZMA_HEADER_SIZE + 3] = { 0x5d, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0x07, 0, 0 };
	ret = decompress (bad, sizeof (bad), sizeof (bad), sizeof (out), &out_len);
	if (ret != Z_DATA_ERROR) {
		printf ("FAIL: invalid record marker not rejected (ret=%d)\n", ret);
		failed = 1;
	}
	/* Header announces 3 bytes, run record produces 9 */
	uint8_t big[LZMA_HEADER_SIZE + 3] = { 0x5d, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0x00, 'x', 9 };
	ret = decompress (big, sizeof (big), sizeof (big), sizeof (out), &out_len);
	if (ret != Z_DATA_ERROR) {
		printf ("FAIL: output beyond announced size not rejected (ret=%d)\n", ret);
		failed = 1;
	}

	if (!failed) {
		printf ("All LZMA streaming tests passed!\n");
	}
	return failed;
}
