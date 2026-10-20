#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/include/otezip/zip.h"

#define DATA_SIZE (300 * 1024)

static uint8_t data[DATA_SIZE];

static int add_entry(zip_t *za, const char *name, uint16_t method, size_t len) {
	uint8_t *copy = (uint8_t *)malloc (len? len: 1);
	if (!copy) {
		return -1;
	}
	memcpy (copy, data, len);
	zip_source_t *src = zip_source_buffer (za, copy, len, 1);
	if (!src) {
		free (copy);
		return -1;
	}
	za->default_method = method;
	if (zip_file_add (za, name, src, 0) < 0) {
		zip_source_free (src);
		return -1;
	}
	return 0;
}

/* Read entry index in step-byte pieces; returns bytes read, or -1 on error */
static long read_entry(zip_t *za, zip_uint64_t index, size_t step, uint8_t *out, size_t cap) {
	zip_file_t *zf = zip_fopen_index (za, index, 0);
	if (!zf) {
		return -1;
	}
	size_t pos = 0;
	zip_int64_t n;
	do {
		size_t want = cap - pos < step? cap - pos: step;
		n = zip_fread (zf, out + pos, want);
		if (n > 0) {
			pos += (size_t)n;
		}
	} while (n > 0 && pos < cap);
	zip_fclose (zf);
	return n < 0? -1: (long)pos;
}

int main(void) {
	for (size_t i = 0; i < DATA_SIZE; i++) {
		data[i] = (uint8_t) ((i / 37) % 7); /* compressible runs */
	}
	char path[] = "/tmp/otezip-stream-XXXXXX";
	int fd = mkstemp (path);
	if (fd < 0) {
		perror ("mkstemp");
		return 1;
	}
	close (fd);
	unlink (path);

	int err = 0;
	zip_t *za = zip_open (path, ZIP_CREATE | ZIP_TRUNCATE, &err);
	if (!za || add_entry (za, "store", OTEZIP_METHOD_STORE, DATA_SIZE) || add_entry (za, "deflate", OTEZIP_METHOD_DEFLATE, DATA_SIZE) ||
		add_entry (za, "lzma", OTEZIP_METHOD_LZMA, DATA_SIZE) || add_entry (za, "brotli", OTEZIP_METHOD_BROTLI, DATA_SIZE) ||
		add_entry (za, "empty", OTEZIP_METHOD_DEFLATE, 0) || zip_close (za) != 0) {
		fprintf (stderr, "FAIL: could not write test archive\n");
		unlink (path);
		return 1;
	}

	za = zip_open (path, ZIP_RDONLY, &err);
	if (!za) {
		fprintf (stderr, "FAIL: zip_open (err=%d)\n", err);
		unlink (path);
		return 1;
	}
	static uint8_t out[DATA_SIZE + 64];
	static const size_t steps[] = { 1, 7, 4096, sizeof (out) };
	int failed = 0;
	for (zip_uint64_t i = 0; i < 5; i++) {
		size_t want = i == 4? 0: DATA_SIZE;
		for (size_t s = 0; s < sizeof (steps) / sizeof (steps[0]); s++) {
			long got = read_entry (za, i, steps[s], out, sizeof (out));
			if (got != (long)want || memcmp (out, data, want)) {
				fprintf (stderr, "FAIL: %s with %zu-byte reads (got %ld)\n", zip_get_name (za, i, 0), steps[s], got);
				failed = 1;
			}
		}
	}

	/* Closing a partially read entry must release everything (checked by ASan) */
	zip_file_t *zf = zip_fopen_index (za, 1, 0);
	if (!zf || zip_fread (zf, out, 100) != 100) {
		fprintf (stderr, "FAIL: partial read\n");
		failed = 1;
	}
	zip_fclose (zf);

	/* A CRC mismatch surfaces at the end of the stream */
	za->entries[1].crc32 ^= 1;
	otezip_verify_crc = 1;
	if (read_entry (za, 1, 4096, out, sizeof (out)) != -1) {
		fprintf (stderr, "FAIL: CRC mismatch accepted with otezip_verify_crc\n");
		failed = 1;
	}
	otezip_verify_crc = 0;
	if (read_entry (za, 1, 4096, out, sizeof (out)) != DATA_SIZE) {
		fprintf (stderr, "FAIL: CRC mismatch without otezip_verify_crc is not just a warning\n");
		failed = 1;
	}
	zip_close (za);
	unlink (path);

	if (!failed) {
		puts ("TEST PASSED: Entries stream through zip_fread.");
	}
	return failed;
}
