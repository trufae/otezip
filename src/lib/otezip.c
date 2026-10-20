/* otezip.c – Minimalistic libzip subset replacement implementation
 * Version: 0.2 (2025-07-27)
 *
 * Implementation file for the otezip library providing a tiny subset of the libzip API.
 *
 * License: MIT / 0-BSD – do whatever you want; attribution appreciated.
 */

#define _GNU_SOURCE
#define OTEZIP_IMPLEMENTATION

/* Suppress MSVC deprecation warnings for POSIX functions on Windows */
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#include <direct.h>
#define unlink _unlink
#define close _close
#define write _write
#define creat _creat
#ifndef ssize_t
#define ssize_t int
#endif
static int otezip_mkstemp(char *template) {
	if (_mktemp_s (template, strlen (template) + 1) != 0) {
		return -1;
	}
	return _open (template, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
}
#elif defined(__wasi__)
#include <unistd.h>
static int otezip_mkstemp(const char *prefix) {
	char path[64];
	for (int i = 0; i < 10000; i++) {
		snprintf (path, sizeof (path), "%s_%d.tmp", prefix, i);
		int fd = open (path, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			return fd; // success
		}
	}
	return -1;
}
#else
#include <unistd.h>
#define otezip_mkstemp mkstemp
#endif

/* Cut the file at the current stream position */
static int otezip_truncate_here(FILE *fp) {
	long pos = ftell (fp);
	if (pos < 0 || fflush (fp) != 0) {
		return -1;
	}
#if defined(_WIN32) || defined(_WIN64)
	return _chsize_s (_fileno (fp), (__int64)pos) == 0? 0: -1;
#else
	return ftruncate (fileno (fp), (off_t)pos);
#endif
}

#include "../include/otezip/zip.h"
#include "../include/otezip/zstream.h"
#include "time.inc.c"

#if defined(_WIN32) || defined(_WIN64)
/* Ensure we have thread-safe fallback for localtime on Windows builds */
#include <time.h>
#endif

#if OTEZIP_ENABLE_LZ4
#include <r_util.h>
#endif

#include "crc32.inc.c"
/* Include compression algorithms based on config */

/* Pull in deflate implementation */
#define MDEFLATE_IMPLEMENTATION
#include "deflate.inc.c"

#if OTEZIP_ENABLE_ZSTD
#include "zstd.inc.c"
#endif
#if OTEZIP_ENABLE_LZFSE
#include "lzfse.inc.c"
#endif
#if OTEZIP_ENABLE_LZMA
#include "lzma.inc.c"
#endif
#if OTEZIP_ENABLE_BROTLI
#include "brotli.inc.c"
#endif

/* Local/central directory signatures */
#define OTEZIP_SIG_LFH 0x04034b50u
#define OTEZIP_SIG_CDH 0x02014b50u
#define OTEZIP_SIG_EOCD 0x06054b50u

/* Safety limits for parsing ZIP fields to avoid integer overflows and
 * excessive allocations. These limits apply to filename/extra/comment
 * fields and to compressed/uncompressed sizes used by this library.
 */
#define OTEZIP_MAX_FIELD_LEN (64u * 1024u - 1u) /* 64 KiB - 1 to fit 16-bit lengths (65535) */
#define OTEZIP_MAX_PAYLOAD (2ULL * 1024ULL * 1024ULL * 1024ULL) /* 2 GiB */

/* Forward declarations of helper functions */
static uint32_t otezip_write_local_header(FILE *fp, const char *name, uint32_t comp_method, uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32);
static uint32_t otezip_write_central_header(FILE *fp, const char *name, uint32_t comp_method, uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32, uint32_t local_header_offset, uint16_t file_time, uint16_t file_date, uint32_t external_attr);
static void otezip_write_end_of_central_directory(FILE *fp, uint32_t num_entries, uint32_t central_dir_size, uint32_t central_dir_offset);
static int otezip_finalize_archive(zip_t *za);

/* Helper function to get compression method ID from string name.
 * Returns the OTEZIP_METHOD_* value or -1 if invalid/not supported. */
int otezip_method_from_string(const char *method_name) {
	if (!method_name) {
		return -1;
	}

	struct method_map {
		const char *name;
		int method;
	};

	static const struct method_map methods[] = {
#ifdef OTEZIP_ENABLE_STORE
		{ "store", OTEZIP_METHOD_STORE },
#endif
#ifdef OTEZIP_ENABLE_DEFLATE
		{ "deflate", OTEZIP_METHOD_DEFLATE },
#endif
#ifdef OTEZIP_ENABLE_ZSTD
		{ "zstd", OTEZIP_METHOD_ZSTD },
#endif
#ifdef OTEZIP_ENABLE_LZMA
		{ "lzma", OTEZIP_METHOD_LZMA },
#endif
#ifdef OTEZIP_ENABLE_LZ4
		{ "lz4", OTEZIP_METHOD_LZ4 },
#endif
#ifdef OTEZIP_ENABLE_BROTLI
		{ "brotli", OTEZIP_METHOD_BROTLI },
#endif
#ifdef OTEZIP_ENABLE_LZFSE
		{ "lzfse", OTEZIP_METHOD_LZFSE },
#endif
		{ NULL, -1 } /* sentinel */
	};

	for (size_t i = 0; methods[i].name != NULL; ++i) {
		if (strcmp (method_name, methods[i].name) == 0) {
			return methods[i].method;
		}
	}

	return -1;
}

/* Global flag: when non-zero, verify CRC32 on extraction and fail on mismatch. */
int otezip_verify_crc = 0;

/* Defaults for expansion protection: allow up to 1000x expansion plus 1 MiB slack.
 * These are conservative defaults to prevent OOM from malicious archives while
 * allowing reasonable compression ratios for typical files. The CLI utility
 * can set `otezip_ignore_zipbomb` to bypass these checks when the user explicitly
 * requests so (see main.c flag). */
uint64_t otezip_max_expansion_ratio = 1000ULL;
uint64_t otezip_max_expansion_slack = 1024ULL * 1024ULL; /* 1 MiB */
int otezip_ignore_zipbomb = 0;

/* helper: little-endian readers/writers (ZIP format is little-endian) */

static uint16_t otezip_rd16(const uint8_t *p) {
	return (uint16_t) (p[0] | (p[1] << 8));
}
static uint32_t otezip_rd32(const uint8_t *p) {
	return (uint32_t) ((unsigned int)p[0] | ((unsigned int)p[1] << 8) |
		((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24));
}
static void otezip_wr16(uint8_t *p, uint16_t v) {
	p[0] = (uint8_t) (v & 0xFF);
	p[1] = (uint8_t) ((v >> 8) & 0xFF);
}
static void otezip_wr32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t) (v & 0xFF);
	p[1] = (uint8_t) ((v >> 8) & 0xFF);
	p[2] = (uint8_t) ((v >> 16) & 0xFF);
	p[3] = (uint8_t) ((v >> 24) & 0xFF);
}

