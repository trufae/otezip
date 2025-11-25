#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int main(int argc, char *argv[]) {
	if (argc < 3) {
		printf ("Usage: %s <input_file> <output_file>\n", argv[0]);
		return 1;
	}

	// Open the input file
	FILE *fin = fopen (argv[1], "rb");
	if (!fin) {
		perror ("Failed to open input file");
		return 1;
	}

	// Get the file size
	fseek (fin, 0, SEEK_END);
	long file_size = ftell (fin);
	fseek (fin, 0, SEEK_SET);

	// Read the compressed data
	unsigned char *compressed_data = malloc (file_size);
	if (fread (compressed_data, 1, file_size, fin) != file_size) {
		perror ("Failed to read input file");
		fclose (fin);
		free (compressed_data);
		return 1;
	}
	fclose (fin);

	// Allocate output buffer (assuming output won't be more than 10x input)
	unsigned char *decompressed_data = malloc (file_size * 10);

	// Set up zlib stream for decompression
	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	strm.next_in = compressed_data;
	strm.avail_in = file_size;
	strm.next_out = decompressed_data;
	strm.avail_out = file_size * 10;

	// Initialize with raw deflate format (negative window bits)
	if (inflateInit2 (&strm, -MAX_WBITS) != Z_OK) {
		fprintf (stderr, "Failed to initialize zlib\n");
		free (compressed_data);
		free (decompressed_data);
		return 1;
	}

	// Decompress
	int ret = inflate (&strm, Z_FINISH);
	if (ret != Z_STREAM_END) {
		fprintf (stderr, "Decompression failed, error code: %d\n", ret);
		inflateEnd (&strm);
		free (compressed_data);
		free (decompressed_data);
		return 1;
	}

	// Get the decompressed size
	unsigned long decompressed_size = strm.total_out;
	inflateEnd (&strm);

	// Write the decompressed data to the output file
	FILE *fout = fopen (argv[2], "wb");
	if (!fout) {
		perror ("Failed to open output file");
		free (compressed_data);
		free (decompressed_data);
		return 1;
	}

	if (fwrite (decompressed_data, 1, decompressed_size, fout) != decompressed_size) {
		perror ("Failed to write output file");
		fclose (fout);
		free (compressed_data);
		free (decompressed_data);
		return 1;
	}

	fclose (fout);
	free (compressed_data);
	free (decompressed_data);

	printf ("Decompressed %lu bytes of data\n", decompressed_size);
	return 0;
}