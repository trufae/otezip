#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/include/otezip/zip.h"

int main(void) {
	enum { payload_size = 4096 };
	char path[] = "/tmp/otezip-compress-XXXXXX";
	char expected[payload_size];
	memset (expected, 'A', sizeof (expected));
	int fd = mkstemp (path);
	if (fd < 0) {
		perror ("mkstemp");
		return 1;
	}
	close (fd);
	unlink (path);

	int err = -1;
	zip_t *za = zip_open (path, ZIP_CREATE | ZIP_TRUNCATE, &err);
	if (!za) {
		fprintf (stderr, "zip_open(create) failed: %d\n", err);
		return 1;
	}

	char *buf = (char *)malloc (sizeof (expected));
	if (!buf) {
		perror ("malloc");
		zip_close (za);
		unlink (path);
		return 1;
	}
	memcpy (buf, expected, sizeof (expected));

	zip_source_t *src = zip_source_buffer (za, buf, sizeof (expected), 1);
	if (!src) {
		fprintf (stderr, "zip_source_buffer failed\n");
		free (buf);
		zip_close (za);
		unlink (path);
		return 1;
	}

	zip_int64_t idx = zip_file_add (za, "hello.txt", src, 0);
	if (idx < 0) {
		fprintf (stderr, "zip_file_add failed\n");
		zip_source_free (src);
		zip_close (za);
		unlink (path);
		return 1;
	}
	if (zip_set_file_compression (za, (zip_uint64_t)idx, ZIP_CM_DEFLATE, 0) != 0) {
		fprintf (stderr, "zip_set_file_compression failed\n");
		zip_close (za);
		unlink (path);
		return 1;
	}
	if (zip_close (za) != 0) {
		fprintf (stderr, "zip_close(write) failed\n");
		unlink (path);
		return 1;
	}

	za = zip_open (path, ZIP_RDONLY, &err);
	if (!za) {
		fprintf (stderr, "zip_open(read) failed: %d\n", err);
		unlink (path);
		return 1;
	}

	const char *name = zip_get_name (za, 0, 0);
	if (!name || strcmp (name, "hello.txt") != 0) {
		fprintf (stderr, "unexpected name: %s\n", name? name: "(null)");
		zip_close (za);
		unlink (path);
		return 1;
	}

	zip_stat_t st;
	zip_stat_init (&st);
	if (zip_stat_index (za, 0, 0, &st) != 0) {
		fprintf (stderr, "zip_stat_index failed\n");
		zip_close (za);
		unlink (path);
		return 1;
	}
	if (st.size != sizeof (expected)) {
		fprintf (stderr, "unexpected size: %llu\n", (unsigned long long)st.size);
		zip_close (za);
		unlink (path);
		return 1;
	}

	zip_file_t *zf = zip_fopen_index (za, 0, 0);
	if (!zf) {
		fprintf (stderr, "zip_fopen_index failed\n");
		zip_close (za);
		unlink (path);
		return 1;
	}

	char out[payload_size];
	memset (out, 0, sizeof (out));
	zip_int64_t nr = zip_fread (zf, out, sizeof (out));
	if (nr != (zip_int64_t)sizeof (expected)) {
		fprintf (stderr, "unexpected read size: %lld\n", (long long)nr);
		zip_fclose (zf);
		zip_close (za);
		unlink (path);
		return 1;
	}
	if (memcmp (out, expected, sizeof (expected)) != 0) {
		fprintf (stderr, "payload mismatch\n");
		zip_fclose (zf);
		zip_close (za);
		unlink (path);
		return 1;
	}

	zip_fclose (zf);
	zip_close (za);
	unlink (path);
	puts ("TEST PASSED: compression can be set via public API after add.");
	return 0;
}
