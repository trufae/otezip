/* main.c – Tiny demo utility for mzip.h
 * Build:  gcc -std=c99 -DMZIP_IMPLEMENTATION main.c -lz -o mzip
 * Usage:  ./mzip -l  archive.zip   # list files
 *         ./mzip -x  archive.zip   # extract into current directory
 *         ./mzip -c  archive.zip file1 file2...  # create new zip archive
 *         ./mzip -a  archive.zip file1 file2...  # add files to existing archive
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "config.h"
#include "mzip.h"

#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void usage(void) {
    puts("mzip – minimal ZIP reader/writer (mzip.h demo)\n"
            "Usage: mzip [-l | -x | -c | -a | -v] <archive.zip> [files...] [options]\n"
            "  -l   List contents\n"
            "  -x   Extract all files into current directory\n"
            "  -c   Create new archive with specified files\n"
            "  -a   Add files to existing archive\n"
            "  -v   Show version number\n\n"
            "Options:");

	/* Show compression options based on what's enabled in config */
#ifdef MZIP_ENABLE_STORE
	puts("  -z0  Store files without compression");
#endif
#ifdef MZIP_ENABLE_DEFLATE
	puts("  -z1  Use deflate compression (default)");
#endif
#ifdef MZIP_ENABLE_ZSTD
	puts("  -z2  Use zstd compression");
#endif
#ifdef MZIP_ENABLE_LZMA
	puts("  -z3  Use LZMA compression");
#endif
#ifdef MZIP_ENABLE_LZ4
	puts("  -z4  Use LZ4 compression");
#endif
#ifdef MZIP_ENABLE_BROTLI
	puts("  -z5  Use Brotli compression");
#endif
#ifdef MZIP_ENABLE_LZFSE
    puts("  -z6  Use LZFSE compression");
#endif
    puts("  -P<policy>, --policy=<policy>  Extraction policy for suspicious entries\n"
         "      reject (default)  - reject entries with absolute paths, empty names, '..' that escape, or symlink parents\n"
         "      strip             - remove leading '..' components that would escape (e.g., '../../a' -> 'a')\n"
         "      allow             - allow unsafe extraction (use with caution)\n");
}

static int list_files(const char *path) {
	int err = 0;
	zip_t *za = zip_open(path, ZIP_RDONLY, &err);
	if (!za) {
		fprintf(stderr, "Failed to open %s (err=%d)\n", path, err);
		return 1;
	}

	zip_uint64_t n = zip_get_num_files(za);
	for (zip_uint64_t i = 0; i < n; ++i) {
		const char *name = NULL; /* we only have names in directory entries */
		/* mzip stores names inside entries array – expose via zip_name_locate */
		name = ((struct mzip_entry*)za->entries)[i].name; /* hack – internal */
		printf("%3llu  %s\n", (unsigned long long)i, name ? name : "<unknown>");
	}

	zip_close(za);
	return 0;
}

