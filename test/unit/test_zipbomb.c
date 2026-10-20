#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>

#include "../../src/include/otezip/zip.h"

static void wr16(uint8_t *p, uint16_t v) {
	p[0] = v & 0xff;
	p[1] = v >> 8;
}

static void wr32(uint8_t *p, uint32_t v) {
	wr16 (p, v & 0xffff);
	wr16 (p + 2, v >> 16);
}

/* Single deflate entry with comp_size=0 but a ~2 GiB declared uncompressed size */
static int write_bomb_zip(const char *path) {
	uint8_t z[30 + 1 + 46 + 1 + 22];
	memset (z, 0, sizeof (z));
	uint8_t *lfh = z;
	uint8_t *cd = z + 31;
	uint8_t *eocd = z + 31 + 47;
	wr32 (lfh, 0x04034b50);
	wr16 (lfh + 4, 20);
	wr16 (lfh + 8, 8);
	wr32 (lfh + 22, 0x7fff0000);
	wr16 (lfh + 26, 1);
	lfh[30] = 'a';
	wr32 (cd, 0x02014b50);
	wr16 (cd + 4, 20);
	wr16 (cd + 6, 20);
	wr16 (cd + 10, 8);
	wr32 (cd + 24, 0x7fff0000);
	wr16 (cd + 28, 1);
	cd[46] = 'a';
	wr32 (eocd, 0x06054b50);
	wr16 (eocd + 8, 1);
	wr16 (eocd + 10, 1);
	wr32 (eocd + 12, 47);
	wr32 (eocd + 16, 31);
	FILE *fp = fopen (path, "wb");
	if (!fp) {
		perror ("fopen");
		return 1;
	}
	if (fwrite (z, 1, sizeof (z), fp) != sizeof (z)) {
		perror ("fwrite");
		fclose (fp);
		return 1;
	}
	return fclose (fp) != 0;
}

/* Peak RSS in bytes (ru_maxrss is bytes on macOS, KiB elsewhere) */
static uint64_t peak_rss(void) {
	struct rusage ru;
	if (getrusage (RUSAGE_SELF, &ru) != 0) {
		return 0;
	}
#ifdef __APPLE__
	return (uint64_t)ru.ru_maxrss;
#else
	return (uint64_t)ru.ru_maxrss * 1024;
#endif
}

int main(void) {
	char path[] = "/tmp/otezip-bomb-XXXXXX";
	int fd = mkstemp (path);
	if (fd < 0) {
		perror ("mkstemp");
		return 1;
	}
	close (fd);
	if (write_bomb_zip (path) != 0) {
		unlink (path);
		return 1;
	}
	int err = 0;
	zip_t *za = zip_open (path, ZIP_RDONLY, &err);
	if (!za) {
		fprintf (stderr, "zip_open failed: %d\n", err);
		unlink (path);
		return 1;
	}
	uint64_t rss_before = peak_rss ();
	zip_file_t *zf = zip_fopen_index (za, 0, 0);
	uint64_t rss_grow = peak_rss () - rss_before;
	int rc = 0;
	if (zf) {
		fprintf (stderr, "FAIL: zero-length compressed entry with huge size was accepted\n");
		zip_fclose (zf);
		rc = 1;
	}
	/* Rejection must happen before allocating the declared size */
	if (rss_grow > 64u * 1024u * 1024u) {
		fprintf (stderr, "FAIL: peak RSS grew by %llu bytes before rejecting\n", (unsigned long long)rss_grow);
		rc = 1;
	}
	zip_close (za);
	unlink (path);
	if (!rc) {
		printf ("test_zipbomb passed\n");
	}
	return rc;
}
