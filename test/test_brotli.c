/* test_brotli.c - Test for the minimalistic Brotli implementation
 *
 * This test file verifies that the brotli compression and decompression
 * works correctly by:
 * 1. Compressing test strings
 * 2. Decompressing them back
 * 3. Verifying the results match the original
 *
 * Compile with:
 *   cc -DOTEZIP_ENABLE_BROTLI -DMBROTLI_IMPLEMENTATION test_brotli.c -o test_brotli
 *
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* z_stream is defined in zstream.h */

/* Define MBROTLI_IMPLEMENTATION to include the implementation */
#define MBROTLI_IMPLEMENTATION
#include "../../src/lib/brotli.inc.c"

/* Maximum buffer sizes */
#define MAX_COMP_SIZE 1024
#define MAX_DECOMP_SIZE 1024

/* Test strings */
const char *test_strings[] = {
	"Hello, world!",
	"The quick brown fox jumps over the lazy dog.",
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit.",
	"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", /* Highly compressible */
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" /* Mix of characters */
};
#define NUM_TESTS (sizeof (test_strings) / sizeof (test_strings[0]))

/* Helper function to compress a string */
size_t compress_string(const char *input, size_t input_len, uint8_t *output, size_t output_size) {
	z_stream strm = { 0 };
	int ret;

	/* Initialize the compression stream */
	ret = brotliInit (&strm, 5); /* Use default compression level */
	if (ret != Z_OK) {
		printf ("Failed to initialize Brotli compressor\n");
		return 0;
	}

	/* Set up the input and output buffers */
	strm.next_in = (uint8_t *)input;
	strm.avail_in = input_len;
	strm.next_out = output;
	strm.avail_out = output_size;

	/* Compress the data */
	ret = brotliCompress (&strm, Z_FINISH);
	if (ret != Z_STREAM_END) {
		printf ("Compression failed with error code: %d\n", ret);
		brotliEnd (&strm);
		return 0;
	}

	/* Clean up */
	size_t compressed_size = strm.total_out;
	brotliEnd (&strm);

	return compressed_size;
}

/* Helper function to decompress a buffer */
size_t decompress_buffer(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_size) {
	z_stream strm = { 0 };
	int ret;

	/* Initialize the decompression stream */
	ret = brotliDecompressInit (&strm);
	if (ret != Z_OK) {
		printf ("Failed to initialize Brotli decompressor\n");
		return 0;
	}

	/* Set up the input and output buffers */
	strm.next_in = (uint8_t *)input;
	strm.avail_in = input_len;
	strm.next_out = output;
	strm.avail_out = output_size;

	/* Decompress the data */
	ret = brotliDecompress (&strm, Z_FINISH);
	if (ret != Z_STREAM_END) {
		printf ("Decompression failed with error code: %d\n", ret);
		brotliDecompressEnd (&strm);
		return 0;
	}

	/* Clean up */
	size_t decompressed_size = strm.total_out;
	brotliDecompressEnd (&strm);

	return decompressed_size;
}

int main() {
	uint8_t compressed[MAX_COMP_SIZE];
	uint8_t decompressed[MAX_DECOMP_SIZE];
	size_t comp_size, decomp_size;
	int passed = 0;

	printf ("Testing minimalistic Brotli implementation...\n\n");

	for (size_t i = 0; i < NUM_TESTS; i++) {
		const char *test_str = test_strings[i];
		size_t input_len = strlen (test_str);

		printf ("Test %zu: \"%s\" (length: %zu)\n", i + 1, test_str, input_len);

		/* Compress the test string */
		comp_size = compress_string (test_str, input_len, compressed, MAX_COMP_SIZE);
		if (comp_size == 0) {
			printf ("  FAIL: Compression failed\n");
			continue;
		}

		printf ("  Compressed size: %zu bytes (%.2f%%)\n", comp_size,
			(float)comp_size * 100.0f / (float)input_len);

		/* Decompress the compressed buffer */
		decomp_size = decompress_buffer (compressed, comp_size, decompressed, MAX_DECOMP_SIZE);
		if (decomp_size == 0) {
			printf ("  FAIL: Decompression failed\n");
			continue;
		}

		/* Verify the decompressed data matches the original */
		if (decomp_size != input_len || memcmp (decompressed, test_str, input_len) != 0) {
			printf ("  FAIL: Decompressed data does not match original\n");
			printf ("  Original: %s\n", test_str);
			printf ("  Decompressed: %.*s\n", (int)decomp_size, decompressed);
			continue;
		}

		printf ("  SUCCESS: Original data successfully recovered\n");
		passed++;
	}

	printf ("\nFinal result: %d/%zu tests passed\n", passed, NUM_TESTS);
	return (passed == NUM_TESTS)? 0: 1;
}