/* Function to create a new ZIP archive or add files to existing one */
static int create_or_add_files(const char *path, char **files, int num_files, int create_mode, int compression_method) {
	int err = 0;
	int flags = create_mode ? (ZIP_CREATE | ZIP_TRUNCATE) : (ZIP_CREATE);

	zip_t *za = zip_open(path, flags, &err);
	if (!za) {
		fprintf(stderr, "Failed to %s %s (err=%d)\n", 
				create_mode ? "create" : "open", path, err);
		return 1;
	}

	/* Modify next entry to add to use specified compression method */
	if (compression_method != 0) {
		/* For this mzip structure, store the compression method in the mzip_archive */
		((struct mzip_archive *)za)->default_method = compression_method;
	}

	for (int i = 0; i < num_files; i++) {
		const char *filename = files[i];

		/* Open file to read content */
		FILE *fp = fopen(filename, "rb");
		if (!fp) {
			fprintf(stderr, "Cannot open file: %s\n", filename);
			continue;
		}

		/* Get file size */
		fseek(fp, 0, SEEK_END);
		long file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		if (file_size < 0) {
			fprintf(stderr, "Error determining file size: %s\n", filename);
			fclose(fp);
			continue;
		}

		/* Read file content */
		void *buffer = malloc(file_size);
		if (!buffer) {
			fprintf(stderr, "Out of memory for file: %s\n", filename);
			fclose(fp);
			continue;
		}

		if (fread(buffer, 1, file_size, fp) != (size_t)file_size) {
			fprintf(stderr, "Error reading file: %s\n", filename);
			fclose(fp);
			free(buffer);
			continue;
		}
		fclose(fp);

		/* Extract just the base filename */
		const char *base_name = filename;
		const char *slash = strrchr(filename, '/');
		if (slash) {
			base_name = slash + 1;
		}

		/* Create source and add to archive */
		zip_source_t *src = zip_source_buffer(za, buffer, file_size, 1); /* 1 = free buffer when done */
		if (!src) {
			fprintf(stderr, "Failed to create source for file: %s\n", filename);
			free(buffer);
			continue;
		}

		/* Add file to archive */
		zip_int64_t idx = zip_file_add(za, base_name, src, 0);
		if (idx < 0) {
			fprintf(stderr, "Failed to add file to archive: %s\n", filename);
			/* Source buffer is already freed in case of failure */
			continue;
		}

		/* Compression method is already applied via default_method before add.
		 * Avoid changing per-entry method post-facto to prevent header mismatch */

		printf("Added: %s (%ld bytes)\n", base_name, file_size);
	}

	/* Close and finalize the zip file */
	zip_close(za);
	return 0;
}

/* Normalize a zip entry name into 'out'. Return 0 on success, -1 on invalid path. */
/* Extraction policies */
#define POLICY_REJECT 0       /* default: reject suspicious entries */
#define POLICY_STRIP  1       /* strip leading '..' components */
#define POLICY_ALLOW  2       /* allow unsafe extraction (use with caution) */

static int g_extract_policy = POLICY_REJECT;

static int sanitize_extract_path(const char *name, char *out, size_t outlen) {
    if (!name || !*name) return -1;
    size_t nlen = strlen(name);
    if (nlen >= (size_t)PATH_MAX) return -1;

    char tmp[PATH_MAX];
    /* Normalize backslashes to forward slashes */
    for (size_t i = 0; i <= nlen; ++i) tmp[i] = (name[i] == '\\') ? '/' : name[i];

    /* Reject absolute paths */
    if (tmp[0] == '/' || tmp[0] == '\\') return -1;
    /* Reject Windows drive absolute like C:/ or C:\ */
    if (nlen >= 2 && ((tmp[1] == ':' && ((tmp[0] >= 'A' && tmp[0] <= 'Z') || (tmp[0] >= 'a' && tmp[0] <= 'z'))))) return -1;

    /* Tokenize and resolve '.' and '..' without touching the filesystem */
    char *segments[PATH_MAX / 2];
    int segc = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(tmp, "/", &saveptr);
    while (tok) {
        if (strcmp(tok, "") == 0 || strcmp(tok, ".") == 0) {
            /* skip */
        } else if (strcmp(tok, "..") == 0) {
            if (segc == 0) {
                if (g_extract_policy == POLICY_REJECT) {
                    /* would escape extraction root */
                    return -1;
                } else if (g_extract_policy == POLICY_STRIP) {
                    /* drop this leading .. component */
                    /* just continue without pushing */
                } else {
                    /* POLICY_ALLOW: allow moving up but do not actually escape since
                     * we are not resolving against filesystem; treat as no-op */
                }
            } else {
                segc--;
            }
        } else {
            segments[segc++] = tok;
        }
        tok = strtok_r(NULL, "/", &saveptr);
    }

    if (segc == 0) return -1; /* empty or only dots */

    /* Build normalized path */
    size_t pos = 0;
    for (int i = 0; i < segc; ++i) {
        size_t need = strlen(segments[i]);
        if (i) {
            if (pos + 1 >= outlen) return -1;
            out[pos++] = '/';
        }
        if (pos + need >= outlen) return -1;
        memcpy(out + pos, segments[i], need);
        pos += need;
    }
    out[pos] = '\0';
    return 0;
}

