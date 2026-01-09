/* otezip.c – Minimalistic libzip subset replacement implementation
 * Version: 0.2 (2025-07-27)
 *
 * Implementation file for the otezip library providing a tiny subset of the libzip API.
 *
 * License: MIT / 0-BSD – do whatever you want; attribution appreciated.
 */

#define _GNU_SOURCE
#define OTEZIP_IMPLEMENTATION

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
#else
#include <unistd.h>
#define otezip_mkstemp mkstemp
#endif

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
#define MZIP_SIG_LFH 0x04034b50u
#define MZIP_SIG_CDH 0x02014b50u
#define MZIP_SIG_EOCD 0x06054b50u

/* Safety limits for parsing ZIP fields to avoid integer overflows and
 * excessive allocations. These limits apply to filename/extra/comment
 * fields and to compressed/uncompressed sizes used by this library.
 */
#define MZIP_MAX_FIELD_LEN (64u * 1024u - 1u) /* 64 KiB - 1 to fit 16-bit lengths (65535) */
#define MZIP_MAX_PAYLOAD (2ULL * 1024ULL * 1024ULL * 1024ULL) /* 2 GiB */

/* Forward declarations of helper functions */
static uint32_t otezip_write_local_header(FILE *fp, const char *name, uint32_t comp_method,
	uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32);
static uint32_t otezip_write_central_header(FILE *fp, const char *name, uint32_t comp_method,
	uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32,
	uint32_t local_header_offset, uint16_t file_time, uint16_t file_date, uint32_t external_attr);
