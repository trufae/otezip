/* mzip.c – Minimalistic libzip subset replacement implementation
 * Version: 0.2 (2025-07-27)
 *
 * Implementation file for the mzip library providing a tiny subset of the libzip API.
 *
 * License: MIT / 0-BSD – do whatever you want; attribution appreciated.
 */

#define MZIP_IMPLEMENTATION

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "mzip.h"

#if MZIP_ENABLE_LZ4
#include <r_util.h>
#endif

/* Include compression algorithms based on config */
#if MZIP_ENABLE_DEFLATE
#include <zlib.h>
#endif
#if MZIP_ENABLE_ZSTD
#include "zstd.inc.c"
#endif
#if MZIP_ENABLE_LZFSE
#include "lzfse.inc.c"
#endif
#if MZIP_ENABLE_LZMA
#include "lzma.inc.c"
#endif
#if MZIP_ENABLE_BROTLI
#include "brotli.inc.c"
#endif

/* Local/central directory signatures */
#define MZIP_SIG_LFH  0x04034b50u
#define MZIP_SIG_CDH  0x02014b50u
#define MZIP_SIG_EOCD 0x06054b50u

/* CRC-32 polynomial table (reversed) */
static const uint32_t crc32_table[256] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
	0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
	0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
	0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
	0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
	0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
	0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
	0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
	0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
	0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
	0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
	0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
	0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
	0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
	0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
	0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

