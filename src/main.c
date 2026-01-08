/* main.c – Tiny demo utility for otezip/zip.h
 * Build:  gcc -std=c99 -DOTEZIP_IMPLEMENTATION main.c -lz -o otezip
 * Usage:  ./mzip -l  archive.zip   # list files
 *         ./mzip -x  archive.zip   # extract into current directory
 *         ./mzip -c  archive.zip file1 file2...  # create new zip archive
 *         ./mzip -a  archive.zip file1 file2...  # add files to existing archive
 *         ./mzip -d  file.gz       # decompress gzip file (gunzip mode)
 *         ./mzip -g  file          # compress file with gzip
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "include/otezip/config.h"
#include "include/otezip/zip.h"
#include "include/otezip/zstream.h"

/* Deflate/inflate API constants and functions from otezip.c */
#define Z_OK 0
#define Z_STREAM_END 1
#define Z_BUF_ERROR (-5)
#define Z_NO_FLUSH 0
#define Z_FINISH 4
#define Z_DEFLATED 8
#define Z_DEFAULT_COMPRESSION (-1)
#define Z_DEFAULT_STRATEGY 0
#define MAX_WBITS 15

extern int inflateInit2(z_stream *strm, int windowBits);
extern int inflate(z_stream *strm, int flush);
extern int inflateEnd(z_stream *strm);
extern int deflateInit2(z_stream *strm, int level, int method, int windowBits,
	int memLevel, int strategy);
extern int deflate(z_stream *strm, int flush);
extern int deflateEnd(z_stream *strm);

#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Force overwrite flag (set via -f / --force) */
static int g_force = 0;

/* Platform compatibility wrappers
 * - mingw/msvc provide mkdir (const char*)/_mkdir and no lstat/S_ISLNK by default.
 * - Provide small wrappers/macros so the rest of the code can use portable names. */
#if defined(_WIN32) || defined(_WIN64)
#define OTEZIP_MKDIR(path, mode) _mkdir (path)
#define OTEZIP_LSTAT(path, buf) stat((path),(buf))
#ifndef OTEZIP_FCHMOD
/* On Windows (MinGW/MSVC) there's no reliable fchmod that maps to POSIX
 * permissions. Setting unix-style permission bits isn't meaningful on NTFS in
 * the same way; failures are non-fatal in extraction code, so make this a
 * no-op that reports success. If a platform provides an fd-based fchmod,
 * callers can override OTEZIP_FCHMOD. */
#define OTEZIP_FCHMOD(fd, mode) (0)
#endif
#ifndef S_ISLNK
#define S_ISLNK(mode) 0
#endif
#else
#define OTEZIP_MKDIR(path, mode) mkdir((path),(mode))
#define OTEZIP_LSTAT(path, buf) lstat((path),(buf))
#define OTEZIP_FCHMOD(fd, mode) fchmod((fd),(mode))
#endif

static void usage(void) {
	puts ("mzip – minimal ZIP reader/writer (mzip.h demo)\n"
	"Usage: mzip [-l | -x | -c | -a | -v | -d | -g] <archive.zip> [files...] [options]\n"
	"  -l   List contents\n"
	"  -x   Extract all files into current directory\n"
	"  -c   Create new archive with specified files\n"
	"  -a   Add files to existing archive\n"
	"  -d   Decompress gzip/deflate file (gunzip mode)\n"
	"  -g   Compress file with gzip (gzip mode)\n"
	"  -v   Show version number\n\n"
	"Options:");

	/* Show compression options based on what's enabled in config */
	puts ("  -z <method>  Use compression method (default: deflate if available, else store)");
#ifdef OTEZIP_ENABLE_STORE
	puts ("      store     Store files without compression");
#endif
#ifdef OTEZIP_ENABLE_DEFLATE
	puts ("      deflate   Use deflate compression");
#endif
#ifdef OTEZIP_ENABLE_ZSTD
	puts ("      zstd      Use zstd compression");
#endif
#ifdef OTEZIP_ENABLE_LZMA
	puts ("      lzma      Use LZMA compression");
#endif
#ifdef OTEZIP_ENABLE_LZ4
	puts ("      lz4       Use LZ4 compression");
#endif
#ifdef OTEZIP_ENABLE_BROTLI
	puts ("      brotli    Use Brotli compression");
#endif
#ifdef OTEZIP_ENABLE_LZFSE
	puts ("      lzfse     Use LZFSE compression");
#endif
	puts ("  -P<policy>, --policy=<policy>  Extraction policy for suspicious entries\n"
	"      reject (default)  - reject entries with absolute paths, empty names, '..' that escape, or symlink parents\n"
	"      strip             - remove leading '..' components that would escape (e.g., '../../a' -> 'a')\n"
	"      allow             - allow unsafe extraction (use with caution)\n");
	puts ("  --verify-crc    Verify CRC32 when extracting and fail on mismatch\n");
	puts ("  --ignore-zipbomb  Ignore zipbomb expansion checks and allow large claimed uncompressed sizes (dangerous)\n");
}