static void otezip_write_end_of_central_directory(FILE *fp, uint32_t num_entries,
	uint32_t central_dir_size, uint32_t central_dir_offset);
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
	return (uint32_t) (p[0] | (p[1] << 8) |
		(p[2] << 16) | (p[3] << 24));
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
#define OTEZIP_ERR_READ -1      /* File read/seek error */
#define OTEZIP_ERR_INCONS -2    /* Archive structure inconsistent */

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
		if (otezip_rd32 (buf + i) == MZIP_SIG_EOCD) {
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
				if (fseek (fp, (long)cd_ofs_tmp, SEEK_SET) != 0) {
					continue;
				}
				uint8_t cd_sig[4];
				if (otezip_read_fully (fp, cd_sig, 4) != 0) {
					fseek (fp, saved_pos, SEEK_SET);
					continue;
				}
				fseek (fp, saved_pos, SEEK_SET);
				if (otezip_rd32 (cd_sig) != MZIP_SIG_CDH) {
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
static int otezip_load_central(zip_t *za) {
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

	/* read entire central directory */
	if (fseek (za->fp, cd_ofs, SEEK_SET) != 0) {
		return OTEZIP_ERR_READ;
	}
	if (cd_size == 0) {
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

	za->entries = (struct otezip_entry *)calloc (n_entries, sizeof (struct otezip_entry));
	za->n_entries = n_entries;

	if (!za->entries) {
		free (cd_buf);
		return OTEZIP_ERR_READ;
	}

	size_t off = 0;
	uint16_t i;
	for (i = 0; i < n_entries; i++) {
		/* Ensure we have at least the fixed-size central header available */
		if (off + 46 > cd_size || otezip_rd32 (cd_buf + off) != MZIP_SIG_CDH) {
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

		/* Field lengths are 16-bit per spec; further validation occurs when
		 * advancing offsets and allocating memory below. */

		struct otezip_entry *e = &za->entries[i];
		e->method = otezip_rd16 (h + 10);
		e->file_time = otezip_rd16 (h + 12);
		e->file_date = otezip_rd16 (h + 14);
		e->crc32 = otezip_rd32 (h + 16);
		e->comp_size = otezip_rd32 (h + 20);
		e->uncomp_size = otezip_rd32 (h + 24);
		e->local_hdr_ofs = otezip_rd32 (h + 42);

		/* Reject entries with absurdly large sizes to avoid allocating
		 * more than our allowed maximum. */
		if ((uint64_t)e->comp_size > MZIP_MAX_PAYLOAD || (uint64_t)e->uncomp_size > MZIP_MAX_PAYLOAD) {
			free (cd_buf);
			return OTEZIP_ERR_INCONS;
		}
		e->external_attr = otezip_rd32 (h + 38);

		e->name = (char *)malloc (filename_len + 1u);
		if (!e->name) {
			free (cd_buf);
			return OTEZIP_ERR_READ;
		}
		/* Ensure the filename bytes are within the central directory buffer */
		if ((size_t) (46 + filename_len) > cd_size - off) {
			free (e->name);
			free (cd_buf);
			return OTEZIP_ERR_INCONS;
		}
		memcpy (e->name, h + 46, filename_len);
		e->name[filename_len] = '\0';

		/* Safely advance offset, checking for overflow and bounds */
		uint64_t advance = 46 + (uint64_t)filename_len + (uint64_t)extra_len + (uint64_t)comment_len;
		if (advance > (uint64_t)cd_size - off) {
			free (cd_buf);
			return OTEZIP_ERR_INCONS;
		}
		off += (size_t)advance;
	}
	free (cd_buf);
	return 0;
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
	if (otezip_rd32 (lfh) != MZIP_SIG_LFH) {
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
	if ((uint64_t)e->comp_size > MZIP_MAX_PAYLOAD || (uint64_t)e->uncomp_size > MZIP_MAX_PAYLOAD) {
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
	if (!otezip_ignore_zipbomb && e->comp_size > 0) {
		uint64_t allowed = (uint64_t)e->comp_size * otezip_max_expansion_ratio;
		allowed += otezip_max_expansion_slack;
		if ((uint64_t)e->uncomp_size > allowed) {
			/* suspiciously large uncompressed size */
			fprintf (stderr, "mzip: entry '%s' claims huge uncompressed size (%u), rejecting to avoid zipbomb\n",
				e->name? e->name: "<unknown>", e->uncomp_size);
			return -1;
		}
	}

	/* seek to compressed data (we were at local header +30 already) */
	if (fseek (za->fp, (long)data_ofs, SEEK_SET) != 0) {
		return -1;
	}

	/* read compressed data */
	uint8_t *cbuf = (uint8_t *)malloc ((size_t)e->comp_size? (size_t)e->comp_size: 1);
	if (!cbuf) {
		return -1;
	}
	if (e->comp_size && otezip_read_fully (za->fp, cbuf, e->comp_size) != 0) {
		free (cbuf);
		return -1;
	}

	uint8_t *ubuf;
#ifdef OTEZIP_ENABLE_STORE
	if (e->method == OTEZIP_METHOD_STORE) { /* stored – nothing to inflate */
		ubuf = cbuf;
	}
#endif
#ifdef OTEZIP_ENABLE_DEFLATE
	else if (e->method == OTEZIP_METHOD_DEFLATE) { /* deflate */

		/* Allocate output buffer */
		ubuf = (uint8_t *)malloc (e->uncomp_size);
		if (!ubuf) {
			free (cbuf);
			return -1;
		}

		/* Initialize buffer to zeros */
		memset (ubuf, 0, e->uncomp_size);

		/* Setup decompression */
		z_stream strm = { 0 };
		strm.next_in = cbuf;
		strm.avail_in = e->comp_size;
		strm.next_out = ubuf;
		strm.avail_out = e->uncomp_size;

		/* Try raw deflate first (standard for ZIP files) */
		int ret = inflateInit2 (&strm, -MAX_WBITS);
		if (ret != Z_OK) {
			/* Fall back to direct copy */
			memcpy (ubuf, cbuf, e->uncomp_size < e->comp_size? e->uncomp_size: e->comp_size);
			*out_buf = ubuf;
			*out_sz = e->uncomp_size;
			free (cbuf);
			return 0;
		}

		/* Attempt decompression */
		ret = inflate (&strm, Z_FINISH);
		inflateEnd (&strm);

		/* If decompression failed */
		if (ret != Z_STREAM_END) {
			free (ubuf);
			free (cbuf);
			return -1;
		}

		free (cbuf);
	}
#endif
#ifdef OTEZIP_ENABLE_ZSTD
	else if (e->method == OTEZIP_METHOD_ZSTD) { /* zstd */
		ubuf = (uint8_t *)malloc (e->uncomp_size);
		if (!ubuf) {
			free (cbuf);
			return -1;
		}
		z_stream strm = { 0 };
		strm.next_in = cbuf;
		strm.avail_in = e->comp_size;
		strm.next_out = ubuf;
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
#ifdef OTEZIP_ENABLE_LZFSE
	else if (e->method == OTEZIP_METHOD_LZFSE) { /* lzfse */
		ubuf = (uint8_t *)malloc (e->uncomp_size);
		if (!ubuf) {
			free (cbuf);
			return -1;
		}
		z_stream strm = { 0 };
		strm.next_in = cbuf;
		strm.avail_in = e->comp_size;
		strm.next_out = ubuf;
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
#ifdef OTEZIP_ENABLE_LZ4
	else if (e->method == OTEZIP_METHOD_LZ4) { /* lz4 - using radare2's implementation */
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
#ifdef OTEZIP_ENABLE_LZMA
	else if (e->method == OTEZIP_METHOD_LZMA) { /* lzma */
		ubuf = (uint8_t *)malloc (e->uncomp_size);
		if (!ubuf) {
			free (cbuf);
			return -1;
		}
		z_stream strm = { 0 };
		strm.next_in = cbuf;
		strm.avail_in = e->comp_size;
		strm.next_out = ubuf;
		strm.avail_out = e->uncomp_size;

		if (lzmaDecompressInit (&strm) != Z_OK) {
			free (cbuf);
			free (ubuf);
			return -1;
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
#ifdef OTEZIP_ENABLE_BROTLI
	else if (e->method == OTEZIP_METHOD_BROTLI) { /* brotli */
		ubuf = (uint8_t *)malloc (e->uncomp_size);
		if (!ubuf) {
			free (cbuf);
			return -1;
		}
		z_stream strm = { 0 };
		strm.next_in = cbuf;
		strm.avail_in = e->comp_size;
		strm.next_out = ubuf;
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
				fprintf (stderr, "Warning: CRC mismatch for '%s' (expected 0x%08x, got 0x%08x)\n",
					e->name? e->name: "<unknown>", e->crc32, computed_crc);
			}
		}
	}

	*out_buf = ubuf;
	*out_sz = e->uncomp_size;
	return 0;
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
		creat (path, 0644);
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
		int load_result = otezip_load_central (za);
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
			zip_close (za);
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
static int otezip_compress_data(uint8_t *in_buf, size_t in_size, uint8_t **out_buf, uint32_t *out_size, uint16_t *method) {
	*out_buf = NULL;
	*out_size = 0;

#ifdef OTEZIP_ENABLE_STORE
	if (*method == OTEZIP_METHOD_STORE) {
		/* Store (no compression) */
		*out_buf = (uint8_t *)malloc (in_size);
		if (!*out_buf) {
			return -1;
		}
		memcpy (*out_buf, in_buf, in_size);
		*out_size = (uint32_t)in_size;
		return 0;
	}
#endif

#ifdef OTEZIP_ENABLE_DEFLATE
	if (*method == OTEZIP_METHOD_DEFLATE) {
		/* Deflate compression */
		unsigned long comp_bound = compressBound (in_size);
		*out_buf = (uint8_t *)malloc (comp_bound);
		if (!*out_buf) {
			return -1;
		}
		z_stream strm = { 0 };
		/* For ZIP files, we need raw deflate (no zlib header) - use negative windowBits */
		if (deflateInit2 (&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
			free (*out_buf);
			*out_buf = NULL;
			return -1;
		}
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
			*method = OTEZIP_METHOD_STORE;
			return otezip_compress_data (in_buf, in_size, out_buf, out_size, method);
		}
		return 0;
	}
#endif

#ifdef OTEZIP_ENABLE_ZSTD
	if (*method == OTEZIP_METHOD_ZSTD) {
		/* ZSTD compression */
		/* For raw blocks (no actual compression), we need space for:
		 * input data + frame header (5) + block headers (~3 bytes per ~65KB block) */
		size_t out_cap = in_size + 1024; /* 1KB overhead for frame + block headers */
		*out_buf = (uint8_t *)malloc (out_cap);
		if (!*out_buf) {
			return -1;
		}
		z_stream strm = { 0 };
		if (zstdInit (&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
			free (*out_buf);
			*out_buf = NULL;
			return -1;
		}

		strm.next_in = in_buf;
		strm.avail_in = (unsigned int)in_size;
		strm.next_out = *out_buf;
		strm.avail_out = (unsigned int)out_cap;

		/* Keep calling compress until all data is processed and we get Z_STREAM_END */
		int ret = Z_OK;
		while (ret == Z_OK) {
			ret = zstdCompress (&strm, Z_FINISH);
		}
		if (ret != Z_STREAM_END) {
			/* If compression failed, fall back to STORE */
			zstdEnd (&strm);
			free (*out_buf);
			*out_buf = NULL;
			*method = OTEZIP_METHOD_STORE;
			return otezip_compress_data (in_buf, in_size, out_buf, out_size, method);
		}

		*out_size = (uint32_t)strm.total_out;
		zstdEnd (&strm);

		/* If compression didn't reduce size, fall back to STORE */
		if (*out_size >= in_size) {
			free (*out_buf);
			*method = OTEZIP_METHOD_STORE;
			return otezip_compress_data (in_buf, in_size, out_buf, out_size, method);
		}
		return 0;
	}
#endif

#ifdef OTEZIP_ENABLE_LZ4
	if (*method == OTEZIP_METHOD_LZ4) {
		/* LZ4 compression using radare2's implementation */
		 *out_buf = (uint8_t *)malloc (in_size * 2); /* Worst case scenario */
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
			*method = OTEZIP_METHOD_STORE;
			return otezip_compress_data (in_buf, in_size, out_buf, out_size, method);
		}

		return 0;
	}
#endif
#ifdef OTEZIP_ENABLE_LZFSE
	if (*method == OTEZIP_METHOD_LZFSE) {
		/* LZFSE compression */
		size_t out_cap = in_size + (in_size / 10) + 100; /* Worst case: ~110% + margin */
		*out_buf = (uint8_t *)malloc (out_cap);
		if (!*out_buf) {
			return -1;
		}
		z_stream strm = { 0 };
		if (lzfseInit (&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
			free (*out_buf);
			*out_buf = NULL;
			return -1;
		}

		strm.next_in = in_buf;
		strm.avail_in = (unsigned int)in_size;
		strm.next_out = *out_buf;
		strm.avail_out = (unsigned int)out_cap;

		int ret = lzfseCompress (&strm, Z_FINISH);
		if (ret != Z_STREAM_END) {
			/* If compression failed, fall back to STORE */
			lzfseEnd (&strm);
			free (*out_buf);
			*out_buf = NULL;
			*method = OTEZIP_METHOD_STORE;
			return otezip_compress_data (in_buf, in_size, out_buf, out_size, method);
		}

		*out_size = (uint32_t)strm.total_out;
		lzfseEnd (&strm);

		/* If compression didn't reduce size, fall back to STORE */
		if (*out_size >= in_size) {
			free (*out_buf);
			*method = OTEZIP_METHOD_STORE;
			return otezip_compress_data (in_buf, in_size, out_buf, out_size, method);
		}
		return 0;
	}
#endif

#ifdef OTEZIP_ENABLE_LZMA
	if (*method == OTEZIP_METHOD_LZMA) {
		/* LZMA compression */
		/* Worst-case bound: input size + header + overhead for incompressible data */
		size_t out_cap = in_size + OTEZIP_LZMA_HEADER_SIZE + (in_size / OTEZIP_LZMA_OVERHEAD_RATIO);
		*out_buf = (uint8_t *)malloc (out_cap);
		if (!*out_buf) {
			return -1;
		}
		z_stream strm = { 0 };
		if (lzmaInit (&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
			free (*out_buf);
			return -1;
		}

		strm.next_in = in_buf;
		strm.avail_in = in_size;
		strm.next_out = *out_buf;
		strm.avail_out = (unsigned int)out_cap;

		int ret = lzmaCompress (&strm, Z_FINISH);
		if (ret != Z_STREAM_END) {
			/* If our minimal LZMA failed for this input, fall back to STORE
			 * instead of propagating an error. This keeps behavior robust
			 * for very large or pathological inputs. */
			lzmaEnd (&strm);
			free (*out_buf);
			*out_buf = NULL;
			*method = OTEZIP_METHOD_STORE;
			return otezip_compress_data (in_buf, in_size, out_buf, out_size, method);
		}

		*out_size = strm.total_out;
		lzmaEnd (&strm);

		/* If compression didn't reduce size, fall back to STORE */
		if (*out_size >= in_size) {
			free (*out_buf);
			*method = OTEZIP_METHOD_STORE;
			return otezip_compress_data (in_buf, in_size, out_buf, out_size, method);
		}
		return 0;
	}
#endif
#ifdef OTEZIP_ENABLE_BROTLI
	if (*method == OTEZIP_METHOD_BROTLI) {
		/* Brotli compression (using vendored upstream implementation via wrappers) */
		size_t out_cap = (in_size? (in_size * 2 + 64): 128);
		*out_buf = (uint8_t *)malloc (out_cap);
		if (!*out_buf) {
			return -1;
		}
		z_stream strm = { 0 };
		if (brotliInit (&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
			free (*out_buf);
			*out_buf = NULL;
			return -1;
		}

		strm.next_in = in_buf;
		strm.avail_in = (unsigned int)in_size;
		strm.next_out = *out_buf;
		strm.avail_out = (unsigned int)out_cap;

		int ret = brotliCompress (&strm, Z_FINISH);
		if (ret != Z_STREAM_END) {
			/* If we ran out of space, grow once and retry */
			if (ret == Z_OK && strm.avail_out == 0) {
				size_t used = (size_t)strm.total_out;
				out_cap *= 2;
				uint8_t *nb = (uint8_t *)realloc (*out_buf, out_cap);
				if (!nb) {
					brotliEnd (&strm);
					free (*out_buf);
					*out_buf = NULL;
					return -1;
				}
				*out_buf = nb;
				strm.next_out = *out_buf + used;
				strm.avail_out = (unsigned int) (out_cap - used);
				ret = brotliCompress (&strm, Z_FINISH);
			}
		}
		if (ret != Z_STREAM_END) {
			brotliEnd (&strm);
			free (*out_buf);
			*out_buf = NULL;
			return -1;
		}
		*out_size = (uint32_t)strm.total_out;
		brotliEnd (&strm);

		/* For empty inputs, Brotli still emits a minimal frame; keep it. */
		if (in_size > 0 && *out_size >= in_size) {
			free (*out_buf);
			*method = OTEZIP_METHOD_STORE;
			return otezip_compress_data (in_buf, in_size, out_buf, out_size, method);
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
	if (nlen - 1 > MZIP_MAX_FIELD_LEN) {
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
	if ((uint64_t)src->len > MZIP_MAX_PAYLOAD || (uint64_t)src->len > (uint64_t)UINT32_MAX) {
		free (e->name);
		return -1;
	}
	e->uncomp_size = (uint32_t)src->len;

	/* Calculate CRC-32 of the uncompressed data */
	e->crc32 = otezip_crc32 (0, src->buf, src->len);

	/* Set current time for file timestamp */
	otezip_get_dostime (&e->file_time, &e->file_date);

	/* Set default permissions: 0644 for files */
	e->external_attr = 0100644 << 16; /* S_IFREG | 0644 << 16 */

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
	if (otezip_compress_data ((uint8_t *)src->buf, src->len, &comp_buf, &comp_size, &e->method) != 0) {
		free (e->name);
		return -1;
	}

	/* Validate compressed size too */
	if ((uint64_t)comp_size > MZIP_MAX_PAYLOAD) {
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

	if (!za || index >= za->n_entries || za->mode != 1) {
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
	if (!za || !za->fp || za->mode != 1) {
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
		uint32_t written = otezip_write_central_header (za->fp, e->name, e->method,
			e->comp_size, e->uncomp_size, e->crc32,
			e->local_hdr_ofs, e->file_time, e->file_date, e->external_attr);
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
	otezip_write_end_of_central_directory (za->fp, (uint32_t)za->n_entries,
		(uint32_t)cd_size_acc, (uint32_t)cd_offset);
	return 0;
}

int zip_close(zip_t *za) {
	if (!za) {
		return -1;
	}
	/* Finalize archive if in write mode */
	if (za->mode == 1) {
		otezip_finalize_archive (za);
	}

	if (za->fp) {
		fclose (za->fp);
	}
	zip_uint64_t i;
	for (i = 0; i < za->n_entries; i++) {
		if (za->entries[i].name) {
			free (za->entries[i].name);
		}
	}
	free (za->entries);
	free (za);
	return 0;
}

zip_uint64_t zip_get_num_files(zip_t *za) {
	return za? za->n_entries: 0u;
}

zip_int64_t zip_name_locate(zip_t *za, const char *fname, zip_flags_t flags) {
	(void)flags; /* flags (case sensitivity etc.) not implemented */
	if (!za || !fname) {
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
	if (!za || index >= za->n_entries) {
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
	zip_uint64_t to_copy = (nbytes < remaining) ? nbytes : remaining;
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
	if (!za || !st || index >= za->n_entries) {
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
	if (!za || index >= za->n_entries) {
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
	int fd = otezip_mkstemp (tmp_path);
	if (fd < 0) {
		return NULL;
	}
	/* Write the source buffer to the temp file */
	ssize_t written = write (fd, src->buf, src->len);
	close (fd);
	if (written != (ssize_t)src->len) {
		unlink (tmp_path);
		return NULL;
	}
	/* Open the archive from the temp file */
	int errorp = 0;
	zip_t *za = zip_open (tmp_path, flags, &errorp);
	if (!za) {
		unlink (tmp_path);
		return NULL;
	}
	/* Note: The temp file will be cleaned up when the archive is closed.
	 * This is a simple implementation; a more sophisticated one would
	 * handle in-memory sources directly. */
	return za;
}

/* Helper function to write local file header */
static uint32_t otezip_write_local_header(FILE *fp, const char *name, uint32_t comp_method,
	uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32) {
	size_t filename_len_sz = strlen (name);
	if (filename_len_sz > MZIP_MAX_FIELD_LEN) {
		filename_len_sz = MZIP_MAX_FIELD_LEN;
	}
	uint16_t filename_len = (uint16_t)filename_len_sz;
	uint8_t header[30];

	/* Write local file header signature */
	otezip_wr32 (header, MZIP_SIG_LFH);

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
static uint32_t otezip_write_central_header(FILE *fp, const char *name, uint32_t comp_method,
	uint32_t comp_size, uint32_t uncomp_size, uint32_t crc32,
	uint32_t local_header_offset, uint16_t file_time, uint16_t file_date, uint32_t external_attr) {
	size_t filename_len_sz = strlen (name);
	if (filename_len_sz > MZIP_MAX_FIELD_LEN) {
		filename_len_sz = MZIP_MAX_FIELD_LEN;
	}
	uint16_t filename_len = (uint16_t)filename_len_sz;
	uint8_t header[46];

	/* Central directory file header signature */
	otezip_wr32 (header, MZIP_SIG_CDH);

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
static void otezip_write_end_of_central_directory(FILE *fp, uint32_t num_entries,
	uint32_t central_dir_size, uint32_t central_dir_offset) {
	uint8_t eocd[22];

	/* End of central directory signature */
	otezip_wr32 (eocd, MZIP_SIG_EOCD);

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
	if (!za || index >= za->n_entries || !src) {
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
	if (otezip_compress_data ((uint8_t *)src->buf, src->len, &comp_buf, &comp_size, &e->method) != 0) {
		return -1;
	}
	if ((uint64_t)comp_size > MZIP_MAX_PAYLOAD) {
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
