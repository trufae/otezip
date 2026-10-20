#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MDEFLATE_IMPLEMENTATION
#include "../../src/lib/deflate.inc.c"

/* Fixed block whose first symbol is a length 258 match at distance 32768 */
static const uint8_t far_32768[] = { 0x1b, 0xbd, 0xff, 0x1f, 0x00 };
/* Literal 'a', then a length 3 match at distance 2 with only 1 byte of history */
static const uint8_t far_by_one[] = { 0x4b, 0x04, 0x42, 0x00 };
/* Literal 'a', then a length 3 match at distance 1: "aaaa" */
static const uint8_t ok_dist_1[] = { 0x4b, 0x04, 0x02, 0x00 };
/* Stored block "ab", then a fixed block with a length 3 match at distance 2: "ababa" */
static const uint8_t stored_hist[] = { 0x00, 0x02, 0x00, 0xfd, 0xff, 0x61, 0x62, 0x03, 0x42, 0x00 };

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
	uint8_t out[512];
	size_t out_len = 0;
	int ret = run_inflate (in, in_len, out, sizeof (out), &out_len);
	if (ret != Z_DATA_ERROR) {
		printf ("FAIL: %s not rejected (ret=%d len=%zu)\n", what, ret, out_len);
		return 1;
	}
	return 0;
}

static int expect_output(const char *what, const uint8_t *in, size_t in_len, const char *want) {
	uint8_t out[512];
	size_t out_len = 0;
	int ret = run_inflate (in, in_len, out, sizeof (out), &out_len);
	if (ret != Z_STREAM_END || out_len != strlen (want) || memcmp (out, want, out_len)) {
		printf ("FAIL: %s (ret=%d len=%zu)\n", what, ret, out_len);
		return 1;
	}
	return 0;
}

int main(void) {
	int failed = 0;
	failed |= expect_reject ("distance 32768 with no history", far_32768, sizeof (far_32768));
	failed |= expect_reject ("distance 2 with 1 byte of history", far_by_one, sizeof (far_by_one));
	failed |= expect_output ("distance 1 after one literal", ok_dist_1, sizeof (ok_dist_1), "aaaa");
	failed |= expect_output ("distance 2 into stored block history", stored_hist, sizeof (stored_hist), "ababa");
	if (!failed) {
		printf ("All deflate distance tests passed!\n");
	}
	return failed;
}