static int list_files(const char *path) {
	int err = 0;
	zip_t *za = zip_open (path, ZIP_RDONLY, &err);
	if (!za) {
		fprintf (stderr, "Failed to open %s (err=%d)\n", path, err);
		return 1;
	}

	zip_uint64_t n = zip_get_num_files (za);
	for (zip_uint64_t i = 0; i < n; ++i) {
		const char *name = NULL; /* we only have names in directory entries */
		/* mzip stores names inside entries array – expose via zip_name_locate */
		name = ((struct otezip_entry *)za->entries)[i].name; /* hack – internal */
		printf ("%3llu  %s\n", (unsigned long long)i, name? name: "<unknown>");
	}

	zip_close (za);
	return 0;
}

/* Function to create a new ZIP archive or add files to existing one */
static int create_or_add_files(const char *path, char **files, int num_files, int create_mode, int compression_method) {
	int err = 0;
	int flags = create_mode? (ZIP_CREATE | ZIP_TRUNCATE): (ZIP_CREATE);

	zip_t *za = zip_open (path, flags, &err);
	if (!za) {
		fprintf (stderr, "Failed to %s %s (err=%d)\n", create_mode? "create": "open", path, err);
		return 1;
	}

	/* Modify next entry to add to use specified compression method */
	if (compression_method != 0) {
		/* For this mzip structure, store the compression method in the archive */
		((struct zip *)za)->default_method = compression_method;
	}

	for (int i = 0; i < num_files; i++) {
		const char *filename = files[i];

		/* Open file to read content */
		FILE *fp = fopen (filename, "rb");
		if (!fp) {
			fprintf (stderr, "Cannot open file: %s\n", filename);
			continue;
		}

		/* Get file size */
		fseek (fp, 0, SEEK_END);
		long file_size = ftell (fp);
		fseek (fp, 0, SEEK_SET);

		if (file_size < 0) {
			fprintf (stderr, "Error determining file size: %s\n", filename);
			fclose (fp);
			continue;
		}

		/* Read file content */
		void *buffer = malloc (file_size);
		if (!buffer) {
			fprintf (stderr, "Out of memory for file: %s\n", filename);
			fclose (fp);
			continue;
		}

		if (fread (buffer, 1, file_size, fp) != (size_t)file_size) {
			fprintf (stderr, "Error reading file: %s\n", filename);
			fclose (fp);
			free (buffer);
			continue;
		}
		fclose (fp);

		/* Extract just the base filename */
		const char *base_name = filename;
		const char *slash = strrchr (filename, '/');
		if (slash) {
			base_name = slash + 1;
		}

		/* Create source and add to archive */
		zip_source_t *src = zip_source_buffer (za, buffer, file_size, 1); /* 1 = free buffer when done */
		if (!src) {
			fprintf (stderr, "Failed to create source for file: %s\n", filename);
			free (buffer);
			continue;
		}

		/* Add file to archive */
		zip_int64_t idx = zip_file_add (za, base_name, src, 0);
		if (idx < 0) {
			fprintf (stderr, "Failed to add file to archive: %s\n", filename);
			/* Source buffer is already freed in case of failure */
			continue;
		}

		/* Compression method is already applied via default_method before add.
		 * Avoid changing per-entry method post-facto to prevent header mismatch */

		printf ("Added: %s (%ld bytes)\n", base_name, file_size);
	}

	/* Close and finalize the zip file */
	zip_close (za);
	return 0;
}

