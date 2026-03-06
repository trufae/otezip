#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/include/otezip/zip.h"

static int write_empty_zip(const char *path) {
	static const uint8_t empty_zip[] = {
		0x50, 0x4b, 0x05, 0x06,
		0x00, 0x00,
		0x00, 0x00,
		0x00, 0x00,
		0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00
	};
	FILE *fp = fopen (path, "wb");
	if (!fp) {
		perror ("fopen");
		return 1;
	}
	if (fwrite (empty_zip, 1, sizeof (empty_zip), fp) != sizeof (empty_zip)) {
		perror ("fwrite");
		fclose (fp);
		return 1;
	}
	if (fclose (fp) != 0) {
		perror ("fclose");
		return 1;
	}
	return 0;
}

int main(void) {
	char path[] = "/tmp/otezip-empty-XXXXXX";
	int fd = mkstemp (path);
	if (fd < 0) {
		perror ("mkstemp");
		return 1;
	}
	close (fd);

	if (write_empty_zip (path) != 0) {
		unlink (path);
		return 1;
	}

	int err = -1;
	zip_t *za = zip_open (path, ZIP_RDONLY, &err);
	if (!za) {
		fprintf (stderr, "zip_open failed for empty archive: err=%d\n", err);
		unlink (path);
		return 1;
	}

	if (zip_get_num_files (za) != 0) {
		fprintf (stderr, "expected 0 entries, got %llu\n",
			(unsigned long long)zip_get_num_files (za));
		zip_close (za);
		unlink (path);
		return 1;
	}

	if (zip_close (za) != 0) {
		fprintf (stderr, "zip_close failed\n");
		unlink (path);
		return 1;
	}

	if (unlink (path) != 0) {
		perror ("unlink");
		return 1;
	}

	puts ("TEST PASSED: Empty EOCD-only ZIP archive opens successfully.");
	return 0;
}
