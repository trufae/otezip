#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MDEFLATE_IMPLEMENTATION
#include "../../src/lib/deflate.inc.c"

/* Decode in into out with at most step bytes of output space per call */
static int decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t cap, size_t step, size_t *out_len) {
	z_stream strm = { 0 };
	if (inflateInit2 (&strm, -MAX_WBITS) != Z_OK) {
		return Z_STREAM_ERROR;
	}
	strm.next_in = in;
	strm.avail_in = in_len;
	size_t pos = 0;
	int ret = Z_OK;
	for (int guard = 0; guard < 100000; guard++) {
		size_t room = cap - pos;
		if (room > step) {
			room = step;
		}
		strm.next_out = out + pos;
		strm.avail_out = room;
		ret = inflate (&strm, Z_FINISH);
		pos += room - strm.avail_out;
		if (ret != Z_OK && ret != Z_BUF_ERROR) {
			break;
		}
	}
	inflateEnd (&strm);
	*out_len = pos;
	return ret;
}

int main(void) {
	/* A 300-byte stored block followed by an empty final stored block */
	uint8_t data[300];
	for (size_t i = 0; i < sizeof (data); i++) {
		data[i] = (uint8_t) (i * 7);
	}
	uint8_t stream[5 + sizeof (data) + 5];
	size_t n = 0;
	stream[n++] = 0x00; /* BFINAL=0, BTYPE=00 */
	stream[n++] = sizeof (data) & 0xff;
	stream[n++] = sizeof (data) >> 8;
	stream[n++] = ~sizeof (data) & 0xff;
	stream[n++] = (~sizeof (data) >> 8) & 0xff;
	memcpy (stream + n, data, sizeof (data));
	n += sizeof (data);
	static const uint8_t empty_final[] = { 0x01, 0x00, 0x00, 0xff, 0xff };
	memcpy (stream + n, empty_final, sizeof (empty_final));
	n += sizeof (empty_final);

	uint8_t out[sizeof (data) + 16];
	int failed = 0;
	static const size_t steps[] = { sizeof (out), 7, 1 };
	for (size_t i = 0; i < sizeof (steps) / sizeof (steps[0]); i++) {
		size_t len = 0;
		int ret = decode (stream, n, out, sizeof (out), steps[i], &len);
		if (ret != Z_STREAM_END || len != sizeof (data) || memcmp (out, data, len)) {
			printf ("FAIL: stored block with %zu-byte output steps (ret=%d len=%zu)\n", steps[i], ret, len);
			failed = 1;
		}
	}
	if (!failed) {
		printf ("All deflate stored block tests passed!\n");
	}
	return failed;
}