/* Normalize a zip entry name into 'out'. Return 0 on success, -1 on invalid path. */
/* Extraction policies */
#define POLICY_REJECT 0 /* default: reject suspicious entries */
#define POLICY_STRIP 1 /* strip leading '..' components */
#define POLICY_ALLOW 2 /* allow unsafe extraction(use with caution) */

static int g_extract_policy = POLICY_REJECT;

static int sanitize_extract_path(const char *name, char *out, size_t outlen) {
	if (!name || !*name) {
		return -1;
	}
	size_t nlen = strlen (name);
	if (nlen >= (size_t)PATH_MAX) {
		return -1;
	}

	char tmp[PATH_MAX];
	memset (tmp, 0, sizeof (tmp));
	/* Normalize backslashes to forward slashes */
	for (size_t i = 0; i <= nlen; ++i) {
		tmp[i] = (name[i] == '\\')? '/': name[i];
	}

	/* Reject absolute paths */
	if (tmp[0] == '/' || tmp[0] == '\\') {
		return -1;
	}
	/* Reject Windows drive absolute like C:/ or C:\ */
	if (nlen >= 2 && ((tmp[1] == ':' && ((tmp[0] >= 'A' && tmp[0] <= 'Z') || (tmp[0] >= 'a' && tmp[0] <= 'z'))))) {
		return -1;
	}

	/* Tokenize and resolve '.' and '..' without touching the filesystem */
	char *segments[PATH_MAX / 2];
	int segc = 0;
	/* Manual tokenization to avoid dependency on strtok_r feature macros */
	char *p = tmp;
	while (*p) {
		/* skip multiple slashes */
		while (*p == '/') {
			p++;
		}
		if (!*p) {
			break;
		}
		char *start = p;
		while (*p && *p != '/') {
			p++;
		}
		/* temporarily terminate segment */
		char saved = *p;
		*p = '\0';
		if (strcmp (start, "") == 0 || strcmp (start, ".") == 0) {
			/* skip */
		} else if (strcmp (start, "..") == 0) {
			if (segc == 0) {
				if (g_extract_policy == POLICY_REJECT) {
					return -1;
				} else if (g_extract_policy == POLICY_STRIP) {
					/* drop leading .. */
				} else {
					/* POLICY_ALLOW: treat as no-op */
				}
			} else {
				segc--;
			}
		} else {
			if (segc < (int) (PATH_MAX / 2)) {
				segments[segc++] = start;
			}
		}
		if (saved == '\0') {
			break;
		}
		*p = saved;
		/* move past '/' */
		if (*p == '/') {
			p++;
		}
	}

	if (segc == 0) {
		return -1; /* empty or only dots */
	}

	/* Build normalized path */
	size_t pos = 0;
	for (int i = 0; i < segc; ++i) {
		size_t need = strlen (segments[i]);
		if (i) {
			if (pos + 1 >= outlen) {
				return -1;
			}
			out[pos++] = '/';
		}
		if (pos + need >= outlen) {
			return -1;
		}
		memcpy (out + pos, segments[i], need);
		pos += need;
	}
	out[pos] = '\0';
	return 0;
}

