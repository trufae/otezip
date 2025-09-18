#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <stdint.h>

/* Simple test to compress and decompress data using deflate to verify our implementation */

int test_compress_decompress() {
	const char *test_data = "Hello, this is a test of deflate compression and decompression.";
	const size_t data_len = strlen(test_data);
	uint8_t compressed[1024] = {0};
	uint8_t decompressed[1024] = {0};

	/* Compress with zlib using raw deflate format */
	z_stream c_strm = {0};
	c_strm.zalloc = Z_NULL;
	c_strm.zfree = Z_NULL;
	c_strm.opaque = Z_NULL;

	/* Initialize with raw deflate format (negative window bits) */
	if (deflateInit2(&c_strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
		printf("deflateInit2 failed\n");
		return 1;
	}

	c_strm.next_in = (uint8_t*)test_data;
	c_strm.avail_in = data_len;
	c_strm.next_out = compressed;
	c_strm.avail_out = sizeof(compressed);

	/* Compress in a single step */
	if (deflate(&c_strm, Z_FINISH) != Z_STREAM_END) {
		printf("deflate failed\n");
		deflateEnd(&c_strm);
		return 1;
	}

	size_t compressed_len = c_strm.total_out;
	deflateEnd(&c_strm);

	printf("Original data size: %zu bytes\n", data_len);
	printf("Compressed data size: %zu bytes\n", compressed_len);

	/* Now decompress */
	z_stream d_strm = {0};
	d_strm.zalloc = Z_NULL;
	d_strm.zfree = Z_NULL;
	d_strm.opaque = Z_NULL;

	/* Initialize with raw deflate format (negative window bits) */
	if (inflateInit2(&d_strm, -MAX_WBITS) != Z_OK) {
		printf("inflateInit2 failed\n");
		return 1;
	}

	d_strm.next_in = compressed;
	d_strm.avail_in = compressed_len;
	d_strm.next_out = decompressed;
	d_strm.avail_out = sizeof(decompressed);

	/* Decompress in a single step */
	int result = inflate(&d_strm, Z_FINISH);
	if (result != Z_STREAM_END) {
		printf("inflate failed with result %d\n", result);
		inflateEnd(&d_strm);
		return 1;
	}

	size_t decompressed_len = d_strm.total_out;
	inflateEnd(&d_strm);

	printf("Decompressed data size: %zu bytes\n", decompressed_len);
	printf("Decompressed data: %s\n", decompressed);

	/* Verify decompressed data matches original */
	if (decompressed_len != data_len || memcmp(decompressed, test_data, data_len) != 0) {
		printf("ERROR: Decompressed data does not match original!\n");
		return 1;
	}

	printf("TEST PASSED: Compression and decompression successful.\n");
	return 0;
}

int main(int argc, char *argv[]) {
	(void)argc; (void)argv;
	printf("Running deflate compression test...\n");
	int result = test_compress_decompress();
	return result;
}