/* ----  internal helpers  ---- */

/* Return codes for error handling */
#define OTEZIP_ERR_READ -1 /* File read/seek error */
#define OTEZIP_ERR_INCONS -2 /* Archive structure inconsistent */

static inline int otezip_read_fully(FILE *fp, void *dst, size_t n) {
	return fread (dst, 1, n, fp) == n? 0: -1;
}

/* locate EOCD record (last 64KiB + 22 bytes) */
static long otezip_find_eocd(FILE *fp, uint8_t *eocd_out /*22+*/, size_t *cd_size, uint32_t *cd_ofs, uint16_t *total_entries) {
	long file_size;
	if (fseek (fp, 0, SEEK_END) != 0) {
		return OTEZIP_ERR_READ;
	}
	file_size = ftell (fp);
	if (file_size < 22) {
		return OTEZIP_ERR_INCONS;
	}
	const size_t max_back = 0x10000 + 22; /* spec: comment <= 65535 */
	/* Ensure both operands of the?: have the same unsigned type to avoid
	 * signed/unsigned conversions. If file_size is smaller than max_back,
	 * use file_size cast to size_t, otherwise use max_back. */
	size_t search_len = (file_size < (long)max_back)? (size_t)file_size: max_back;

	if (fseek (fp, file_size - (long)search_len, SEEK_SET) != 0) {
		return OTEZIP_ERR_READ;
	}
	uint8_t buf[65558];
	if (otezip_read_fully (fp, buf, search_len) != 0) {
		return OTEZIP_ERR_READ;
	}
	size_t i;
	for (i = search_len - 22; i != (size_t)-1; --i) {
		if (otezip_rd32 (buf + i) == OTEZIP_SIG_EOCD) {
			/* Basic EOCD present; extract fields but validate them before
			 * returning to avoid trusting potentially corrupted archives. */
			memcpy (eocd_out, buf + i, 22);
			uint16_t comment_len = otezip_rd16 (buf + i + 20);
			uint16_t entries = otezip_rd16 (buf + i + 10);
			uint32_t cd_size_tmp = otezip_rd32 (buf + i + 12);
			uint32_t cd_ofs_tmp = otezip_rd32 (buf + i + 16);
			(void)comment_len;

			/* Ensure central directory lies within the file. Use 64-bit
			 * arithmetic to avoid overflow when adding offsets. */
			uint64_t cd_end = (uint64_t)cd_ofs_tmp + (uint64_t)cd_size_tmp;
			if (cd_ofs_tmp > (uint32_t)file_size || cd_end > (uint64_t)file_size) {
				/* Central directory claims to be outside the file -> malformed */
				continue;
			}

			/* Validate that the central directory starts with a valid CDH signature.
			 * This handles files with embedded EOCD signatures in compressed data. */
			if (entries > 0 && cd_size_tmp >= 4) {
				long saved_pos = ftell (fp);
				if (saved_pos < 0) {
					continue;
				}
				if (fseek (fp, (long)cd_ofs_tmp, SEEK_SET) != 0) {
					continue;
				}
				uint8_t cd_sig[4];
				if (otezip_read_fully (fp, cd_sig, 4) != 0) {
					(void)fseek (fp, saved_pos, SEEK_SET);
					continue;
				}
				(void)fseek (fp, saved_pos, SEEK_SET);
				if (otezip_rd32 (cd_sig) != OTEZIP_SIG_CDH) {
					/* CD doesn't start with valid header - try next EOCD candidate */
					continue;
				}
			}

			/* Everything looked sane; commit results */
			*total_entries = entries;
			*cd_size = cd_size_tmp;
			*cd_ofs = cd_ofs_tmp;
			return (long) (file_size - search_len + i);
		}
	}
	/* not found */
	return OTEZIP_ERR_INCONS;
}

/* parse central directory into array of otezip_entry */
typedef struct {
	uint64_t start;
	uint64_t end;
} otezip_span;

static int otezip_span_cmp(const void *a, const void *b) {
	const otezip_span *x = (const otezip_span *)a;
	const otezip_span *y = (const otezip_span *)b;
	return (x->start > y->start) - (x->start < y->start);
}

/* Reject entries sharing bytes: overlapping local headers or data let a tiny
 * compressed kernel be extracted many times (overlapping-file zip bombs).
 * Each span is a lower bound (30-byte header + compressed data, ignoring the
 * name and extra fields), so valid archives are never rejected. */
static int otezip_check_overlap(const zip_t *za) {
	if (otezip_ignore_zipbomb || za->n_entries < 2) {
		return 0;
	}
	otezip_span *spans = (otezip_span *)malloc (za->n_entries * sizeof (otezip_span));
	if (!spans) {
		return OTEZIP_ERR_READ;
	}
	zip_uint64_t i;
	for (i = 0; i < za->n_entries; i++) {
		spans[i].start = za->entries[i].local_hdr_ofs;
		spans[i].end = spans[i].start + 30u + za->entries[i].comp_size;
	}
	qsort (spans, za->n_entries, sizeof (otezip_span), otezip_span_cmp);
	int ret = 0;
	for (i = 1; i < za->n_entries; i++) {
		if (spans[i].start < spans[i - 1].end) {
			ret = OTEZIP_ERR_INCONS;
			break;
		}
	}
	free (spans);
	return ret;
}

/* Parse the central directory; *cd_ofs_out gets its offset, where appended
 * entries must start */