/* Ensure parent directories exist and are not symlinks. Return 0 on success. */
static int ensure_parent_dirs(const char *path) {
	char tmp[PATH_MAX];
	strncpy (tmp, path, sizeof (tmp));
	tmp[sizeof (tmp) - 1] = '\0';

	size_t len = strlen (tmp);
	for (size_t i = 0; i < len; ++i) {
		if (tmp[i] == '/') {
			tmp[i] = '\0';
			struct stat st;
			if (OTEZIP_LSTAT (tmp, &st) == 0) {
				/* If the path exists, reject symlinks when policy says so */
				if (S_ISLNK (st.st_mode)) {
					if (g_extract_policy == POLICY_REJECT) {
						tmp[i] = '/';
						return -1;
					}
					/* POLICY_ALLOW: continue but do not create directories through symlinks */
				}
				if (!S_ISDIR (st.st_mode) && !S_ISLNK (st.st_mode)) {
					tmp[i] = '/';
					return -1;
				}
			} else {
				if (errno == ENOENT) {
					/* Try to create directory. If another thread/process created it
					 * concurrently, handle EEXIST by re-checking via lstat to avoid TOCTOU. */
					if (OTEZIP_MKDIR (tmp, 0755) != 0) {
						if (errno == EEXIST) {
							/* Re-check what exists */
							if (OTEZIP_LSTAT (tmp, &st) != 0) {
								tmp[i] = '/';
								return -1;
							}
							if (S_ISLNK (st.st_mode)) {
								if (g_extract_policy == POLICY_REJECT) {
									tmp[i] = '/';
									return -1;
								}
							}
							if (!S_ISDIR (st.st_mode)) {
								tmp[i] = '/';
								return -1;
							}
						} else {
							tmp[i] = '/';
							return -1;
						}
					}
				} else {
					tmp[i] = '/';
					return -1;
				}
			}
			tmp[i] = '/';
		}
	}
	return 0;
}

