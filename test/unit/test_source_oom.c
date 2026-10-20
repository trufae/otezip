#include <stdio.h>
#include <stdlib.h>

/* Make the next allocation fail on demand */
static int fail_next = 0;
static void *test_malloc(size_t n) {
	if (fail_next) {
		fail_next = 0;
		return NULL;
	}
	return malloc (n);
}
#define malloc test_malloc
#include "../../src/lib/otezip.c"
#undef malloc

int main(void) {
	char data[] = "x";
	int failed = 0;

	fail_next = 1;
	if (zip_source_buffer (NULL, data, 1, 0) != NULL) {
		printf ("FAIL: zip_source_buffer returned a source without memory\n");
		failed = 1;
	}
	fail_next = 1;
	if (zip_source_buffer_create (data, 1, 0, NULL) != NULL) {
		printf ("FAIL: zip_source_buffer_create returned a source without memory\n");
		failed = 1;
	}
	zip_source_t *src = zip_source_buffer (NULL, data, 1, 0);
	if (!src || src->buf != data || src->len != 1) {
		printf ("FAIL: zip_source_buffer without memory pressure\n");
		failed = 1;
	}
	zip_source_free (src);

	if (!failed) {
		printf ("All zip source OOM tests passed!\n");
	}
	return failed;
}
