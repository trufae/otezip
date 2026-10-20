#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MDEFLATE_IMPLEMENTATION
#include "../../src/lib/deflate.inc.c"

/* Fixed block: six 0xFF literals (9 bits each) and end of block, exactly 64 bits.
 * The last literal and the end of block code are both decoded from bits already
 * buffered after the final input byte was loaded. */
static const uint8_t six_ff[] = { 0xfb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01 };

int main(void) {
	uint8_t out[16];
	z_stream strm = { 0 };
	if (inflateInit2 (&strm, -MAX_WBITS) != Z_OK) {
		printf ("FAIL: inflateInit2\n");
		return 1;
	}
	strm.next_in = (uint8_t *)six_ff;
	strm.avail_in = sizeof (six_ff);
	strm.next_out = out;
	strm.avail_out = sizeof (out);
	int ret = inflate (&strm, Z_FINISH);
	size_t len = strm.total_out;
	inflateEnd (&strm);
	if (ret != Z_STREAM_END || len != 6 || memcmp (out, "\xff\xff\xff\xff\xff\xff", 6)) {
		printf ("FAIL: stream ending in buffered bits (ret=%d len=%zu)\n", ret, len);
		return 1;
	}
	printf ("All deflate tail tests passed!\n");
	return 0;
}