static int extract_all(const char *path) {
	int err = 0;
	zip_t *za = zip_open (path, ZIP_RDONLY, &err);
	if (!za) {
		fprintf (stderr, "Failed to open %s (err=%d)\n", path, err);
		return 1;
	}

	zip_uint64_t n = zip_get_num_files (za);
	for (zip_uint64_t i = 0; i < n; ++i) {
		zip_file_t *zf = zip_fopen_index (za, i, 0);
		if (!zf) {
			fprintf (stderr, "Could not read entry %llu\n", (unsigned long long)i);
			continue;
		}

		const char *raw_name = ((struct otezip_entry *)za->entries)[i].name; /* internal */
		char fname_sanitized[PATH_MAX];
		if (sanitize_extract_path (raw_name, fname_sanitized, sizeof (fname_sanitized)) != 0) {
			fprintf (stderr, "Skipping suspicious entry: %s\n", raw_name? raw_name: "(null)");
			zip_fclose (zf);
			continue;
		}

		/* If entry denotes a directory (name ends with '/'), create it */
		size_t rlen = raw_name? strlen (raw_name): 0;
		if (rlen > 0 && raw_name[rlen - 1] == '/') {
			if (ensure_parent_dirs (fname_sanitized) != 0) {
				fprintf (stderr, "Failed to create directory for %s\n", fname_sanitized);
			} else {
				if (OTEZIP_MKDIR (fname_sanitized, 0755) != 0 && errno != EEXIST) {
					fprintf (stderr, "Failed to create directory %s\n", fname_sanitized);
				}
			}
			zip_fclose (zf);
			continue;
		}

		if (ensure_parent_dirs (fname_sanitized) != 0) {
			fprintf (stderr, "Cannot ensure parent dirs for %s\n", fname_sanitized);
			zip_fclose (zf);
			continue;
		}

		/* Avoid overwriting existing files unless force (-f) is specified. */
		struct stat pst;
		if (OTEZIP_LSTAT (fname_sanitized, &pst) == 0) {
			if (!g_force) {
				fprintf (stderr, "Skipping existing file (use -f to overwrite): %s\n", fname_sanitized);
				zip_fclose (zf);
				continue;
			}
			/* If force is set and path is a symlink, reject unless policy allows */
			if (S_ISLNK (pst.st_mode) && g_extract_policy == POLICY_REJECT) {
				fprintf (stderr, "Refusing to overwrite symlink: %s\n", fname_sanitized);
				zip_fclose (zf);
				continue;
			}
		}

		/* Determine safe mode from central directory external attributes.
		 * Mask to 0777 to avoid applying SUID/SGID/sticky from archive. */
		uint32_t external_attr = 0;
		/* access internal entry data safely */
		struct otezip_entry *entry = &((struct otezip_entry *)za->entries)[i];
		external_attr = entry->external_attr;
		mode_t desired_mode = (mode_t) ((external_attr >> 16) & 0777);
		if (desired_mode == 0) {
			desired_mode = 0644; /* fallback */
		}

		/* Open the output file atomically: try O_CREAT|O_EXCL first to avoid
		 * TOCTOU overwrite races. If it exists and force is requested, open with
		 * O_TRUNC to overwrite. Use low-level descriptors and write () to avoid
		 * stdio buffering issues. */
		int fd = -1;
		int open_flags = O_WRONLY | O_CREAT | O_EXCL;
		fd = open (fname_sanitized, open_flags, desired_mode);
		if (fd < 0) {
			if (errno == EEXIST) {
				if (!g_force) {
					fprintf (stderr, "Skipping existing file (use -f to overwrite): %s\n", fname_sanitized);
					zip_fclose (zf);
					continue;
				}
				/* Force path: open for write/truncate but ensure it's not a symlink */
				if (OTEZIP_LSTAT (fname_sanitized, &pst) == 0 && S_ISLNK (pst.st_mode) && g_extract_policy == POLICY_REJECT) {
					fprintf (stderr, "Refusing to overwrite symlink: %s\n", fname_sanitized);
					zip_fclose (zf);
					continue;
				}
				fd = open (fname_sanitized, O_WRONLY | O_TRUNC);
				if (fd < 0) {
					fprintf (stderr, "Cannot open for overwrite %s: %s\n", fname_sanitized, strerror (errno));
					zip_fclose (zf);
					continue;
				}
			} else {
				fprintf (stderr, "Cannot create %s: %s\n", fname_sanitized, strerror (errno));
				zip_fclose (zf);
				continue;
			}
		}

		/* After creating/opening, ensure we didn't follow a symlink to a special file. */
		struct stat st2;
		if (fstat (fd, &st2) != 0) {
			fprintf (stderr, "Failed to stat %s\n", fname_sanitized);
			close (fd);
			zip_fclose (zf);
			continue;
		}
		if (!S_ISREG (st2.st_mode)) {
			fprintf (stderr, "Refusing to write non-regular file %s\n", fname_sanitized);
			close (fd);
			zip_fclose (zf);
			continue;
		}

		/* Apply safe permissions (masking out SUID/SGID/sticky by using 0777 mask) */
		if (OTEZIP_FCHMOD (fd, desired_mode & 0777) != 0) {
			/* Non-fatal: warn but continue */
			fprintf (stderr, "Warning: failed to set permissions on %s: %s\n", fname_sanitized, strerror (errno));
		}

		/* Write the file content */
		ssize_t wrote = 0;
		const uint8_t *data = (const uint8_t *)zf->data;
		size_t remain = zf->size;
		while (remain > 0) {
			ssize_t n = write (fd, data + wrote, remain);
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				fprintf (stderr, "Write error for %s: %s\n", fname_sanitized, strerror (errno));
				break;
			}
			wrote += n;
			remain -= n;
		}
		close (fd);

		/* Save size before closing – zip_fclose () frees the zip_file_t */
		size_t entry_size = (size_t)zf->size;
		zip_fclose (zf);
		if (remain == 0) {
			printf ("Extracted %s (%lu bytes)\n", fname_sanitized, (unsigned long)entry_size);
		} else {
			fprintf (stderr, "Failed to fully write %s\n", fname_sanitized);
		}
	}

	zip_close (za);
	return 0;
}

