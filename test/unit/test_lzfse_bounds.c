#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../../src/include/otezip/zlib.h"
#include "../../src/lib/lzfse.inc.c"

int main(void) {
	uint8_t out[16];
	int failed = 0;

	/* Raw block whose length 0xfffffffb wraps "len + 5" to 0 in 32 bits */
	static const uint8_t wrap[] = { 0x06, 0xfb, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03 };
	if (sizeof (size_t) > 4) {
		memset (out, 0xaa, sizeof (out));
		/* Claim a 4 GiB output buffer so only the input bound can stop the copy */
		size_t cap = (size_t)1 << 32;
		if (lzfse_decompress (wrap, sizeof (wrap), out, cap) != 0 || out[0] != 0xaa) {
			printf ("FAIL: wrapping block length not rejected\n");
			failed = 1;
		}
	}

	static const uint8_t fits[] = { 0x06, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c' };
	if (lzfse_decompress (fits, sizeof (fits), out, sizeof (out)) != 3 || memcmp (out, "abc", 3)) {
		printf ("FAIL: exact-length raw block\n");
		failed = 1;
	}
	static const uint8_t short_in[] = { 0x06, 0x04, 0x00, 0x00, 0x00, 'a', 'b', 'c' };
	if (lzfse_decompress (short_in, sizeof (short_in), out, sizeof (out)) != 0) {
		printf ("FAIL: block longer than its input not rejected\n");
		failed = 1;
	}

	/* Writer: an output buffer smaller than the 5-byte header */
	if (lzfse_write_raw_block ((const uint8_t *)"x", 1, out, 4) != 0) {
		printf ("FAIL: raw block written into a too small buffer\n");
		failed = 1;
	}

	if (!failed) {
		printf ("All LZFSE bounds tests passed!\n");
	}
	return failed;
}