static int otezip_load_central(zip_t *za, uint32_t *cd_ofs_out) {
	uint8_t eocd[22];
	size_t cd_size;
	uint32_t cd_ofs;
	uint16_t n_entries;
	long eocd_pos;

	eocd_pos = otezip_find_eocd (za->fp, eocd, &cd_size, &cd_ofs, &n_entries);
	if (eocd_pos < 0) {
		/* Return the specific error code from find_eocd */
		return (int)eocd_pos;
	}

	/* Validate central directory against actual file size to avoid
	 * out-of-bounds reads or huge allocations. */
	if (fseek (za->fp, 0, SEEK_END) != 0) {
		return OTEZIP_ERR_READ;
	}
	long file_size_long = ftell (za->fp);
	if (file_size_long < 0) {
		return OTEZIP_ERR_READ;
	}
	uint64_t file_size = (uint64_t)file_size_long;
	if ((uint64_t)cd_ofs + (uint64_t)cd_size > file_size) {
		return OTEZIP_ERR_INCONS;
	}

	/* A valid empty ZIP may contain only an EOCD record with an empty
	 * central directory. Accept that case without trying to read or parse
	 * any central directory entries. */
	if (n_entries == 0) {
		if (cd_size != 0) {
			return OTEZIP_ERR_INCONS;
		}
		za->entries = NULL;
		za->n_entries = 0;
		*cd_ofs_out = cd_ofs;
		return 0;
	}

	/* read entire central directory */
	if (fseek (za->fp, cd_ofs, SEEK_SET) != 0) {
		return OTEZIP_ERR_READ;
	}
	/* Validate central directory size isn't unreasonably large */
	if (cd_size > OTEZIP_MAX_PAYLOAD) {
		return OTEZIP_ERR_INCONS;
	}
	uint8_t *cd_buf = (uint8_t *)malloc (cd_size);
	if (!cd_buf) {
		return OTEZIP_ERR_READ;
	}
	if (otezip_read_fully (za->fp, cd_buf, cd_size) != 0) {
		free (cd_buf);
		return OTEZIP_ERR_READ;
	}

	/* Validate number of entries is reasonable (max ~1.4M entries fits in cd_size) */
	if ((size_t)n_entries * 46 > cd_size) {
		free (cd_buf);
		return OTEZIP_ERR_INCONS;
	}

	za->entries = (struct otezip_entry *)calloc (n_entries, sizeof (struct otezip_entry));
	if (!za->entries) {
		free (cd_buf);
		return OTEZIP_ERR_READ;
	}
	za->n_entries = n_entries;

	size_t off = 0;
	uint16_t i;
	for (i = 0; i < n_entries; i++) {
		/* Ensure we have at least the fixed-size central header available */
		if (off + 46 > cd_size || otezip_rd32 (cd_buf + off) != OTEZIP_SIG_CDH) {
			free (cd_buf);
			return OTEZIP_ERR_INCONS; /* malformed */
		}
		const uint8_t *h = cd_buf + off;

		/* Data descriptors (general purpose bit 3): when set, the local
		 * file header has CRC/sizes zeroed and actual values follow the
		 * compressed data. However, the central directory always stores
		 * the correct values, so we can safely use those. No need to reject. */

		uint16_t filename_len = otezip_rd16 (h + 28);
		uint16_t extra_len = otezip_rd16 (h + 30);
		uint16_t comment_len = otezip_rd16 (h + 32);
		size_t entry_size = 46 + (size_t)filename_len + (size_t)extra_len + (size_t)comment_len;

		if (entry_size > cd_size - off) {
			free (cd_buf);
			return OTEZIP_ERR_INCONS;
		}

		struct otezip_entry *e = &za->entries[i];
		e->method = otezip_rd16 (h + 10);
		e->file_time = otezip_rd16 (h + 12);
		e->file_date = otezip_rd16 (h + 14);
		e->crc32 = otezip_rd32 (h + 16);
		e->comp_size = otezip_rd32 (h + 20);
		e->uncomp_size = otezip_rd32 (h + 24);
		e->local_hdr_ofs = otezip_rd32 (h + 42);
		e->external_attr = otezip_rd32 (h + 38);

		if ((uint64_t)e->comp_size > OTEZIP_MAX_PAYLOAD || (uint64_t)e->uncomp_size > OTEZIP_MAX_PAYLOAD) {
			free (cd_buf);
			return OTEZIP_ERR_INCONS;
		}

		e->name = (char *)malloc (filename_len + 1u);
		if (!e->name) {
			free (cd_buf);
			return OTEZIP_ERR_READ;
		}
		memcpy (e->name, h + 46, filename_len);
		e->name[filename_len] = '\0';
		off += entry_size;
	}
	free (cd_buf);
	int overlap = otezip_check_overlap (za);
	if (overlap != 0) {
		return overlap;
	}
	*cd_ofs_out = cd_ofs;
	return 0;
}

/* --------------  codec table  --------------- */

/* Each method exposes zlib-style init/run/end calls for both directions.
 * Wrappers adapt the few init functions that take extra arguments. */
typedef struct {
	uint16_t method;
	int (*dec_init)(z_stream *strm);
	int (*dec)(z_stream *strm, int flush);
	int (*dec_end)(z_stream *strm);
	int (*enc_init)(z_stream *strm);
	int (*enc)(z_stream *strm, int flush);
	int (*enc_end)(z_stream *strm);
	size_t (*enc_bound)(size_t in_size); /* output buffer size for in_size bytes */
} otezip_codec;