/* Decompress a gzip file (gunzip mode) */
static int gunzip_file(const char *input_path, const char *output_path) {
	FILE *fin = fopen (input_path, "rb");
	if (!fin) {
		fprintf (stderr, "Cannot open input file: %s\n", input_path);
		return 1;
	}

	/* Read entire input file */
	fseek (fin, 0, SEEK_END);
	long input_size = ftell (fin);
	fseek (fin, 0, SEEK_SET);

	if (input_size <= 0) {
		fprintf (stderr, "Invalid input file size\n");
		fclose (fin);
		return 1;
	}

	uint8_t *input_data = (uint8_t *)malloc (input_size);
	if (!input_data) {
		fprintf (stderr, "Out of memory\n");
		fclose (fin);
		return 1;
	}

	if (fread (input_data, 1, input_size, fin) != (size_t)input_size) {
		fprintf (stderr, "Failed to read input file\n");
		free (input_data);
		fclose (fin);
		return 1;
	}
	fclose (fin);

	/* Initialize inflate with auto-detect (gzip or zlib) */
	z_stream strm;
	memset (&strm, 0, sizeof (strm));
	strm.next_in = input_data;
	strm.avail_in = input_size;

	/* MAX_WBITS + 32 = auto-detect gzip or zlib header */
	int ret = inflateInit2 (&strm, MAX_WBITS + 32);
	if (ret != Z_OK) {
		fprintf (stderr, "inflateInit2 failed: %d\n", ret);
		free (input_data);
		return 1;
	}

	/* Allocate output buffer - start with 4x input size */
	size_t out_alloc = input_size * 4;
	if (out_alloc < 65536) {
		out_alloc = 65536;
	}
	uint8_t *output_data = (uint8_t *)malloc (out_alloc);
	if (!output_data) {
		fprintf (stderr, "Out of memory for output buffer\n");
		inflateEnd (&strm);
		free (input_data);
		return 1;
	}

	strm.next_out = output_data;
	strm.avail_out = out_alloc;

	/* Decompress */
	while (1) {
		ret = inflate (&strm, Z_NO_FLUSH);
		if (ret == Z_STREAM_END) {
			break;
		}
		if (ret == Z_BUF_ERROR) {
			/* Need more output space - either avail_out is 0 or
			 * we need to copy more bytes than available */
			size_t new_alloc = out_alloc * 2;
			uint8_t *new_buf = (uint8_t *)realloc (output_data, new_alloc);
			if (!new_buf) {
				fprintf (stderr, "Out of memory expanding output buffer\n");
				inflateEnd (&strm);
				free (output_data);
				free (input_data);
				return 1;
			}
			output_data = new_buf;
			strm.next_out = output_data + strm.total_out;
			strm.avail_out = new_alloc - strm.total_out;
			out_alloc = new_alloc;
			continue;
		}
		if (ret != Z_OK) {
			fprintf (stderr, "inflate failed: %d (avail_in=%u avail_out=%u total_out=%lu)\n",
				ret, strm.avail_in, strm.avail_out, (unsigned long)strm.total_out);
			inflateEnd (&strm);
			free (output_data);
			free (input_data);
			return 1;
		}
	}

	size_t output_size = strm.total_out;
	inflateEnd (&strm);
	free (input_data);

	/* Write output */
	FILE *fout = fopen (output_path, "wb");
	if (!fout) {
		fprintf (stderr, "Cannot create output file: %s\n", output_path);
		free (output_data);
		return 1;
	}

	if (fwrite (output_data, 1, output_size, fout) != output_size) {
		fprintf (stderr, "Failed to write output file\n");
		fclose (fout);
		free (output_data);
		return 1;
	}

	fclose (fout);
	free (output_data);

	printf ("Decompressed %s -> %s (%ld -> %lu bytes)\n",
		input_path, output_path, input_size, (unsigned long)output_size);
	return 0;
}

