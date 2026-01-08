/* otezip.h – Minimalistic libzip subset replacement
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
 *  • No encrypted entries, no ZIP64.
 *  • Data descriptors (general flag bit 3) supported via central directory.
 *
 * License: MIT / 0-BSD – do whatever you want; attribution appreciated.
 */
#ifndef OTEZIP_H_
#define OTEZIP_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "./config.h"

/* Little-endian read/write helpers (portable, unaligned-safe).
 * Use these when reading/writing multi-byte fields from on-disk formats
 * (ZIP and other custom in-repo formats). These are static inline so they
 * can be used from any compilation unit including the compression backends.
 */
static inline uint16_t otezip_read_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t otezip_read_le32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}
static inline uint64_t otezip_read_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= ((uint64_t)p[i]) << (i * 8);
    return v;
}
static inline void otezip_write_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static inline void otezip_write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static inline void otezip_write_le64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

/* ----  minimal type aliases (keep public names identical to libzip) ---- */

typedef uint64_t zip_uint64_t;
typedef int64_t  zip_int64_t;
typedef int      zip_flags_t;    /* we don't interpret any flags for now */
typedef int32_t  zip_int32_t;
typedef uint32_t zip_uint32_t;
typedef uint16_t zip_uint16_t;
typedef uint8_t  zip_uint8_t;

/* an in-memory representation of a single directory entry */
struct otezip_entry {
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

/* Use libzip-compatible struct names for full compatibility */
struct zip {
    FILE               *fp;
    struct otezip_entry  *entries;
    zip_uint64_t        n_entries;
    int                 mode;       /* 0=read-only, 1=write */
    zip_uint64_t        next_index; /* Next available index for adding files */
    uint16_t            default_method; /* Default compression method for new entries */
};

struct zip_file {
    uint8_t   *data;   /* complete uncompressed data                 */
    uint32_t   size;
    zip_uint64_t pos;  /* current read position for zip_fread       */
};

struct zip_source {
    const void *buf;
    zip_uint64_t len;
    int freep;
};

/* Error information structure */
struct otezip_error {
    int zip_err;   /* libzip error code (ZIP_ER_*) */
    int sys_err;   /* copy of errno (E*) or zlib error code */
};

/* Backward compatibility: keep otezip names as aliases */
typedef struct zip         otezip_archive;
typedef struct zip_file    otezip_file;
typedef struct zip_source  otezip_src_buf;

typedef struct zip         zip_t;      /* opaque archive handle        */
typedef struct zip_file    zip_file_t; /* opaque file-in-memory handle */
typedef struct zip_source  zip_source_t;/* stub                          */
typedef struct otezip_error zip_error_t; /* error structure              */

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

/* Compression methods */
#define ZIP_CM_STORE 0    /* stored (uncompressed) */
#define ZIP_CM_DEFLATE 8  /* deflated */

/* Maximum value for zip_uint64_t */
#define ZIP_UINT64_MAX ((zip_uint64_t)-1)

/* libzip error codes */
#define ZIP_ER_OK 0               /* No error */
#define ZIP_ER_INVAL 18           /* Invalid argument */
#define ZIP_ER_NOENT 9            /* No such file */
#define ZIP_ER_EXISTS 10          /* File already exists */
#define ZIP_ER_NOZIP 19           /* Not a zip archive */
#define ZIP_ER_RDONLY 25          /* Read-only archive */
#define ZIP_ER_OPEN 11            /* Can't open file */
#define ZIP_ER_READ 5             /* Read error */
#define ZIP_ER_INCONS 21          /* Zip archive inconsistent */

/* zip_stat flags */
#define ZIP_STAT_NAME 0x0001u
#define ZIP_STAT_INDEX 0x0002u
#define ZIP_STAT_SIZE 0x0004u
#define ZIP_STAT_COMP_SIZE 0x0008u
#define ZIP_STAT_MTIME 0x0010u
#define ZIP_STAT_CRC 0x0020u
#define ZIP_STAT_COMP_METHOD 0x0040u

/* zip_stat structure */
struct zip_stat {
    zip_uint64_t valid;        /* which fields have valid values */
    const char *name;          /* name of the file */
    zip_uint64_t index;        /* index within archive */
    zip_uint64_t size;         /* size of file (uncompressed) */
    zip_uint64_t comp_size;    /* size of file (compressed) */
    time_t mtime;              /* modification time */
    zip_uint32_t crc;          /* crc of file data */
    zip_uint16_t comp_method;  /* compression method used */
};

typedef struct zip_stat zip_stat_t;

/* ----------------------------  public API  ----------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

zip_t *        zip_open          (const char *path, int flags, int *errorp);
zip_t *        zip_open_from_source(zip_source_t *src, int flags, zip_error_t *error);
int            zip_close         (zip_t *za);

zip_uint64_t   zip_get_num_files (zip_t *za);
zip_int64_t    zip_name_locate   (zip_t *za, const char *fname, zip_flags_t flags);
const char *   zip_get_name      (zip_t *za, zip_uint64_t index, zip_flags_t flags);

zip_file_t *   zip_fopen_index   (zip_t *za, zip_uint64_t index, zip_flags_t flags);
int            zip_fclose        (zip_file_t *zf);
zip_int64_t    zip_fread         (zip_file_t *zf, void *buf, zip_uint64_t nbytes);

zip_source_t * zip_source_buffer (zip_t *za, const void *data, zip_uint64_t len, int freep);
zip_source_t * zip_source_buffer_create(const void *data, zip_uint64_t len, int freep, zip_error_t *error);
void           zip_source_free   (zip_source_t *src);
zip_int64_t    zip_file_add      (zip_t *za, const char *name, zip_source_t *src, zip_flags_t flags);
int            zip_file_replace  (zip_t *za, zip_uint64_t index, zip_source_t *src, zip_flags_t flags);
int            zip_replace       (zip_t *za, zip_uint64_t index, zip_source_t *src);
zip_int64_t    zip_add           (zip_t *za, const char *name, zip_source_t *src);
int            zip_set_file_compression(zip_t *za, zip_uint64_t index, zip_int32_t comp, zip_uint32_t comp_flags);

int            zip_stat          (zip_t *za, const char *fname, zip_flags_t flags, zip_stat_t *st);
int            zip_stat_index    (zip_t *za, zip_uint64_t index, zip_flags_t flags, zip_stat_t *st);
void           zip_stat_init     (zip_stat_t *st);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* Global flag: when non-zero, verify CRC32 on extraction and fail on mismatch. */
extern int otezip_verify_crc;

/* Zipbomb / expansion protection globals.
 * - otezip_max_expansion_ratio: maximum allowed multiplier (out <= in * ratio + slack)
 * - otezip_max_expansion_slack: additional bytes allowed regardless of ratio
 * - otezip_ignore_zipbomb: when non-zero, skip the expansion checks (CLI override)
 */
extern uint64_t otezip_max_expansion_ratio;
extern uint64_t otezip_max_expansion_slack;
extern int otezip_ignore_zipbomb;

/* Helper function to get compression method ID from string name.
 * Returns the OTEZIP_METHOD_* value or -1 if invalid/not supported. */
int otezip_method_from_string(const char *method_name);

#endif /* OTEZIP_H_ */