/* Ensure parent directories exist and are not symlinks. Return 0 on success. */
static int ensure_parent_dirs(const char *path) {
    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp)-1] = '\0';

    size_t len = strlen(tmp);
    for (size_t i = 0; i < len; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            struct stat st;
            if (lstat(tmp, &st) == 0) {
                if (S_ISLNK(st.st_mode)) {
                    if (g_extract_policy == POLICY_REJECT) return -1;
                    /* if policy allows, treat symlink as opaque and continue */
                }
                if (!S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) return -1;
            } else {
                if (errno == ENOENT) {
                    if (mkdir(tmp, 0755) != 0) return -1;
                } else {
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
    zip_t *za = zip_open(path, ZIP_RDONLY, &err);
    if (!za) {
        fprintf(stderr, "Failed to open %s (err=%d)\n", path, err);
        return 1;
    }

    zip_uint64_t n = zip_get_num_files(za);
    for (zip_uint64_t i = 0; i < n; ++i) {
        zip_file_t *zf = zip_fopen_index(za, i, 0);
        if (!zf) {
            fprintf(stderr, "Could not read entry %llu\n", (unsigned long long)i);
            continue;
        }

        const char *raw_name = ((struct mzip_entry*)za->entries)[i].name; /* internal */
        char fname_sanitized[PATH_MAX];
        if (sanitize_extract_path(raw_name, fname_sanitized, sizeof(fname_sanitized)) != 0) {
            fprintf(stderr, "Skipping suspicious entry: %s\n", raw_name ? raw_name : "(null)");
            zip_fclose(zf);
            continue;
        }

        /* If entry denotes a directory (name ends with '/'), create it */
        size_t rlen = raw_name ? strlen(raw_name) : 0;
        if (rlen > 0 && raw_name[rlen-1] == '/') {
            if (ensure_parent_dirs(fname_sanitized) != 0) {
                fprintf(stderr, "Failed to create directory for %s\n", fname_sanitized);
            } else {
                if (mkdir(fname_sanitized, 0755) != 0 && errno != EEXIST) {
                    fprintf(stderr, "Failed to create directory %s\n", fname_sanitized);
                }
            }
            zip_fclose(zf);
            continue;
        }

        if (ensure_parent_dirs(fname_sanitized) != 0) {
            fprintf(stderr, "Cannot ensure parent dirs for %s\n", fname_sanitized);
            zip_fclose(zf);
            continue;
        }

        FILE *out = fopen(fname_sanitized, "wb");
        if (!out) {
            fprintf(stderr, "Cannot create %s\n", fname_sanitized);
            zip_fclose(zf);
            continue;
        }

        /* Write data and add proper termination for text files */
        fwrite(zf->data, 1, zf->size, out);
        fclose(out);
        /* Save size before closing – zip_fclose() frees the zip_file_t */
        size_t entry_size = (size_t)zf->size;
        zip_fclose(zf);
        printf("Extracted %s (%zu bytes)\n", fname_sanitized, entry_size);
    }

    zip_close(za);
    return 0;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		usage();
		return 1;
	}

	if (strcmp(argv[1], "-v") == 0) {
		printf("mzip version %s\n", MZIP_VERSION);
		return 0;
	}

	/* Treat -h/--help as a successful request for usage information */
	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		usage();
		return 0;
	}

	if (argc < 3) {
		usage();
		return 1;
	}

	int mode_list=0, mode_extract=0, mode_create=0, mode_append=0;

	/* Set default compression method based on available algorithms */
	int compression_method = 0; /* Default to store */
#ifdef MZIP_ENABLE_DEFLATE
	compression_method = MZIP_METHOD_DEFLATE; /* Default to deflate if available */
#endif

	if (strcmp(argv[1], "-l") == 0) mode_list = 1;
	else if (strcmp(argv[1], "-x") == 0) mode_extract = 1;
	else if (strcmp(argv[1], "-c") == 0) mode_create = 1;
	else if (strcmp(argv[1], "-a") == 0) mode_append = 1;
	else if (strcmp(argv[1], "-v") == 0) {
		printf("mzip version %s\n", MZIP_VERSION);
		return 0;
	}
	else {
		usage();
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
			if (strncmp(argv[i], "-z", 2) == 0) {
				filter_count++;
			}
		}
		num_files -= filter_count;
	}

	/* Find compression options */
	for (i = 3; i < argc; i++) {
#ifdef MZIP_ENABLE_STORE
		if (strcmp(argv[i], "-z0") == 0) {
			compression_method = MZIP_METHOD_STORE; /* Store - no compression */
		} 
#endif
#ifdef MZIP_ENABLE_DEFLATE
		else if (strcmp(argv[i], "-z1") == 0) {
			compression_method = MZIP_METHOD_DEFLATE; /* Deflate */
		} 
#endif
#ifdef MZIP_ENABLE_ZSTD
		else if (strcmp(argv[i], "-z2") == 0) {
			compression_method = MZIP_METHOD_ZSTD; /* Zstd */
		}
#endif
#ifdef MZIP_ENABLE_LZMA
		else if (strcmp(argv[i], "-z3") == 0) {
			compression_method = MZIP_METHOD_LZMA; /* LZMA */
		}
#endif
#ifdef MZIP_ENABLE_LZ4
		else if (strcmp(argv[i], "-z4") == 0) {
			compression_method = MZIP_METHOD_LZ4; /* LZ4 */
		}
#endif
#ifdef MZIP_ENABLE_BROTLI
		else if (strcmp(argv[i], "-z5") == 0) {
			compression_method = MZIP_METHOD_BROTLI; /* Brotli */
		}
#endif
#ifdef MZIP_ENABLE_LZFSE
		else if (strcmp(argv[i], "-z6") == 0) {
			compression_method = MZIP_METHOD_LZFSE; /* LZFSE */
		}
#endif
	}

	/* Parse extraction policy option: -P<policy> or --policy=<policy>
	 * Supported: reject (default), strip, allow
	 */
	for (i = 3; i < argc; i++) {
		const char *arg = argv[i];
		const char *val = NULL;
		if (strncmp(arg, "-P", 2) == 0) {
			val = arg + 2;
			if (*val == '\0' && i + 1 < argc) val = argv[++i];
		} else if (strncmp(arg, "--policy=", 9) == 0) {
			val = arg + 9;
		}
		if (!val) continue;
		if (strcmp(val, "reject") == 0 || strcmp(val, "0") == 0) {
			g_extract_policy = POLICY_REJECT;
		} else if (strcmp(val, "strip") == 0 || strcmp(val, "1") == 0) {
			g_extract_policy = POLICY_STRIP;
		} else if (strcmp(val, "allow") == 0 || strcmp(val, "2") == 0) {
			g_extract_policy = POLICY_ALLOW;
		} else {
			fprintf(stderr, "Unknown policy: %s\n", val);
			return 1;
		}
	}

	if (mode_list) {
		if (argc != 3) {
			usage();
			return 1;
		}
		return list_files(zip_path);
	} else if (mode_extract) {
		if (argc != 3) {
			usage();
			return 1;
		}
		return extract_all(zip_path);
	} else if (mode_create || mode_append) {
		if (argc < 4) {
			fprintf(stderr, "Error: No files specified to %s.\n", 
					mode_create ? "create archive with" : "add to archive");
			usage();
			return 1;
		}
		return create_or_add_files(zip_path, files_to_add, num_files, mode_create, compression_method);
	}

	usage();
	return 1;
}