/* Compress a file with gzip */
static int gzip_file(const char *input_path, const char *output_path) {
	FILE *fin = fopen (input_path, "rb");
	if (!fin) {
		fprintf (stderr, "Cannot open input file: %s\n", input_path);
		return 1;
	}

	/* Read entire input file */
	fseek (fin, 0, SEEK_END);
	long input_size = ftell (fin);
	fseek (fin, 0, SEEK_SET);

	if (input_size < 0) {
		fprintf (stderr, "Invalid input file size\n");
		fclose (fin);
		return 1;
	}

	uint8_t *input_data = NULL;
	if (input_size > 0) {
		input_data = (uint8_t *)malloc (input_size);
		if (!input_data) {
			fprintf (stderr, "Out of memory\n");
			fclose (fin);
			return 1;
		}
		if (fread (input_data, 1, input_size, fin) != (size_t)input_size) {
			fprintf (stderr, "Failed to read input file\n");
			free (input_data);
			fclose (fin);
			return 1;
		}
	}
	fclose (fin);

	/* Initialize deflate for gzip output (MAX_WBITS + 16) */
	z_stream strm;
	memset (&strm, 0, sizeof (strm));

	int ret = deflateInit2 (&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
		MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
	if (ret != Z_OK) {
		fprintf (stderr, "deflateInit2 failed: %d\n", ret);
		free (input_data);
		return 1;
	}

	/* Allocate output buffer */
	size_t out_alloc = input_size + (input_size / 8) + 128;
	if (out_alloc < 256) {
		out_alloc = 256;
	}
	uint8_t *output_data = (uint8_t *)malloc (out_alloc);
	if (!output_data) {
		fprintf (stderr, "Out of memory for output buffer\n");
		deflateEnd (&strm);
		free (input_data);
		return 1;
	}

	strm.next_in = input_data;
	strm.avail_in = input_size;
	strm.next_out = output_data;
	strm.avail_out = out_alloc;

	/* Compress */
	ret = deflate (&strm, Z_FINISH);
	if (ret != Z_STREAM_END) {
		fprintf (stderr, "deflate failed: %d\n", ret);
		deflateEnd (&strm);
		free (output_data);
		free (input_data);
		return 1;
	}

	size_t output_size = strm.total_out;
	deflateEnd (&strm);
	free (input_data);

	/* Write output */
	FILE *fout = fopen (output_path, "wb");
	if (!fout) {
		fprintf (stderr, "Cannot create output file: %s\n", output_path);
		free (output_data);
		return 1;
	}

	if (fwrite (output_data, 1, output_size, fout) != output_size) {
		fprintf (stderr, "Failed to write output file\n");
		fclose (fout);
		free (output_data);
		return 1;
	}

	fclose (fout);
	free (output_data);

	printf ("Compressed %s -> %s (%ld -> %lu bytes)\n",
		input_path, output_path, input_size, (unsigned long)output_size);
	return 0;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		usage ();
		return 1;
	}

	if (strcmp (argv[1], "-v") == 0) {
		printf ("mzip version %s\n", OTEZIP_VERSION);
		return 0;
	}

	/* Treat -h/--help as a successful request for usage information */
	if (strcmp (argv[1], "-h") == 0 || strcmp (argv[1], "--help") == 0) {
		usage ();
		return 0;
	}

	/* Handle gzip/gunzip modes early - they only need input file */
	if (strcmp (argv[1], "-d") == 0) {
		/* Decompress (gunzip) mode */
		if (argc < 3) {
			fprintf (stderr, "Error: No input file specified for decompression\n");
			usage ();
			return 1;
		}
		const char *input_path = argv[2];
		char output_path[PATH_MAX];
		if (argc >= 4) {
			strncpy (output_path, argv[3], sizeof (output_path) - 1);
			output_path[sizeof (output_path) - 1] = '\0';
		} else {
			/* Remove .gz extension if present, else add .out */
			size_t len = strlen (input_path);
			if (len > 3 && strcmp (input_path + len - 3, ".gz") == 0) {
				snprintf (output_path, sizeof (output_path), "%.*s", (int)(len - 3), input_path);
			} else {
				snprintf (output_path, sizeof (output_path), "%s.out", input_path);
			}
		}
		return gunzip_file (input_path, output_path);
	}

	if (strcmp (argv[1], "-g") == 0) {
		/* Compress (gzip) mode */
		if (argc < 3) {
			fprintf (stderr, "Error: No input file specified for compression\n");
			usage ();
			return 1;
		}
		const char *input_path = argv[2];
		char output_path[PATH_MAX];
		if (argc >= 4) {
			strncpy (output_path, argv[3], sizeof (output_path) - 1);
			output_path[sizeof (output_path) - 1] = '\0';
		} else {
			snprintf (output_path, sizeof (output_path), "%s.gz", input_path);
		}
		return gzip_file (input_path, output_path);
	}

	if (argc < 3) {
		usage ();
		return 1;
	}

	int mode_list = 0, mode_extract = 0, mode_create = 0, mode_append = 0;

	/* Set default compression method based on available algorithms */
	int compression_method = 0; /* Default to store */
#ifdef OTEZIP_ENABLE_DEFLATE
	compression_method = OTEZIP_METHOD_DEFLATE; /* Default to deflate if available */
#endif

	if (strcmp (argv[1], "-l") == 0) {
		mode_list = 1;
	} else if (strcmp (argv[1], "-x") == 0) {
		mode_extract = 1;
	} else if (strcmp (argv[1], "-c") == 0) {
		mode_create = 1;
	} else if (strcmp (argv[1], "-a") == 0) {
		mode_append = 1;
	} else if (strcmp (argv[1], "-v") == 0) {
		printf ("mzip version %s\n", OTEZIP_VERSION);
		return 0;
	} else {
		usage ();
		return 1;
	}

	const char *zip_path = argv[2];

	/* Process additional options */
	int i;
	int filter_count = 0;
	char **files_to_add = NULL;
	int num_files = 0;

	if (mode_create || mode_append) {
		/* Count actual files vs option flags */
		files_to_add = &argv[3];
		num_files = argc - 3;

		for (i = 3; i < argc; i++) {
			if (strcmp (argv[i], "-z") == 0) {
				filter_count += 2; // -z and its argument
			}
		}
		num_files -= filter_count;
	}

	/* Parse compression method */
	for (i = 3; i < argc; i++) {
		if (strcmp (argv[i], "-z") == 0) {
			if (i + 1 >= argc) {
				fprintf (stderr, "Error: -z requires a method argument\n");
				return 1;
			}
			int method = otezip_method_from_string (argv[i + 1]);
			if (method == -1) {
				fprintf (stderr, "Error: unknown compression method '%s'\n", argv[i + 1]);
				return 1;
			}
			compression_method = method;
			i++; /* skip the method argument */
		}
	}

	/* Parse extraction policy option: -P<policy> or --policy=<policy>
	 * Supported: reject (default), strip, allow
	 */
	for (i = 3; i < argc; i++) {
		const char *arg = argv[i];
		const char *val = NULL;
		if (strncmp (arg, "-P", 2) == 0) {
			val = arg + 2;
			if (*val == '\0' && i + 1 < argc) {
				val = argv[++i];
			}
		} else if (strncmp (arg, "--policy=", 9) == 0) {
			val = arg + 9;
		}
		if (!val) {
			continue;
		}
		if (strcmp (val, "reject") == 0 || strcmp (val, "0") == 0) {
			g_extract_policy = POLICY_REJECT;
		} else if (strcmp (val, "strip") == 0 || strcmp (val, "1") == 0) {
			g_extract_policy = POLICY_STRIP;
		} else if (strcmp (val, "allow") == 0 || strcmp (val, "2") == 0) {
			g_extract_policy = POLICY_ALLOW;
		} else {
			fprintf (stderr, "Unknown policy: %s\n", val);
			return 1;
		}
	}

	/* Parse CRC verification option: --verify-crc */
	for (i = 3; i < argc; i++) {
		if (strcmp (argv[i], "--verify-crc") == 0) {
			/* enable strict CRC verification in the mzip backend */
			otezip_verify_crc = 1;
		}
	}

	/* Parse zipbomb ignore option: --ignore-zipbomb (explicit override) */
	for (i = 3; i < argc; i++) {
		if (strcmp (argv[i], "--ignore-zipbomb") == 0) {
			/* explicit, dangerous override: allow archives that claim huge
			 * uncompressed sizes. This bypasses internal safety checks. */
			otezip_ignore_zipbomb = 1;
		}
	}

	/* Parse force option: -f or --force (allow overwriting existing files) */
	for (i = 3; i < argc; i++) {
		if (strcmp (argv[i], "-f") == 0 || strcmp (argv[i], "--force") == 0) {
			g_force = 1;
		}
	}

	if (mode_list) {
		if (argc < 3) {
			usage ();
			return 1;
		}
		return list_files (zip_path);
	} else if (mode_extract) {
		if (argc < 3) {
			usage ();
			return 1;
		}
		return extract_all (zip_path);
	} else if (mode_create || mode_append) {
		if (argc < 4) {
			fprintf (stderr, "Error: No files specified to %s.\n", mode_create? "create archive with": "add to archive");
			usage ();
			return 1;
		}
		return create_or_add_files (zip_path, files_to_add, num_files, mode_create, compression_method);
	}

	usage ();
	return 1;
}
