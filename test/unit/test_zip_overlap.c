#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/include/otezip/zip.h"

/* Stored entries only; CRCs are left zero since only zip_open is exercised */

static uint8_t buf[4096];
static size_t len;

static void put16(uint32_t v) {
	buf[len++] = v & 0xff;
	buf[len++] = (v >> 8) & 0xff;
}

static void put32(uint32_t v) {
	put16 (v & 0xffff);
	put16 (v >> 16);
}

static void put_lfh(const char *name, uint32_t size) {
	put32 (0x04034b50);
	put16 (20); put16 (0); put16 (0); put16 (0); put16 (0);
	put32 (0); put32 (size); put32 (size);
	put16 (strlen (name)); put16 (0);
	memcpy (buf + len, name, strlen (name));
	len += strlen (name);
}

static void put_cdh(const char *name, uint32_t size, uint32_t ofs) {
	put32 (0x02014b50);
	put16 (0x31e); put16 (20); put16 (0); put16 (0); put16 (0); put16 (0);
	put32 (0); put32 (size); put32 (size);
	put16 (strlen (name)); put16 (0); put16 (0); put16 (0); put16 (0);
	put32 (0); put32 (ofs);
	memcpy (buf + len, name, strlen (name));
	len += strlen (name);
}

static void put_eocd(uint16_t n, uint32_t cd_ofs) {
	uint32_t cd_size = len - cd_ofs;
	put32 (0x06054b50);
	put16 (0); put16 (0); put16 (n); put16 (n);
	put32 (cd_size); put32 (cd_ofs); put16 (0);
}

static void put_data(char c, uint32_t n) {
	memset (buf + len, c, n);
	len += n;
}

/* Three central directory entries sharing one local header and its data */
static void build_shared_header(void) {
	len = 0;
	put_lfh ("k", 100);
	put_data ('x', 100);
	uint32_t cd = len;
	put_cdh ("e0", 100, 0);
	put_cdh ("e1", 100, 0);
	put_cdh ("e2", 100, 0);
	put_eocd (3, cd);
}

/* Entry "a" stores entry "b" (header and data) inside its own data */
static void build_quoted(void) {
	len = 0;
	uint32_t b_size = 100;
	uint32_t a_size = 30 + 1 + b_size;
	put_lfh ("a", a_size);
	uint32_t b_ofs = len;
	put_lfh ("b", b_size);
	put_data ('y', b_size);
	uint32_t cd = len;
	put_cdh ("a", a_size, 0);
	put_cdh ("b", b_size, b_ofs);
	put_eocd (2, cd);
}

/* Two ordinary back-to-back entries */
static void build_valid(void) {
	len = 0;
	put_lfh ("a", 10);
	put_data ('a', 10);
	uint32_t b_ofs = len;
	put_lfh ("b", 10);
	put_data ('b', 10);
	uint32_t cd = len;
	put_cdh ("a", 10, 0);
	put_cdh ("b", 10, b_ofs);
	put_eocd (2, cd);
}

/* Returns 1 if zip_open accepted the archive currently in buf */
static int opens(void) {
	char path[] = "/tmp/otezip-overlap-XXXXXX";
	int fd = mkstemp (path);
	if (fd < 0) {
		perror ("mkstemp");
		exit (1);
	}
	if (write (fd, buf, len) != (ssize_t)len) {
		perror ("write");
		exit (1);
	}
	close (fd);
	int err = 0;
	zip_t *za = zip_open (path, ZIP_RDONLY, &err);
	if (za) {
		zip_close (za);
	}
	unlink (path);
	return za != NULL;
}

int main(void) {
	int failed = 0;

	build_valid ();
	if (!opens ()) {
		fprintf (stderr, "FAIL: valid archive rejected\n");
		failed = 1;
	}
	build_shared_header ();
	if (opens ()) {
		fprintf (stderr, "FAIL: entries sharing a local header accepted\n");
		failed = 1;
	}
	build_quoted ();
	if (opens ()) {
		fprintf (stderr, "FAIL: entry quoting another entry accepted\n");
		failed = 1;
	}
	otezip_ignore_zipbomb = 1;
	build_shared_header ();
	if (!opens ()) {
		fprintf (stderr, "FAIL: otezip_ignore_zipbomb does not allow overlapping entries\n");
		failed = 1;
	}
	otezip_ignore_zipbomb = 0;

	if (failed) {
		return 1;
	}
	puts ("TEST PASSED: Overlapping entries are rejected.");
	return 0;
}