#ifdef OTEZIP_ENABLE_DEFLATE
static int otezip_inflate_init(z_stream *strm) {
	return inflateInit2 (strm, -MAX_WBITS); /* raw deflate, as stored in ZIP */
}
static int otezip_deflate_init(z_stream *strm) {
	return deflateInit2 (strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
}
static size_t otezip_deflate_bound(size_t n) {
	return compressBound ((unsigned long)n);
}
#endif
#ifdef OTEZIP_ENABLE_ZSTD
static int otezip_zstd_init(z_stream *strm) {
	return zstdInit (strm, Z_DEFAULT_COMPRESSION);
}
static size_t otezip_zstd_bound(size_t n) {
	return n + 1024; /* frame header plus block headers */
}
#endif
#ifdef OTEZIP_ENABLE_LZFSE
static int otezip_lzfse_init(z_stream *strm) {
	return lzfseInit (strm, Z_DEFAULT_COMPRESSION);
}
static size_t otezip_lzfse_bound(size_t n) {
	return n + n / 10 + 100;
}
#endif
#ifdef OTEZIP_ENABLE_LZMA
static int otezip_lzma_init(z_stream *strm) {
	return lzmaInit (strm, Z_DEFAULT_COMPRESSION);
}
static size_t otezip_lzma_bound(size_t n) {
	return n + OTEZIP_LZMA_HEADER_SIZE + n / OTEZIP_LZMA_OVERHEAD_RATIO;
}
#endif
#ifdef OTEZIP_ENABLE_BROTLI
static int otezip_brotli_init(z_stream *strm) {
	return brotliInit (strm, Z_DEFAULT_COMPRESSION);
}
static size_t otezip_brotli_bound(size_t n) {
	return n * 2 + 64;
}
#endif

static const otezip_codec otezip_codecs[] = {
#ifdef OTEZIP_ENABLE_DEFLATE
	{ OTEZIP_METHOD_DEFLATE, otezip_inflate_init, inflate, inflateEnd, otezip_deflate_init, deflate, deflateEnd, otezip_deflate_bound },
#endif
#ifdef OTEZIP_ENABLE_ZSTD
	{ OTEZIP_METHOD_ZSTD, zstdDecompressInit, zstdDecompress, zstdDecompressEnd, otezip_zstd_init, zstdCompress, zstdEnd, otezip_zstd_bound },
#endif
#ifdef OTEZIP_ENABLE_LZFSE
	{ OTEZIP_METHOD_LZFSE, lzfseDecompressInit, lzfseDecompress, lzfseDecompressEnd, otezip_lzfse_init, lzfseCompress, lzfseEnd, otezip_lzfse_bound },
#endif
#ifdef OTEZIP_ENABLE_LZMA
	{ OTEZIP_METHOD_LZMA, lzmaDecompressInit, lzmaDecompress, lzmaDecompressEnd, otezip_lzma_init, lzmaCompress, lzmaEnd, otezip_lzma_bound },
#endif
#ifdef OTEZIP_ENABLE_BROTLI
	{ OTEZIP_METHOD_BROTLI, brotliDecompressInit, brotliDecompress, brotliDecompressEnd, otezip_brotli_init, brotliCompress, brotliEnd, otezip_brotli_bound },
#endif
	{ 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL } /* sentinel */
};

static const otezip_codec *otezip_codec_find(uint16_t method) {
	for (const otezip_codec *c = otezip_codecs; c->dec; c++) {
		if (c->method == method) {
			return c;
		}
	}
	return NULL;
}

/* Decompress exactly out_size bytes; returns 0 on success */
static int otezip_decompress(const otezip_codec *c, const uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t out_size) {
	z_stream strm = { 0 };
	strm.next_in = in;
	strm.avail_in = in_size;
	strm.next_out = out;
	strm.avail_out = out_size;
	if (c->dec_init (&strm) != Z_OK) {
		return -1;
	}
	int ret = c->dec (&strm, Z_FINISH);
	c->dec_end (&strm);
	return (ret == Z_STREAM_END && strm.total_out == out_size)? 0: -1;
}

/* Compress with codec c. Returns the compressed size and sets *out, or 0 if
 * the encoder failed or did not make the data smaller (caller stores it). */
static uint32_t otezip_compress_with(const otezip_codec *c, const uint8_t *in, size_t in_size, uint8_t **out) {
	size_t cap = c->enc_bound (in_size);
	uint8_t *buf = (uint8_t *)malloc (cap);
	if (!buf) {
		return 0;
	}
	z_stream strm = { 0 };
	if (c->enc_init (&strm) != Z_OK) {
		free (buf);
		return 0;
	}
	strm.next_in = in;
	strm.avail_in = (uInt)in_size;
	strm.next_out = buf;
	strm.avail_out = (uInt)cap;
	int ret;
	for (;;) {
		uLong in_before = strm.total_in;
		uLong out_before = strm.total_out;
		ret = c->enc (&strm, Z_FINISH);
		/* Every bound is >= in_size, so a full buffer means no gain either */
		if (ret != Z_OK || strm.total_out >= in_size ||
			(strm.total_in == in_before && strm.total_out == out_before)) {
			break;
		}
	}
	uint32_t size = (ret == Z_STREAM_END && strm.total_out < in_size)? (uint32_t)strm.total_out: 0;
	c->enc_end (&strm);
	if (!size) {
		free (buf);
		return 0;
	}
	*out = buf;
	return size;
}

/* load entire (uncompressed) file into memory and hand ownership to caller */
static int otezip_extract_entry(zip_t *za, struct otezip_entry *e, uint8_t **out_buf, uint32_t *out_sz) {
	/* move to local header */
	/* Validate local header offset against file size to avoid seeking
	 * outside the file. Use 64-bit math for safety. */
	if (fseek (za->fp, 0, SEEK_END) != 0) {
		return -1;
	}
	long file_sz_l = ftell (za->fp);
	if (file_sz_l < 0) {
		return -1;
	}
	uint64_t file_sz = (uint64_t)file_sz_l;
	if ((uint64_t)e->local_hdr_ofs > file_sz) {
		return -1;
	}
	if (fseek (za->fp, (long)e->local_hdr_ofs, SEEK_SET) != 0) {
		return -1;
	}
	uint8_t lfh[30];
	if (otezip_read_fully (za->fp, lfh, 30) != 0) {
		return -1;
	}
	if (otezip_rd32 (lfh) != OTEZIP_SIG_LFH) {
		return -1;
	}

	/* Data descriptors (bit 3): when set, the local file header stores
	 * CRC/sizes as 0 and actual values appear after compressed data.
	 * We rely on the central directory values (stored in entry e) which
	 * are always correct, so we can safely proceed. */
	uint16_t fn_len = otezip_rd16 (lfh + 26);
	uint16_t extra_len = otezip_rd16 (lfh + 28);

	/* Filename/extra lengths are 16-bit per spec. Further bounds checks
	 * are performed for file offsets and allocations below. */

	/* Ensure the compressed data lies within the file bounds. Calculate
	 * offset to compressed data = local_hdr_ofs + 30 + fn_len + extra_len. */
	uint64_t data_ofs = (uint64_t)e->local_hdr_ofs + 30ULL + (uint64_t)fn_len + (uint64_t)extra_len;
	if (data_ofs > file_sz) {
		return -1;
	}
	if ((uint64_t)e->comp_size > OTEZIP_MAX_PAYLOAD || (uint64_t)e->uncomp_size > OTEZIP_MAX_PAYLOAD) {
		return -1;
	}
	if (data_ofs + (uint64_t)e->comp_size > file_sz) {
		return -1;
	}

	/* Protect against zipbombs: require that expected uncompressed size from
	 * the central directory is within a reasonable bound relative to the
	 * compressed size. If the entry claims a huge expansion, fail unless the
	 * global `otezip_ignore_zipbomb` flag is set by the caller (CLI override).
	 * We compute allowed = comp_size * ratio + slack and compare against the
	 * declared uncompressed size. Use 64-bit math to avoid overflow. */
	if (!otezip_ignore_zipbomb) {
		uint64_t allowed = (uint64_t)e->comp_size * otezip_max_expansion_ratio;
		allowed += otezip_max_expansion_slack;
		if ((uint64_t)e->uncomp_size > allowed) {
			/* suspiciously large uncompressed size */
			fprintf (stderr, "mzip: entry '%s' claims huge uncompressed size (%u), rejecting to avoid zipbomb\n", e->name? e->name: "<unknown>", e->uncomp_size);
			return -1;
		}
	}

	/* seek to compressed data (we were at local header +30 already) */
	if (fseek (za->fp, (long)data_ofs, SEEK_SET) != 0) {
		return -1;
	}

	/* read compressed data */
	uint8_t *cbuf = (uint8_t *)calloc (e->comp_size ? e->comp_size : 1, 1);
	if (!cbuf) {
		return -1;
	}
	if (e->comp_size && otezip_read_fully (za->fp, cbuf, e->comp_size) != 0) {
		free (cbuf);
		return -1;
	}

	uint8_t *ubuf = NULL;
	if (e->method == OTEZIP_METHOD_STORE) {
		if (e->comp_size != e->uncomp_size) {
			free (cbuf);
			return -1;
		}
		ubuf = cbuf;
		cbuf = NULL;
	}
#ifdef OTEZIP_ENABLE_LZ4
	else if (e->method == OTEZIP_METHOD_LZ4) { /* lz4 - using radare2's implementation */
		size_t output_size = 0;
		ubuf = r_lz4_decompress (cbuf, e->comp_size, &output_size);
		if (!ubuf || output_size != e->uncomp_size) {
			free (cbuf);
			free (ubuf);
			return -1;
		}
	}
#endif
	else {
		const otezip_codec *c = otezip_codec_find (e->method);
		if (c) {
			ubuf = (uint8_t *)malloc (e->uncomp_size? e->uncomp_size: 1);
		}
		if (!ubuf || otezip_decompress (c, cbuf, e->comp_size, ubuf, e->uncomp_size) != 0) {
			free (ubuf);
			free (cbuf);
			return -1; /* unsupported method or corrupt data */
		}
	}
	free (cbuf);
	/* Verify CRC32 of uncompressed data if requested or warn on mismatch. */
	{
		uint32_t computed_crc = otezip_crc32 (0, ubuf, e->uncomp_size);
		if (computed_crc != e->crc32) {
			if (otezip_verify_crc) {
				/* On strict verify, treat mismatch as fatal for this entry. */
				free (ubuf);
				return -1;
			} else {
				/* Non-strict mode: warn but continue. */
				fprintf (stderr, "Warning: CRC mismatch for '%s' (expected 0x%08x, got 0x%08x)\n", e->name? e->name: "<unknown>", e->crc32, computed_crc);
			}
		}
	}

	*out_buf = ubuf;
	*out_sz = e->uncomp_size;
	return 0;
}

/* Helper function to validate archive state */
static int otezip_is_valid(const zip_t *za) {
	return (za != NULL && za->fp != NULL);
}

/* --------------  public API implementation  --------------- */

zip_t *zip_open(const char *path, int flags, int *errorp) {
	zip_t *za = (zip_t *)calloc (1, sizeof (zip_t));
	const char *mode;
	int exists = 0;

	if (!za) {
		if (errorp) {
			*errorp = -1;
		}
		return NULL;
	}

	/* Initialize structure */
	za->default_method = 0; /* Default to store */

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
		if (exists && ! (flags & ZIP_TRUNCATE)) {
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
	}

	FILE *fp = fopen (path, mode);
	if (!fp) {
		if (errorp) {
			*errorp = ZIP_ER_OPEN;
		}
		free (za);
		return NULL;
	}
	za->fp = fp;
	if (za->mode == 0 || (exists && ! (flags & ZIP_TRUNCATE))) {
		/* Load central directory for existing archive */
		uint32_t cd_ofs = 0;
		int load_result = otezip_load_central (za, &cd_ofs);
		if (load_result != 0) {
			/* Convert internal error codes to libzip error codes */
			if (errorp) {
				if (load_result == OTEZIP_ERR_READ) {
					*errorp = ZIP_ER_READ;
				} else if (load_result == OTEZIP_ERR_INCONS) {
					*errorp = ZIP_ER_INCONS;
				} else {
					 *errorp = ZIP_ER_NOZIP; /* Unknown error - not a zip */
				}
			}
			/* Never finalize a half-loaded archive: entries may lack names and
			 * writing a central directory would corrupt the existing file */
			za->mode = 0;
			zip_close (za);
			return NULL;
		}

		if (za->mode == 1) {
			/* New entries overwrite the old central directory, which is
			 * rewritten after them on close. This seek is also required
			 * by C before writing to a stream that was last read. */
			if (fseek (za->fp, (long)cd_ofs, SEEK_SET) != 0) {
				if (errorp) {
					*errorp = ZIP_ER_READ;
				}
				za->mode = 0;
				zip_close (za);
				return NULL;
			}
			za->next_index = za->n_entries;
		}
	}
	if (errorp) {
		*errorp = 0;
	}
	return za;
}

#ifdef OTEZIP_ENABLE_LZ4
/* LZ4 via radare2; same contract as otezip_compress_with */
static uint32_t otezip_compress_lz4(const uint8_t *in, size_t in_size, uint8_t **out) {
	uint8_t *buf = (uint8_t *)malloc (in_size * 2); /* Worst case scenario */
	if (!buf) {
		return 0;
	}
	int size = r_lz4_compress (buf, in, in_size, 9); /* Use max compression level */
	if (size <= 0 || (size_t)size >= in_size) {
		free (buf);
		return 0;
	}
	*out = buf;
	return (uint32_t)size;
}
#endif

/* Compress in_buf with *method. Falls back to STORE (updating *method) for
 * empty input or when compression fails or does not make the data smaller. */
static int otezip_compress_data(const uint8_t *in_buf, size_t in_size, uint8_t **out_buf, uint32_t *out_size, uint16_t *method) {
	*out_buf = NULL;
	*out_size = 0;
	if (in_size > 0 && *method != OTEZIP_METHOD_STORE) {
		uint32_t size = 0;
#ifdef OTEZIP_ENABLE_LZ4
		if (*method == OTEZIP_METHOD_LZ4) {
			size = otezip_compress_lz4 (in_buf, in_size, out_buf);
		} else
#endif
		{
			const otezip_codec *c = otezip_codec_find (*method);
			if (!c) {
				return -1; /* unsupported method */
			}
			size = otezip_compress_with (c, in_buf, in_size, out_buf);
		}
		if (size) {
			*out_size = size;
			return 0;
		}
	}
	*out_buf = (uint8_t *)malloc (in_size? in_size: 1);
	if (!*out_buf) {
		return -1;
	}
	if (in_size) {
		memcpy (*out_buf, in_buf, in_size);
	}
	*out_size = (uint32_t)in_size;
	*method = OTEZIP_METHOD_STORE;
	return 0;
}

/* Add file to ZIP archive */
zip_int64_t zip_file_add(zip_t *za, const char *name, zip_source_t *src, zip_flags_t flags) {
	(void)flags;
	if (!za || !name || !src || za->mode != 1) {
		return -1;
	}
	/* Allocate a new entry */
	struct otezip_entry *new_entries;
	new_entries = realloc (za->entries, (za->n_entries + 1) * sizeof (struct otezip_entry));
	if (!new_entries) {
		return -1;
	}
	za->entries = new_entries;

	/* Set up the new entry */
	struct otezip_entry *e = &za->entries[za->n_entries];
	memset (e, 0, sizeof (struct otezip_entry));

	/* strdup is POSIX; allocate and copy to be portable and avoid
	 * implicit declaration warnings. */
	size_t nlen = strlen (name) + 1;
	if (nlen - 1 > OTEZIP_MAX_FIELD_LEN) {
		return -1;
	}
	e->name = (char *)malloc (nlen);
	if (!e->name) {
		return -1;
	}
	memcpy (e->name, name, nlen);

	/* Use the default compression method if set, otherwise store */
	if (za->default_method > 0) {
		e->method = za->default_method;
	} else {
		/* Store uncompressed by default */
		e->method = 0;
	}

	/* Validate uncompressed size fits our limits and ZIP 32-bit field */
	if ((uint64_t)src->len > OTEZIP_MAX_PAYLOAD || (uint64_t)src->len > (uint64_t)UINT32_MAX) {
		free (e->name);
		return -1;
	}
	e->uncomp_size = (uint32_t)src->len;

	/* Calculate CRC-32 of the uncompressed data */
	e->crc32 = otezip_crc32 (0, src->buf, src->len);

	/* Set current time for file timestamp */
	otezip_get_dostime (&e->file_time, &e->file_date);

	/* Set default permissions: 0644 for files */
	e->external_attr = (uint32_t)0100644 << 16; /* S_IFREG | 0644, unsigned: 0100644 << 16 overflows int */

	/* Get current position for local header offset */
	long current_pos = ftell (za->fp);
	if (current_pos < 0) {
		free (e->name);
		return -1;
	}

	/* Ensure local header offset fits into ZIP 32-bit field */
	if ((uint64_t)current_pos > (uint64_t)UINT32_MAX) {
		free (e->name);
		return -1;
	}
	e->local_hdr_ofs = (uint32_t)current_pos;

	/* Compress the data using the selected method */
	uint8_t *comp_buf = NULL;
	uint32_t comp_size = 0;

	/* Compress the data using the selected method */
	if (otezip_compress_data ((const uint8_t *)src->buf, src->len, &comp_buf, &comp_size, &e->method) != 0) {
		free (e->name);
		return -1;
	}

	/* Validate compressed size too */
	if ((uint64_t)comp_size > OTEZIP_MAX_PAYLOAD) {
		free (e->name);
		free (comp_buf);
		return -1;
	}
	e->comp_size = comp_size;

	/* Write local file header */
	otezip_write_local_header (za->fp, e->name, e->method, e->comp_size, e->uncomp_size, e->crc32);

	/* Write compressed data */
	fwrite (comp_buf, 1, comp_size, za->fp);
	free (comp_buf);

	/* Free source data if requested */
	if (src->freep) {
		free ((void *)src->buf);
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

	if (!otezip_is_valid (za) || index >= za->n_entries || za->mode != 1) {
		return -1;
	}

	/* Check if the requested compression method is supported */
#ifdef OTEZIP_ENABLE_STORE
	if (comp == OTEZIP_METHOD_STORE) {
		/* Store is always supported */
	}
#endif
#ifdef OTEZIP_ENABLE_DEFLATE
	else if (comp == OTEZIP_METHOD_DEFLATE) {
		/* Deflate is supported */
	}
#endif
#ifdef OTEZIP_ENABLE_ZSTD
	else if (comp == OTEZIP_METHOD_ZSTD) {
		/* Zstd is supported */
	}
#endif
#ifdef OTEZIP_ENABLE_LZFSE
	else if (comp == OTEZIP_METHOD_LZFSE) {
		/* LZFSE is supported */
	}
#endif
#ifdef OTEZIP_ENABLE_LZ4
	else if (comp == OTEZIP_METHOD_LZ4) {
		/* LZ4 is supported */
	}
#endif
#ifdef OTEZIP_ENABLE_LZMA
	else if (comp == OTEZIP_METHOD_LZMA) {
		/* LZMA is supported */
	}
#endif
#ifdef OTEZIP_ENABLE_BROTLI
	else if (comp == OTEZIP_METHOD_BROTLI) {
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
static int otezip_finalize_archive(zip_t *za) {
	if (!otezip_is_valid (za) || !za->fp || za->mode != 1) {
		return -1;
	}
	/* Get offset for central directory */
	long cd_offset = ftell (za->fp);
	if (cd_offset < 0) {
		return -1;
	}
	/* Write central directory headers. Accumulate in 64-bit and ensure
	 * the final size and offset fit into the 32-bit EOCD fields before
	 * writing them. */
	uint64_t cd_size_acc = 0;
	for (zip_uint64_t i = 0; i < za->n_entries; i++) {
		struct otezip_entry *e = &za->entries[i];
		uint32_t written = otezip_write_central_header (za->fp, e->name, e->method, e->comp_size, e->uncomp_size, e->crc32, e->local_hdr_ofs, e->file_time, e->file_date, e->external_attr);
		cd_size_acc += written;
		if (cd_size_acc > (uint64_t)UINT32_MAX) {
			/* central directory too large for ZIP32 EOCD */
			return -1;
		}
	}

	/* Ensure central directory offset fits into 32-bit */
	if ((uint64_t)cd_offset > (uint64_t)UINT32_MAX) {
		return -1;
	}

	/* Write end of central directory record */
	otezip_write_end_of_central_directory (za->fp, (uint32_t)za->n_entries, (uint32_t)cd_size_acc, (uint32_t)cd_offset);
	/* Drop leftovers of a longer previous archive (e.g. an old comment) */
	return otezip_truncate_here (za->fp);
}

int zip_close(zip_t *za) {
	if (!otezip_is_valid (za)) {
		free (za);
		return -1;
	}
	/* Finalize archive if in write mode */
	int finalize_ret = 0;
	if (za->mode == 1) {
		finalize_ret = otezip_finalize_archive (za);
	}

	if (za->fp) {
		if (fclose (za->fp) != 0 && finalize_ret == 0) {
			finalize_ret = -1;
		}
	}
	zip_uint64_t i;
	for (i = 0; i < za->n_entries; i++) {
		if (za->entries[i].name) {
			free (za->entries[i].name);
		}
	}
	free (za->entries);
	free (za);
	return finalize_ret;
}

zip_uint64_t zip_get_num_files(zip_t *za) {
	return za? za->n_entries: 0u;
}

zip_int64_t zip_name_locate(zip_t *za, const char *fname, zip_flags_t flags) {
	(void)flags; /* flags (case sensitivity etc.) not implemented */
	if (!otezip_is_valid (za) || !fname) {
		return -1;
	}

	for (zip_uint64_t i = 0; i < za->n_entries; i++) {
		if (strcmp (za->entries[i].name, fname) == 0) {
			return (zip_int64_t)i;
		}
	}
	return -1;
}

zip_file_t *zip_fopen_index(zip_t *za, zip_uint64_t index, zip_flags_t flags) {
	(void)flags;
	if (!otezip_is_valid (za) || index >= za->n_entries) {
		return NULL;
	}
	uint8_t *buf = NULL;
	uint32_t sz = 0;
	if (otezip_extract_entry (za, &za->entries[index], &buf, &sz) != 0) {
		return NULL;
	}
	zip_file_t *zf = (zip_file_t *)malloc (sizeof (zip_file_t));
	if (!zf) {
		free (buf);
		return NULL;
	}
	zf->data = buf;
	zf->size = sz;
	zf->pos = 0;
	return zf;
}

int zip_fclose(zip_file_t *zf) {
	if (!zf) {
		return -1;
	}
	free (zf->data);
	free (zf);
	return 0;
}

zip_int64_t zip_fread(zip_file_t *zf, void *buf, zip_uint64_t nbytes) {
	if (!zf || !buf) {
		return -1;
	}
	if (zf->pos >= zf->size) {
		return 0;
	}
	zip_uint64_t remaining = zf->size - zf->pos;
	zip_uint64_t to_copy = (nbytes < remaining)? nbytes: remaining;
	memcpy (buf, (uint8_t *)zf->data + zf->pos, to_copy);
	zf->pos += to_copy;
	return (zip_int64_t)to_copy;
}

void zip_stat_init(zip_stat_t *st) {
	if (!st) {
		return;
	}
	st->valid = 0;
	st->name = NULL;
	st->index = ZIP_UINT64_MAX;
	st->size = 0;
	st->comp_size = 0;
	st->mtime = (time_t)-1;
	st->crc = 0;
	st->comp_method = ZIP_CM_STORE;
}

int zip_stat_index(zip_t *za, zip_uint64_t index, zip_flags_t flags, zip_stat_t *st) {
	(void)flags;
	if (!otezip_is_valid (za) || !st || index >= za->n_entries) {
		return -1;
	}
	zip_stat_init (st);
	struct otezip_entry *e = &za->entries[index];
	st->name = e->name;
	st->index = index;
	st->size = e->uncomp_size;
	st->comp_size = e->comp_size;
	st->crc = e->crc32;
	st->comp_method = e->method;
	st->valid = ZIP_STAT_NAME | ZIP_STAT_INDEX | ZIP_STAT_SIZE | ZIP_STAT_COMP_SIZE | ZIP_STAT_CRC | ZIP_STAT_COMP_METHOD;
	return 0;
}

const char *zip_get_name(zip_t *za, zip_uint64_t index, zip_flags_t flags) {
	(void)flags;
	if (!otezip_is_valid (za) || index >= za->n_entries) {
		return NULL;
	}
	return za->entries[index].name;
}

int zip_stat(zip_t *za, const char *fname, zip_flags_t flags, zip_stat_t *st) {
	zip_int64_t index = zip_name_locate (za, fname, flags);
	if (index < 0) {
		return -1;
	}
	return zip_stat_index (za, (zip_uint64_t)index, flags, st);
}

zip_t *zip_open_from_source(zip_source_t *src, int flags, zip_error_t *error) {
	(void)error;
	if (!src) {
		return NULL;
	}
	/* Since otezip's zip_source_t is a simple buffer wrapper,
	 * we create a temporary file and write the buffer to it,
	 * then open the archive normally. */
	char tmp_path[] = "/tmp/otezip_XXXXXX";
	/* Set restrictive umask before mkstemp for security */
#if defined(__wasi__) || defined(_WIN32) || defined(_WIN64)
	int fd = otezip_mkstemp (tmp_path);
#else
	mode_t old_umask = umask (077);
	int fd = otezip_mkstemp (tmp_path);
	umask (old_umask);
#endif
	if (fd < 0) {
		return NULL;
	}
	/* Write the source buffer to the temp file */
	int written = (int)write (fd, src->buf, (unsigned int)src->len);
	close (fd);
	if ((int)written != (int)src->len) {
		unlink (tmp_path);
		return NULL;
	}
	/* Open the archive from the temp file */
	int errorp = 0;
	zip_t *za = zip_open (tmp_path, flags, &errorp);
	if (!za) {
		unlink (tmp_path);
	}
	return za;
}

/* Helper function to write local file header */
static uint32_t otezip_write_local_header(FILE *fp, const char *name, uint32_t comp_method, uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32) {
	size_t filename_len_sz = strlen (name);
	if (filename_len_sz > OTEZIP_MAX_FIELD_LEN) {
		filename_len_sz = OTEZIP_MAX_FIELD_LEN;
	}
	uint16_t filename_len = (uint16_t)filename_len_sz;
	uint8_t header[30];

	/* Write local file header signature */
	otezip_wr32 (header, OTEZIP_SIG_LFH);

	/* Version needed to extract (2.0) */
	otezip_wr16 (header + 4, 20);

	/* General purpose bit flag */
	otezip_wr16 (header + 6, 0);

	/* Compression method */
	otezip_wr16 (header + 8, comp_method);

	/* Last mod file time & date */
	uint16_t file_time, file_date;
	otezip_get_dostime (&file_time, &file_date);
	otezip_wr16 (header + 10, file_time);
	otezip_wr16 (header + 12, file_date);

	/* CRC-32 */
	otezip_wr32 (header + 14, crc32);

	/* Compressed size */
	otezip_wr32 (header + 18, comp_size);

	/* Uncompressed size */
	otezip_wr32 (header + 22, uncomp_size);

	/* File name length */
	otezip_wr16 (header + 26, filename_len);

	/* Extra field length */
	otezip_wr16 (header + 28, 0);

	/* Write header */
	fwrite (header, 1, sizeof (header), fp);

	/* Write filename */
	fwrite (name, 1, filename_len, fp);

	return 30 + filename_len;
}

/* Helper function to write central directory header */
static uint32_t otezip_write_central_header(FILE *fp, const char *name, uint32_t comp_method, uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32, uint32_t local_header_offset, uint16_t file_time, uint16_t file_date, uint32_t external_attr) {
	size_t filename_len_sz = strlen (name);
	if (filename_len_sz > OTEZIP_MAX_FIELD_LEN) {
		filename_len_sz = OTEZIP_MAX_FIELD_LEN;
	}
	uint16_t filename_len = (uint16_t)filename_len_sz;
	uint8_t header[46];

	/* Central directory file header signature */
	otezip_wr32 (header, OTEZIP_SIG_CDH);

	/* Version made by (UNIX, version 2.0) */
	otezip_wr16 (header + 4, 0x031e);

	/* Version needed to extract (2.0) */
	otezip_wr16 (header + 6, 20);

	/* General purpose bit flag */
	otezip_wr16 (header + 8, 0);

	/* Compression method */
	otezip_wr16 (header + 10, comp_method);

	/* Last mod file time & date */
	otezip_wr16 (header + 12, file_time);
	otezip_wr16 (header + 14, file_date);

	/* CRC-32 */
	otezip_wr32 (header + 16, crc32);

	/* Compressed size */
	otezip_wr32 (header + 20, comp_size);

	/* Uncompressed size */
	otezip_wr32 (header + 24, uncomp_size);

	/* File name length */
	otezip_wr16 (header + 28, filename_len);

	/* Extra field length */
	otezip_wr16 (header + 30, 0);

	/* File comment length */
	otezip_wr16 (header + 32, 0);

	/* Disk number start */
	otezip_wr16 (header + 34, 0);

	/* Internal file attributes */
	otezip_wr16 (header + 36, 0);

	/* External file attributes */
	otezip_wr32 (header + 38, external_attr);

	/* Relative offset of local header */
	otezip_wr32 (header + 42, local_header_offset);

	/* Write header */
	fwrite (header, 1, sizeof (header), fp);

	/* Write filename */
	fwrite (name, 1, filename_len, fp);

	return 46 + filename_len;
}

/* Helper function to write end of central directory record */
static void otezip_write_end_of_central_directory(FILE *fp, uint32_t num_entries, uint32_t central_dir_size, uint32_t central_dir_offset) {
	uint8_t eocd[22];

	/* End of central directory signature */
	otezip_wr32 (eocd, OTEZIP_SIG_EOCD);

	/* Number of this disk */
	otezip_wr16 (eocd + 4, 0);

	/* Number of the disk with the start of the central directory */
	otezip_wr16 (eocd + 6, 0);

	/* Total number of entries in the central directory on this disk */
	otezip_wr16 (eocd + 8, num_entries);

	/* Total number of entries in the central directory */
	otezip_wr16 (eocd + 10, num_entries);

	/* Size of the central directory */
	otezip_wr32 (eocd + 12, central_dir_size);

	/* Offset of start of central directory with respect to the starting disk number */
	otezip_wr32 (eocd + 16, central_dir_offset);

	/* .ZIP file comment length */
	otezip_wr16 (eocd + 20, 0);

	/* Write end of central directory record */
	fwrite (eocd, 1, sizeof (eocd), fp);
}

zip_source_t *zip_source_buffer(zip_t *za, const void *data, zip_uint64_t len, int freep) {
	(void)za;
	zip_source_t *src = (zip_source_t *)malloc (sizeof (zip_source_t));
	if (!src) {
		return NULL; /* data stays owned by the caller */
	}
	src->buf = data;
	src->len = len;
	src->freep = freep;
	return src;
}

zip_source_t *zip_source_buffer_create(const void *data, zip_uint64_t len, int freep, zip_error_t *error) {
	(void)error;
	return zip_source_buffer (NULL, data, len, freep);
}

void zip_source_free(zip_source_t *src) {
	if (!src) {
		return;
	}
	if (src->freep && src->buf) {
		free ((void *)src->buf);
	}
	free (src);
}

/* Helper to replace entry data at an existing index */
static int otezip_replace_entry_data(zip_t *za, zip_uint64_t index, zip_source_t *src) {
	if (!otezip_is_valid (za) || index >= za->n_entries || !src) {
		return -1;
	}
	if (za->mode != 1) {
		return -1;
	}
	struct otezip_entry *e = &za->entries[index];
	/* Update entry with new source data */
	e->uncomp_size = (uint32_t)src->len;
	e->crc32 = otezip_crc32 (0, src->buf, src->len);
	/* Compress the data using the selected method */
	uint8_t *comp_buf = NULL;
	uint32_t comp_size = 0;
	if (otezip_compress_data ((const uint8_t *)src->buf, src->len, &comp_buf, &comp_size, &e->method) != 0) {
		return -1;
	}
	if ((uint64_t)comp_size > OTEZIP_MAX_PAYLOAD) {
		free (comp_buf);
		return -1;
	}
	e->comp_size = comp_size;
	/* Write the updated entry */
	long current_pos = ftell (za->fp);
	if (current_pos < 0) {
		free (comp_buf);
		return -1;
	}
	e->local_hdr_ofs = (uint32_t)current_pos;
	otezip_write_local_header (za->fp, e->name, e->method, e->comp_size, e->uncomp_size, e->crc32);
	fwrite (comp_buf, 1, comp_size, za->fp);
	free (comp_buf);
	return 0;
}

int zip_file_replace(zip_t *za, zip_uint64_t index, zip_source_t *src, zip_flags_t flags) {
	(void)flags;
	if (otezip_replace_entry_data (za, index, src) != 0) {
		return -1;
	}
	return 0;
}

/* Deprecated functions - just forward to the new API */
int zip_replace(zip_t *za, zip_uint64_t index, zip_source_t *src) {
	return zip_file_replace (za, index, src, 0);
}

zip_int64_t zip_add(zip_t *za, const char *name, zip_source_t *src) {
	return zip_file_add (za, name, src, 0);
}
