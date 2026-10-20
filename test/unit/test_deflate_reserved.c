#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MDEFLATE_IMPLEMENTATION
#include "../../src/lib/deflate.inc.c"

/* Fixed-Huffman blocks: literal 'a', length 3, distance code N, end of block */
static const uint8_t dist_code_0[] = { 0x4b, 0x04, 0x02, 0x00, 0x00, 0x00 };
static const uint8_t dist_code_30[] = { 0x4b, 0x04, 0x3e, 0x00, 0x00, 0x00 };
static const uint8_t dist_code_31[] = { 0x4b, 0x04, 0x7e, 0x00, 0x00, 0x00 };

static int run_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap, size_t *out_len) {
	z_stream strm = { 0 };
	if (inflateInit2 (&strm, -MAX_WBITS) != Z_OK) {
		return Z_STREAM_ERROR;
	}
	strm.next_in = (uint8_t *)in;
	strm.avail_in = in_len;
	strm.next_out = out;
	strm.avail_out = out_cap;
	int ret = inflate (&strm, Z_FINISH);
	*out_len = strm.total_out;
	inflateEnd (&strm);
	return ret;
}

int main(void) {
	uint8_t out[64];
	size_t out_len = 0;
	int failed = 0;

	int ret = run_inflate (dist_code_0, sizeof (dist_code_0), out, sizeof (out), &out_len);
	if (ret != Z_STREAM_END || out_len != 4 || memcmp (out, "aaaa", 4)) {
		printf ("FAIL: valid distance code 0 stream (ret=%d len=%zu)\n", ret, out_len);
		failed = 1;
	}
	ret = run_inflate (dist_code_30, sizeof (dist_code_30), out, sizeof (out), &out_len);
	if (ret != Z_DATA_ERROR) {
		printf ("FAIL: reserved distance code 30 not rejected (ret=%d)\n", ret);
		failed = 1;
	}
	ret = run_inflate (dist_code_31, sizeof (dist_code_31), out, sizeof (out), &out_len);
	if (ret != Z_DATA_ERROR) {
		printf ("FAIL: reserved distance code 31 not rejected (ret=%d)\n", ret);
		failed = 1;
	}
	if (!failed) {
		printf ("All deflate reserved code tests passed!\n");
	}
	return failed;
}
