#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Include ZSTD implementation */
#define OTEZIP_ENABLE_ZSTD
#include "../../src/lib/zstd.inc.c"

/* Simple test to compress and decompress data using ZSTD */
int test_zstd_compress_decompress() {
	const char *test_data = "Hello, this is a test of ZSTD compression and decompression.";
	const size_t data_len = strlen (test_data);
	uint8_t compressed[1024] = { 0 };
	uint8_t decompressed[1024] = { 0 };

	/* Compress with ZSTD */
	z_stream c_strm = { 0 };

	if (zstdInit (&c_strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
		printf ("zstdInit failed\n");
		return 1;
	}

	c_strm.next_in = (uint8_t *)test_data;
	c_strm.avail_in = data_len;
	c_strm.next_out = compressed;
	c_strm.avail_out = sizeof (compressed);

	/* Compress in a single step */
	int compress_result = zstdCompress (&c_strm, Z_FINISH);
	if (compress_result != Z_STREAM_END) {
		printf ("zstdCompress failed with result %d\n", compress_result);
		zstdEnd (&c_strm);
		return 1;
	}

	size_t compressed_len = c_strm.total_out;
	zstdEnd (&c_strm);

	printf ("Original data size: %zu bytes\n", data_len);
	printf ("Compressed data size: %zu bytes\n", compressed_len);

	/* Now decompress */
	z_stream d_strm = { 0 };

	if (zstdDecompressInit (&d_strm) != Z_OK) {
		printf ("zstdDecompressInit failed\n");
		return 1;
	}

	d_strm.next_in = compressed;
	d_strm.avail_in = compressed_len;
	d_strm.next_out = decompressed;
	d_strm.avail_out = sizeof (decompressed);

	/* Decompress in a single step */
	int decompress_result = zstdDecompress (&d_strm, Z_FINISH);
	if (decompress_result != Z_STREAM_END) {
		printf ("zstdDecompress failed with result %d\n", decompress_result);
		zstdDecompressEnd (&d_strm);
		return 1;
	}

	size_t decompressed_len = d_strm.total_out;
	zstdDecompressEnd (&d_strm);

	printf ("Decompressed data size: %zu bytes\n", decompressed_len);
	printf ("Decompressed data: %s\n", decompressed);

	/* Verify decompressed data matches original */
	if (decompressed_len != data_len || memcmp (decompressed, test_data, data_len) != 0) {
		printf ("ERROR: Decompressed data does not match original!\n");
		return 1;
	}

	printf ("TEST PASSED: ZSTD Compression and decompression successful.\n");
	return 0;
}

/* Test with larger data to ensure proper block handling */
int test_zstd_large_data() {
	/* Generate a larger test string with repeated pattern */
	size_t test_size = 10000;
	char *test_data = malloc (test_size + 1);
	if (!test_data) {
		printf ("Memory allocation failed\n");
		return 1;
	}

	/* Fill with repeating pattern */
	for (size_t i = 0; i < test_size; i++) {
		test_data[i] = 'A' + (i % 26);
	}
	test_data[test_size] = '\0';

	/* Allocate buffers */
	uint8_t *compressed = malloc (test_size * 2);
	uint8_t *decompressed = malloc (test_size + 100);

	if (!compressed || !decompressed) {
		printf ("Memory allocation failed\n");
		free (test_data);
		free (compressed);
		free (decompressed);
		return 1;
	}

	/* Compress */
	z_stream c_strm = { 0 };

	if (zstdInit (&c_strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
		printf ("zstdInit failed\n");
		free (test_data);
		free (compressed);
		free (decompressed);
		return 1;
	}

	c_strm.next_in = (uint8_t *)test_data;
	c_strm.avail_in = test_size;
	c_strm.next_out = compressed;
	c_strm.avail_out = test_size * 2;

	int compress_result = zstdCompress (&c_strm, Z_FINISH);
	if (compress_result != Z_STREAM_END) {
		printf ("zstdCompress failed with result %d\n", compress_result);
		zstdEnd (&c_strm);
		free (test_data);
		free (compressed);
		free (decompressed);
		return 1;
	}

	size_t compressed_len = c_strm.total_out;
	zstdEnd (&c_strm);

	printf ("Large test: Original size: %zu bytes, Compressed size: %zu bytes\n",
		test_size,
		compressed_len);

	/* Decompress */
	z_stream d_strm = { 0 };

	if (zstdDecompressInit (&d_strm) != Z_OK) {
		printf ("zstdDecompressInit failed\n");
		free (test_data);
		free (compressed);
		free (decompressed);
		return 1;
	}

	d_strm.next_in = compressed;
	d_strm.avail_in = compressed_len;
	d_strm.next_out = decompressed;
	d_strm.avail_out = test_size + 100;

	int decompress_result = zstdDecompress (&d_strm, Z_FINISH);
	if (decompress_result != Z_STREAM_END) {
		printf ("zstdDecompress failed with result %d\n", decompress_result);
		zstdDecompressEnd (&d_strm);
		free (test_data);
		free (compressed);
		free (decompressed);
		return 1;
	}

	size_t decompressed_len = d_strm.total_out;
	zstdDecompressEnd (&d_strm);

	/* Verify result */
	if (decompressed_len != test_size || memcmp (decompressed, test_data, test_size) != 0) {
		printf ("ERROR: Large data test failed - decompressed data does not match original!\n");
		free (test_data);
		free (compressed);
		free (decompressed);
		return 1;
	}

	printf ("TEST PASSED: ZSTD large data compression and decompression successful.\n");

	free (test_data);
	free (compressed);
	free (decompressed);
	return 0;
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	printf ("Running ZSTD basic test...\n");
	int result1 = test_zstd_compress_decompress ();

	printf ("\nRunning ZSTD large data test...\n");
	int result2 = test_zstd_large_data ();

	return (result1 || result2);
}
