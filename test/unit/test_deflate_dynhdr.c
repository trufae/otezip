#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MDEFLATE_IMPLEMENTATION
#include "../../src/lib/deflate.inc.c"

/* Dynamic block with HLIT=286, HDIST=30 (the maximum valid counts) decoding to "a".
 * The trailing 0x00 is padding: the decoder stops once avail_in hits 0 even with
 * bits still buffered, which would reject this stream for an unrelated reason. */
static const uint8_t max_counts[] = {
	0xed, 0xdd, 0x21, 0x09, 0x00, 0x00, 0x00, 0x00,
	0xa0, 0xad, 0xfe, 0x3f, 0xe1, 0x12, 0x97, 0x08, 0x00
};
/* Dynamic blocks declaring too many symbols, code lengths filled with zeros.
 * HLIT=288 HDIST=32 fills exactly 318 lengths, the old ll_lengths[] size, so the
 * distance table was built from 2 bytes past the end of the array. */
static const uint8_t hlit288_hdist32[] = { 0xfd, 0x1f, 0x80, 0xe4, 0xff, 0xff, 0x07 };
static const uint8_t hlit287_hdist30[] = { 0xf5, 0x1d, 0x80, 0xe4, 0xff, 0xbf, 0x07 };
static const uint8_t hlit286_hdist31[] = { 0xed, 0x1e, 0x80, 0xe4, 0xff, 0xbf, 0x07 };

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

static int expect_reject(const char *what, const uint8_t *in, size_t in_len) {
	uint8_t out[64];
	size_t out_len = 0;
	int ret = run_inflate (in, in_len, out, sizeof (out), &out_len);
	if (ret != Z_DATA_ERROR) {
		printf ("FAIL: %s not rejected (ret=%d)\n", what, ret);
		return 1;
	}
	return 0;
}

int main(void) {
	uint8_t out[64];
	size_t out_len = 0;
	int failed = 0;

	int ret = run_inflate (max_counts, sizeof (max_counts), out, sizeof (out), &out_len);
	if (ret != Z_STREAM_END || out_len != 1 || out[0] != 'a') {
		printf ("FAIL: valid HLIT=286 HDIST=30 stream (ret=%d len=%zu)\n", ret, out_len);
		failed = 1;
	}
	failed |= expect_reject ("HLIT=288 HDIST=32", hlit288_hdist32, sizeof (hlit288_hdist32));
	failed |= expect_reject ("HLIT=287", hlit287_hdist30, sizeof (hlit287_hdist30));
	failed |= expect_reject ("HDIST=31", hlit286_hdist31, sizeof (hlit286_hdist31));
	if (!failed) {
		printf ("All deflate dynamic header tests passed!\n");
	}
	return failed;
}
