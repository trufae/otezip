#include <stdio.h>
#include <string.h>
#include "otezip.h"

int main() {
	uint8_t hello[] = "hello
			";
		uint8_t *
			compressed;
	uint32_t comp_size;
	otezip_compress_data (hello, 6, &compressed, &comp_size, OTEZIP_METHOD_DEFLATE);
	printf ("Original: %s", hello);
printf("Compressed size: %u
", comp_size);
uint8_t *decompressed = malloc(6);
z_stream strm = {0};
strm.next_in = compressed;
strm.avail_in = comp_size;
strm.next_out = decompressed;
strm.avail_out = 6;
inflateInit2(&strm, -MAX_WBITS);
int ret = inflate(&strm, Z_FINISH);
inflateEnd(&strm);
printf("Decompressed: %s", decompressed);
printf("Return code: %d
", ret);
return 0;
}
