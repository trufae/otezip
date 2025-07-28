/* mzip.h – Minimalistic libzip subset replacement
 * Version: 0.2 (2025-07-27)
 *
 * This library provides a tiny subset of the libzip API so that
 * existing code using **only** the following symbols keeps compiling:
 *
 *   zip_open           (read/write)
 *   zip_close
 *   zip_get_num_files
 *   zip_name_locate
 *   zip_fopen_index    (returns the **whole** uncompressed file in memory)
 *   zip_fclose
 *   zip_source_buffer  (for adding files)
 *   zip_file_add       (add file to archive)
 *   zip_set_file_compression (set compression method)
 *
 * Supported archives
 * ------------------
 *  • Single-disk, non-spanned ZIP files created with the standard PKZIP spec.
 *  • Compression methods 0 (stored) and 8 (deflate).
 *  • No encrypted entries, no ZIP64, no data descriptors (general flag bit 3).
 *
 * License: MIT / 0-BSD – do whatever you want; attribution appreciated.
 */
#ifndef MZIP_H_
#define MZIP_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

/* ----  minimal type aliases (keep public names identical to libzip) ---- */

typedef uint64_t zip_uint64_t;
typedef int64_t  zip_int64_t;
typedef int      zip_flags_t;    /* we don't interpret any flags for now */
typedef int32_t  zip_int32_t;
typedef uint32_t zip_uint32_t;

/* an in-memory representation of a single directory entry */
struct mzip_entry {
    char      *name;                /* zero-terminated filename              */
    uint32_t   local_hdr_ofs;       /* offset of corresponding LFH          */
    uint32_t   comp_size;
    uint32_t   uncomp_size;
    uint16_t   method;              /* 0=store, 8=deflate                   */
    uint32_t   crc32;               /* CRC-32 checksum of uncompressed data */
    uint16_t   file_time;           /* DOS format file time */
    uint16_t   file_date;           /* DOS format file date */
    uint32_t   external_attr;       /* External file attributes (permissions) */
};

struct mzip_archive {
    FILE               *fp;
    struct mzip_entry  *entries;
    zip_uint64_t        n_entries;
    int                 mode;       /* 0=read-only, 1=write */
    zip_uint64_t        next_index; /* Next available index for adding files */
};

struct mzip_file {
    uint8_t   *data;   /* complete uncompressed data                 */
    uint32_t   size;
};

struct mzip_src_buf { 
    const void *buf;
    zip_uint64_t len;
    int freep;
};

typedef struct mzip_archive   zip_t;      /* opaque archive handle        */
typedef struct mzip_file      zip_file_t; /* opaque file-in-memory handle */
typedef struct mzip_src_buf   zip_source_t;/* stub                          */

/* Only flag we meaningfully accept at the moment. */
#ifndef ZIP_RDONLY
#define ZIP_RDONLY 0
#endif

#ifndef ZIP_CREATE
#define ZIP_CREATE 1
#endif

#ifndef ZIP_EXCL
#define ZIP_EXCL 2
#endif

#ifndef ZIP_TRUNCATE
#define ZIP_TRUNCATE 8
#endif

/* ----------------------------  public API  ----------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

zip_t *        zip_open          (const char *path, int flags, int *errorp);
int            zip_close         (zip_t *za);

zip_uint64_t   zip_get_num_files (zip_t *za);
zip_int64_t    zip_name_locate   (zip_t *za, const char *fname, zip_flags_t flags);

zip_file_t *   zip_fopen_index   (zip_t *za, zip_uint64_t index, zip_flags_t flags);
int            zip_fclose        (zip_file_t *zf);

zip_source_t * zip_source_buffer (zip_t *za, const void *data, zip_uint64_t len, int freep);
zip_int64_t    zip_file_add      (zip_t *za, const char *name, zip_source_t *src, zip_flags_t flags);
int            zip_set_file_compression(zip_t *za, zip_uint64_t index, zip_int32_t comp, zip_uint32_t comp_flags);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MZIP_H_ */