/* Calculate CRC-32 checksum (standard reversed polynomial) */
static uint32_t mzip_crc32(uint32_t crc, const void *buf, size_t len) {
	const uint8_t *p = (const uint8_t*)buf;
	crc = ~crc;
	for (size_t i = 0; i < len; i++)
		crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

/* Forward declarations of helper functions */
static uint32_t mzip_write_local_header(FILE *fp, const char *name, uint32_t comp_method, 
		uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32);
static uint32_t mzip_write_central_header(FILE *fp, const char *name, uint32_t comp_method,
		uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32,
		uint32_t local_header_offset, uint16_t file_time, uint16_t file_date, uint32_t external_attr);
static void mzip_write_end_of_central_directory(FILE *fp, uint32_t num_entries, 
		uint32_t central_dir_size, uint32_t central_dir_offset);
static int mzip_finalize_archive(zip_t *za);

/* helper: little-endian readers/writers (ZIP format is little-endian) */

/* Date/time conversion for ZIP entries */
static void mzip_get_dostime(uint16_t *dos_time, uint16_t *dos_date) {
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	*dos_time = (uint16_t)((tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2));
	*dos_date = (uint16_t)(((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday);
}
static uint16_t mzip_rd16 (const uint8_t *p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t mzip_rd32 (const uint8_t *p) {
	return (uint32_t)(p[0]        | (p[1] << 8) |
			(p[2] << 16) | (p[3] << 24));
}
static void mzip_wr16 (uint8_t *p, uint16_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void mzip_wr32 (uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
	p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* ----  internal helpers  ---- */

static inline int mzip_read_fully (FILE *fp, void *dst, size_t n) {
	return fread (dst, 1, n, fp) == n ? 0 : -1;
}

/* locate EOCD record (last 64KiB + 22 bytes) */
static long mzip_find_eocd(FILE *fp, uint8_t *eocd_out /*22+*/, size_t *cd_size, uint32_t *cd_ofs, uint16_t *total_entries) {
	long file_size;
	if (fseek (fp, 0, SEEK_END) != 0) {
		return -1;
	}
	file_size = ftell (fp);
	if (file_size < 22) {
		return -1;
	}
	const size_t max_back = 0x10000 + 22; /* spec: comment <= 65535 */
	size_t search_len = (size_t)(file_size < (long)max_back ? file_size : max_back);

	if (fseek (fp, file_size - (long)search_len, SEEK_SET) != 0) {
		return -1;
	}
	uint8_t *buf = (uint8_t*)malloc (search_len);
	if (!buf) {
		return -1;
	}
	if (mzip_read_fully (fp, buf, search_len) != 0) {
		free (buf);
		return -1;
	}
	size_t i;
	for (i = search_len - 22; i != (size_t)-1; --i) {
		if (mzip_rd32 (buf + i) == MZIP_SIG_EOCD) {
			memcpy (eocd_out, buf + i, 22);
			*total_entries  = mzip_rd16 (buf + i + 10);
			*cd_size        = mzip_rd32 (buf + i + 12);
			*cd_ofs         = mzip_rd32 (buf + i + 16);
			free(buf);
			return (long)(file_size - search_len + i);
		}
	}
	/* not found */
	free (buf);
	return -1;
}

/* parse central directory into array of mzip_entry */
static int mzip_load_central(zip_t *za) {
	uint8_t  eocd[22];
	size_t   cd_size;
	uint32_t cd_ofs;
	uint16_t n_entries;

	if (mzip_find_eocd (za->fp, eocd, &cd_size, &cd_ofs, &n_entries) < 0) {
		return -1;
	}
	/* read entire central directory */
	if (fseek (za->fp, cd_ofs, SEEK_SET) != 0) {
		return -1;
	}
	uint8_t *cd_buf = (uint8_t*)malloc(cd_size);
	if (!cd_buf) {
		return -1;
	}
	if (mzip_read_fully (za->fp, cd_buf, cd_size) != 0) {
		free (cd_buf);
		return -1;
	}

	za->entries    = (struct mzip_entry*)calloc(n_entries, sizeof(struct mzip_entry));
	za->n_entries  = n_entries;

	if (!za->entries) {
		free(cd_buf);
		return -1;
	}

	size_t off = 0;
	for (uint16_t i = 0; i < n_entries; i++) {
		if (off + 46 > cd_size || mzip_rd32 (cd_buf + off) != MZIP_SIG_CDH) {
			free(cd_buf);
			return -1; /* malformed */
		}
		const uint8_t *h = cd_buf + off;

		uint16_t filename_len = mzip_rd16 (h + 28);
		uint16_t extra_len    = mzip_rd16 (h + 30);
		uint16_t comment_len  = mzip_rd16 (h + 32);

		struct mzip_entry *e = &za->entries[i];
		e->method            = mzip_rd16 (h + 10);
		e->file_time         = mzip_rd16 (h + 12);
		e->file_date         = mzip_rd16 (h + 14);
		e->crc32             = mzip_rd32 (h + 16);
		e->comp_size         = mzip_rd32 (h + 20);
		e->uncomp_size       = mzip_rd32 (h + 24);
		e->local_hdr_ofs     = mzip_rd32 (h + 42);
		e->external_attr     = mzip_rd32 (h + 38);

		e->name = (char*)malloc (filename_len + 1u);
		if (!e->name) {
			free (cd_buf);
			return -1;
		}
		memcpy (e->name, h + 46, filename_len);
		e->name[filename_len] = '\0';

		off += 46 + filename_len + extra_len + comment_len;
	}

	free(cd_buf);
	return 0;
}

/* load entire (uncompressed) file into memory and hand ownership to caller */
static int mzip_extract_entry(zip_t *za, struct mzip_entry *e, uint8_t **out_buf, uint32_t *out_sz) {
	/* move to local header */
	if (fseek (za->fp, e->local_hdr_ofs, SEEK_SET) != 0) {
		return -1;
	}
	uint8_t lfh[30];
	if (mzip_read_fully(za->fp, lfh, 30) != 0) {
		return -1;
	}
	if (mzip_rd32 (lfh) != MZIP_SIG_LFH) {
		return -1;
	}
	uint16_t fn_len = mzip_rd16 (lfh + 26);
	uint16_t extra_len = mzip_rd16 (lfh + 28);

	/* skip filename + extra */
	if (fseek (za->fp, fn_len + extra_len, SEEK_CUR) != 0)
		return -1;

	/* read compressed data */
	uint8_t *cbuf = (uint8_t*)malloc(e->comp_size);
	if (!cbuf) return -1;
	if (mzip_read_fully(za->fp, cbuf, e->comp_size) != 0) {
		free(cbuf);
		return -1;
	}

	uint8_t *ubuf;
#ifdef MZIP_ENABLE_STORE
	if (e->method == MZIP_METHOD_STORE) { /* stored – nothing to inflate */
		ubuf = cbuf;
	}
#endif
#ifdef MZIP_ENABLE_DEFLATE
	else if (e->method == MZIP_METHOD_DEFLATE) { /* deflate */
		ubuf = (uint8_t*)malloc(e->uncomp_size);
		if (!ubuf) { free(cbuf); return -1; }

		z_stream strm = {0};
		strm.next_in   = cbuf;
		strm.avail_in  = e->comp_size;
		strm.next_out  = ubuf;
		strm.avail_out = e->uncomp_size;

		if (inflateInit2 (&strm, -MAX_WBITS) != Z_OK) {
			free(cbuf); free(ubuf); return -1;
		}
		int zret = inflate(&strm, Z_FINISH);
		inflateEnd(&strm);
		if (zret != Z_STREAM_END || strm.total_out != e->uncomp_size) {
			free(cbuf); free(ubuf); return -1;
		}
		free(cbuf);
	}
#endif
#ifdef MZIP_ENABLE_ZSTD
	else if (e->method == MZIP_METHOD_ZSTD) { /* zstd */
		ubuf = (uint8_t*)malloc (e->uncomp_size);
		if (!ubuf) {
			free (cbuf);
			return -1;
		}
		z_stream strm = {0};
		strm.next_in   = cbuf;
		strm.avail_in  = e->comp_size;
		strm.next_out  = ubuf;
		strm.avail_out = e->uncomp_size;

		if (zstdDecompressInit (&strm) != Z_OK) {
			free (cbuf);
			free (ubuf);
			return -1;
		}
		int zret = zstdDecompress (&strm, Z_FINISH);
		zstdDecompressEnd (&strm);
		if (zret != Z_STREAM_END || strm.total_out != e->uncomp_size) {
			free (cbuf);
			free (ubuf);
			return -1;
		}
		free (cbuf);
	}
#endif
#ifdef MZIP_ENABLE_LZFSE
	else if (e->method == MZIP_METHOD_LZFSE) { /* lzfse */
		ubuf = (uint8_t*)malloc (e->uncomp_size);
		if (!ubuf) {
			free (cbuf);
			return -1;
		}
		z_stream strm = {0};
		strm.next_in   = cbuf;
		strm.avail_in  = e->comp_size;
		strm.next_out  = ubuf;
		strm.avail_out = e->uncomp_size;

		if (lzfseDecompressInit (&strm) != Z_OK) {
			free (cbuf);
			free (ubuf);
			return -1;
		}
		int zret = lzfseDecompress (&strm, Z_FINISH);
		lzfseDecompressEnd (&strm);
		if (zret != Z_STREAM_END || strm.total_out != e->uncomp_size) {
			free (cbuf);
			free (ubuf);
			return -1;
		}
		free (cbuf);
	}
#endif
#ifdef MZIP_ENABLE_LZ4
	else if (e->method == MZIP_METHOD_LZ4) { /* lz4 - using radare2's implementation */
		size_t output_size = 0;
		ubuf = r_lz4_decompress (cbuf, e->comp_size, &output_size);
		if (!ubuf || output_size != e->uncomp_size) {
			free (cbuf);
			free (ubuf);
			return -1;
		}
		free (cbuf);
	}
#endif
#ifdef MZIP_ENABLE_LZMA
	else if (e->method == MZIP_METHOD_LZMA) { /* lzma */
		ubuf = (uint8_t*)malloc (e->uncomp_size);
		if (!ubuf) {
			free (cbuf);
			return -1;
		}
		z_stream strm = {0};
		strm.next_in   = cbuf;
		strm.avail_in  = e->comp_size;
		strm.next_out  = ubuf;
		strm.avail_out = e->uncomp_size;

		if (lzmaDecompressInit(&strm) != Z_OK) {
			free(cbuf); free(ubuf); return -1;
		}
		int zret = lzmaDecompress (&strm, Z_FINISH);
		lzmaDecompressEnd (&strm);
		if (zret != Z_STREAM_END || strm.total_out != e->uncomp_size) {
			free (cbuf);
			free (ubuf);
			return -1;
		}
		free (cbuf);
	}
#endif
#ifdef MZIP_ENABLE_BROTLI
	else if (e->method == MZIP_METHOD_BROTLI) { /* brotli */
		ubuf = (uint8_t*)malloc (e->uncomp_size);
		if (!ubuf) {
			free (cbuf);
			return -1;
		}
		z_stream strm = {0};
		strm.next_in   = cbuf;
		strm.avail_in  = e->comp_size;
		strm.next_out  = ubuf;
		strm.avail_out = e->uncomp_size;

		if (brotliDecompressInit (&strm) != Z_OK) {
			free (cbuf);
			free (ubuf);
			return -1;
		}
		int zret = brotliDecompress (&strm, Z_FINISH);
		brotliDecompressEnd (&strm);
		if (zret != Z_STREAM_END || strm.total_out != e->uncomp_size) {
			free (cbuf);
			free (ubuf);
			return -1;
		}
		free (cbuf);
	}
#endif
	else {
		free (cbuf);
		return -1; /* unsupported method */
	}

	*out_buf = ubuf;
	*out_sz  = e->uncomp_size;
	return 0;
}

/* --------------  public API implementation  --------------- */

zip_t *zip_open(const char *path, int flags, int *errorp) {
	zip_t *za = (zip_t*)calloc (1, sizeof (zip_t));
	const char *mode;
	int exists = 0;

	if (flags & ZIP_CREATE) {
		if ((flags & ZIP_EXCL) && (flags & ZIP_TRUNCATE)) {
			if (errorp) {
				*errorp = -1; /* incompatible flags */
			}
			free (za);
			return NULL;
		}

		/* Check if file exists */
		FILE *test = fopen (path, "rb");
		exists = (test != NULL);
		if (test) {
			fclose (test);
		}
		if (exists && (flags & ZIP_EXCL)) {
			if (errorp) {
				*errorp = -1; /* file exists but EXCL set */
			}
			free (za);
			return NULL;
		}
		if (exists && !(flags & ZIP_TRUNCATE)) {
			/* Open existing for append */
			mode = "r+b";
		} else {
			/* Create new or truncate */
			mode = "w+b";
		}
		za->mode = 1; /* write mode */
		za->next_index = 0;
	} else {
		/* Read-only mode */
		mode = "rb";
		za->mode = 0;
	}
	if (strchr (mode, 'w')) {
		unlink (path);
		// mkrdir
		creat (path, 0644);
	}

	FILE *fp = fopen (path, mode);
	if (!fp) {
		if (errorp) {
			*errorp = -1;
		}
		free (za);
		return NULL;
	}
	za->fp = fp;
	if (za->mode == 0 || (exists && !(flags & ZIP_TRUNCATE))) {
		/* Load central directory for existing archive */
		if (mzip_load_central (za) != 0) {
			zip_close (za);
			if (errorp) {
				*errorp = -1;
			}
			return NULL;
		}

		/* Set next_index for append mode */
		if (za->mode == 1) {
			za->next_index = za->n_entries;
		}
	}
	if (errorp) {
		*errorp = 0;
	}
	return za;
}

/* Helper function to compress data using various compression methods */
static int mzip_compress_data(uint8_t *in_buf, size_t in_size, uint8_t **out_buf, uint32_t *out_size, uint16_t method) {
	*out_buf = NULL;
	*out_size = 0;

#ifdef MZIP_ENABLE_STORE
	if (method == MZIP_METHOD_STORE) {
		/* Store (no compression) */
		*out_buf = (uint8_t*)malloc(in_size);
		if (!*out_buf) {
			return -1;
		}
		memcpy (*out_buf, in_buf, in_size);
		*out_size = (uint32_t)in_size;
		return 0;
	}
#endif

#ifdef MZIP_ENABLE_DEFLATE
	if (method == MZIP_METHOD_DEFLATE) {
		/* Deflate compression */
		uLong comp_bound = compressBound(in_size);
		*out_buf = (uint8_t*)malloc(comp_bound);
		if (!*out_buf) {
			return -1;
		}
		z_stream strm = {0};
		deflateInit2 (&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
		strm.next_in = in_buf;
		strm.avail_in = in_size;
		strm.next_out = *out_buf;
		strm.avail_out = comp_bound;

		if (deflate (&strm, Z_FINISH) != Z_STREAM_END) {
			deflateEnd (&strm);
			free (*out_buf);
			*out_buf = NULL;
			return -1;
		}
		*out_size = strm.total_out;
		deflateEnd (&strm);

		/* If compression didn't reduce size, fall back to STORE */
		if (*out_size >= in_size) {
			free (*out_buf);
			method = MZIP_METHOD_STORE;
			return mzip_compress_data (in_buf, in_size, out_buf, out_size, MZIP_METHOD_STORE);
		}
		return 0;
	}
#endif

#ifdef MZIP_ENABLE_LZ4
	if (method == MZIP_METHOD_LZ4) {
		/* LZ4 compression using radare2's implementation */
		*out_buf = (uint8_t*)malloc (in_size * 2); /* Worst case scenario */
		if (!*out_buf) {
			return -1;
		}
		int comp_size = r_lz4_compress (*out_buf, in_buf, in_size, 9); /* Use max compression level */
		if (comp_size <= 0) {
			free (*out_buf);
			*out_buf = NULL;
			return -1;
		}
		*out_size = comp_size;
		/* If compression didn't reduce size, fall back to STORE */
		if (*out_size >= in_size) {
			free (*out_buf);
			method = MZIP_METHOD_STORE;
			return mzip_compress_data(in_buf, in_size, out_buf, out_size, MZIP_METHOD_STORE);
		}

		return 0;
	}
#endif
#ifdef MZIP_ENABLE_LZMA
	if (method == MZIP_METHOD_LZMA) {
		/* LZMA compression */
		*out_buf = (uint8_t*)malloc(in_size * 2); /* Worst case scenario */
		if (!*out_buf) {
			return -1;
		}
		z_stream strm = {0};
		if (lzmaInit (&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
			free (*out_buf);
			return -1;
		}

		strm.next_in = in_buf;
		strm.avail_in = in_size;
		strm.next_out = *out_buf;
		strm.avail_out = in_size * 2;

		int ret = lzmaCompress (&strm, Z_FINISH);
		if (ret != Z_STREAM_END) {
			lzmaEnd (&strm);
			free (*out_buf);
			*out_buf = NULL;
			return -1;
		}

		*out_size = strm.total_out;
		lzmaEnd (&strm);

		/* If compression didn't reduce size, fall back to STORE */
		if (*out_size >= in_size) {
			free (*out_buf);
			method = MZIP_METHOD_STORE;
			return mzip_compress_data (in_buf, in_size, out_buf, out_size, MZIP_METHOD_STORE);
		}
		return 0;
	}
#endif
#ifdef MZIP_ENABLE_BROTLI
	if (method == MZIP_METHOD_BROTLI) {
		/* Brotli compression */
		*out_buf = (uint8_t*)malloc(in_size * 2); /* Worst case scenario */
		if (!*out_buf) {
			return -1;
		}
		z_stream strm = {0};
		if (brotliInit (&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
			free (*out_buf);
			return -1;
		}

		strm.next_in = in_buf;
		strm.avail_in = in_size;
		strm.next_out = *out_buf;
		strm.avail_out = in_size * 2;

		int ret = brotliCompress (&strm, Z_FINISH);
		if (ret != Z_STREAM_END) {
			brotliEnd (&strm);
			free (*out_buf);
			*out_buf = NULL;
			return -1;
		}
		*out_size = strm.total_out;
		brotliEnd (&strm);

		/* If compression didn't reduce size, fall back to STORE */
		if (*out_size >= in_size) {
			free (*out_buf);
			method = MZIP_METHOD_STORE;
			return mzip_compress_data (in_buf, in_size, out_buf, out_size, MZIP_METHOD_STORE);
		}
		return 0;
	}
#endif

	/* Unsupported method or none of the above */
	return -1;
}

/* Add file to ZIP archive */
zip_int64_t zip_file_add(zip_t *za, const char *name, zip_source_t *src, zip_flags_t flags) {
	(void)flags;
	if (!za || !name || !src || za->mode != 1) {
		return -1;
	}
	/* Allocate a new entry */
	struct mzip_entry *new_entries;
	new_entries = realloc (za->entries, (za->n_entries + 1) * sizeof (struct mzip_entry));
	if (!new_entries) {
		return -1;
	}
	za->entries = new_entries;

	/* Set up the new entry */
	struct mzip_entry *e = &za->entries[za->n_entries];
	memset (e, 0, sizeof (struct mzip_entry));

	e->name = strdup (name);
	if (!e->name) {
		return -1;
	}
	/* Store uncompressed by default */
	e->method = 0; 
	e->uncomp_size = (uint32_t)src->len;

	/* Calculate CRC-32 of the uncompressed data */
	e->crc32 = mzip_crc32(0, src->buf, src->len);

	/* Set current time for file timestamp */
	mzip_get_dostime(&e->file_time, &e->file_date);

	/* Set default permissions: 0644 for files */
	e->external_attr = 0100644 << 16; /* S_IFREG | 0644 << 16 */

	/* Get current position for local header offset */
	long current_pos = ftell (za->fp);
	if (current_pos < 0) {
		free (e->name);
		return -1;
	}

	e->local_hdr_ofs = (uint32_t)current_pos;

	/* For now, we just store the data uncompressed */
	uint8_t *comp_buf = NULL;
	uint32_t comp_size = 0;

	/* By default, just store the data */
	if (mzip_compress_data ((uint8_t*)src->buf, src->len, &comp_buf, &comp_size, e->method) != 0) {
		free (e->name);
		return -1;
	}

	e->comp_size = comp_size;

	/* Write local file header */
	mzip_write_local_header (za->fp, e->name, e->method, e->comp_size, e->uncomp_size, e->crc32);

	/* Write compressed data */
	fwrite (comp_buf, 1, comp_size, za->fp);
	free (comp_buf);

	/* Free source data if requested */
	if (src->freep) {
		free ((void*)src->buf);
	}
	free (src);

	/* Increment entry count */
	zip_uint64_t index = za->n_entries;
	za->n_entries++;
	za->next_index = za->n_entries;

	return (zip_int64_t)index;
}

/* Set file compression method */
int zip_set_file_compression(zip_t *za, zip_uint64_t index, zip_int32_t comp, zip_uint32_t comp_flags) {
	(void)comp_flags;

	if (!za || index >= za->n_entries || za->mode != 1) {
		return -1;
	}

	/* Check if the requested compression method is supported */
#ifdef MZIP_ENABLE_STORE
	if (comp == MZIP_METHOD_STORE) {
		/* Store is always supported */
	} 
#endif
#ifdef MZIP_ENABLE_DEFLATE
	else if (comp == MZIP_METHOD_DEFLATE) {
		/* Deflate is supported */
	} 
#endif
#ifdef MZIP_ENABLE_ZSTD
	else if (comp == MZIP_METHOD_ZSTD) {
		/* Zstd is supported */
	} 
#endif
#ifdef MZIP_ENABLE_LZFSE
	else if (comp == MZIP_METHOD_LZFSE) {
		/* LZFSE is supported */
	} 
#endif
#ifdef MZIP_ENABLE_LZ4
	else if (comp == MZIP_METHOD_LZ4) {
		/* LZ4 is supported */
	} 
#endif
#ifdef MZIP_ENABLE_LZMA
	else if (comp == MZIP_METHOD_LZMA) {
		/* LZMA is supported */
	} 
#endif
#ifdef MZIP_ENABLE_BROTLI
	else if (comp == MZIP_METHOD_BROTLI) {
		/* Brotli is supported */
	} 
#endif
	else {
		/* Unsupported compression method */
		return -1;
	}

	/* Set the method for next files that will be added */
	za->entries[index].method = (uint16_t)comp;
	return 0;
}

/* Finalize ZIP file before closing - write central directory */
static int mzip_finalize_archive(zip_t *za) {
	if (!za || !za->fp || za->mode != 1) {
		return -1;
	}
	/* Get offset for central directory */
	long cd_offset = ftell (za->fp);
	if (cd_offset < 0) {
		return -1;
	}
	/* Write central directory headers */
	uint32_t cd_size = 0;
	for (zip_uint64_t i = 0; i < za->n_entries; i++) {
		struct mzip_entry *e = &za->entries[i];
		cd_size += mzip_write_central_header (za->fp, e->name, e->method, 
				e->comp_size, e->uncomp_size, e->crc32, 
				e->local_hdr_ofs, e->file_time, e->file_date, e->external_attr);
	}

	/* Write end of central directory record */
	mzip_write_end_of_central_directory (za->fp, (uint32_t)za->n_entries,
			cd_size, (uint32_t)cd_offset);
	return 0;
}

int zip_close(zip_t *za) {
	if (!za) {
		return -1;
	}
	/* Finalize archive if in write mode */
	if (za->mode == 1) {
		mzip_finalize_archive (za);
	}

	if (za->fp) {
		fclose (za->fp);
	}
	zip_uint64_t i;
	for (i = 0; i < za->n_entries; i++) {
		free (za->entries[i].name);
	}
	free (za->entries);
	free(za);
	return 0;
}

zip_uint64_t zip_get_num_files(zip_t *za) {
	return za ? za->n_entries : 0u;
}

zip_int64_t zip_name_locate(zip_t *za, const char *fname, zip_flags_t flags) {
	(void)flags; /* flags (case sensitivity etc.) not implemented */
	if (!za || !fname) return -1;

	for (zip_uint64_t i = 0; i < za->n_entries; i++) {
		if (strcmp(za->entries[i].name, fname) == 0)
			return (zip_int64_t)i;
	}
	return -1;
}

zip_file_t *zip_fopen_index(zip_t *za, zip_uint64_t index, zip_flags_t flags) {
	(void)flags;
	if (!za || index >= za->n_entries) {
		return NULL;
	}
	uint8_t  *buf;
	uint32_t  sz;
	if (mzip_extract_entry (za, &za->entries[index], &buf, &sz) != 0) {
		return NULL;
	}
	zip_file_t *zf = (zip_file_t*)malloc(sizeof(zip_file_t));
	if (!zf) {
		free (buf);
		return NULL;
	}
	zf->data = buf;
	zf->size = sz;
	return zf;
}

int zip_fclose(zip_file_t *zf) {
	if (!zf) return -1;
	free(zf->data);
	free(zf);
	return 0;
}

/* Helper function to write local file header */
static uint32_t mzip_write_local_header(FILE *fp, const char *name, uint32_t comp_method, 
		uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32) {
	uint16_t filename_len = (uint16_t)strlen(name);
	uint8_t header[30];

	/* Write local file header signature */
	mzip_wr32 (header, MZIP_SIG_LFH);

	/* Version needed to extract (2.0) */
	mzip_wr16 (header + 4, 20);

	/* General purpose bit flag */
	mzip_wr16 (header + 6, 0);

	/* Compression method */
	mzip_wr16 (header + 8, comp_method);

	/* Last mod file time & date */
	uint16_t file_time, file_date;
	mzip_get_dostime(&file_time, &file_date);
	mzip_wr16 (header + 10, file_time);
	mzip_wr16 (header + 12, file_date);

	/* CRC-32 */
	mzip_wr32 (header + 14, crc32);

	/* Compressed size */
	mzip_wr32 (header + 18, comp_size);

	/* Uncompressed size */
	mzip_wr32 (header + 22, uncomp_size);

	/* File name length */
	mzip_wr16 (header + 26, filename_len);

	/* Extra field length */
	mzip_wr16 (header + 28, 0);

	/* Write header */
	fwrite (header, 1, sizeof (header), fp);

	/* Write filename */
	fwrite (name, 1, filename_len, fp);

	return 30 + filename_len;
}

/* Helper function to write central directory header */
static uint32_t mzip_write_central_header(FILE *fp, const char *name, uint32_t comp_method,
		uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32,
		uint32_t local_header_offset, uint16_t file_time, uint16_t file_date, uint32_t external_attr) {
	uint16_t filename_len = (uint16_t)strlen (name);
	uint8_t header[46];

	/* Central directory file header signature */
	mzip_wr32 (header, MZIP_SIG_CDH);

	/* Version made by (UNIX, version 2.0) */
	mzip_wr16 (header + 4, 0x031e); 

	/* Version needed to extract (2.0) */
	mzip_wr16 (header + 6, 20);

	/* General purpose bit flag */
	mzip_wr16 (header + 8, 0);

	/* Compression method */
	mzip_wr16 (header + 10, comp_method);

	/* Last mod file time & date */
	mzip_wr16 (header + 12, file_time);
	mzip_wr16 (header + 14, file_date);

	/* CRC-32 */
	mzip_wr32 (header + 16, crc32);

	/* Compressed size */
	mzip_wr32 (header + 20, comp_size);

	/* Uncompressed size */
	mzip_wr32 (header + 24, uncomp_size);

	/* File name length */
	mzip_wr16 (header + 28, filename_len);

	/* Extra field length */
	mzip_wr16 (header + 30, 0);

	/* File comment length */
	mzip_wr16 (header + 32, 0);

	/* Disk number start */
	mzip_wr16 (header + 34, 0);

	/* Internal file attributes */
	mzip_wr16 (header + 36, 0);

	/* External file attributes */
	mzip_wr32 (header + 38, external_attr);

	/* Relative offset of local header */
	mzip_wr32 (header + 42, local_header_offset);

	/* Write header */
	fwrite (header, 1, sizeof (header), fp);

	/* Write filename */
	fwrite (name, 1, filename_len, fp);

	return 46 + filename_len;
}

/* Helper function to write end of central directory record */
static void mzip_write_end_of_central_directory(FILE *fp, uint32_t num_entries, 
		uint32_t central_dir_size, uint32_t central_dir_offset) {
	uint8_t eocd[22];

	/* End of central directory signature */
	mzip_wr32 (eocd, MZIP_SIG_EOCD);

	/* Number of this disk */
	mzip_wr16 (eocd + 4, 0);

	/* Number of the disk with the start of the central directory */
	mzip_wr16 (eocd + 6, 0);

	/* Total number of entries in the central directory on this disk */
	mzip_wr16 (eocd + 8, num_entries);

	/* Total number of entries in the central directory */
	mzip_wr16 (eocd + 10, num_entries);

	/* Size of the central directory */
	mzip_wr32 (eocd + 12, central_dir_size);

	/* Offset of start of central directory with respect to the starting disk number */
	mzip_wr32 (eocd + 16, central_dir_offset);

	/* .ZIP file comment length */
	mzip_wr16 (eocd + 20, 0);

	/* Write end of central directory record */
	fwrite (eocd, 1, sizeof (eocd), fp);
}

zip_source_t *zip_source_buffer(zip_t *za, const void *data, zip_uint64_t len, int freep) {
	(void)za;
	zip_source_t *src = (zip_source_t*)malloc (sizeof (zip_source_t));
	src->buf = data;
	src->len = len;
	src->freep = freep;
	return src;
}
