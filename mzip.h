/* mzip.h – Minimalistic libzip subset replacement
 * Version: 0.2 (2025-07-27)
 *
 * This header-only library provides a tiny subset of the libzip API so that
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
 * Build notes
 * -----------
 *   #define MZIP_IMPLEMENTATION **once** in **one** translation unit *before*
 *   including this header to generate the implementation.
 *
 *   Compile & link with zlib:
 *     gcc -std=c99 your_app.c -lz
 *
 * License: MIT / 0-BSD – do whatever you want; attribution appreciated.
 */
#ifndef MZIP_H_
#define MZIP_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Include configuration settings */
#include "config.h"

/* Include compression algorithms based on config */
#ifdef MZIP_ENABLE_DEFLATE
#include <zlib.h>
#endif

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
zip_int64_t   zip_file_add     (zip_t *za, const char *name, zip_source_t *src, zip_flags_t flags);
int           zip_set_file_compression(zip_t *za, zip_uint64_t index, zip_int32_t comp, zip_uint32_t comp_flags);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ------------------------- implementation ------------------------------ */
#ifdef MZIP_IMPLEMENTATION

/* ---------------------------  internals  ------------------------------- */

/* Local/central directory signatures */
#define MZIP_SIG_LFH  0x04034b50u
#define MZIP_SIG_CDH  0x02014b50u
#define MZIP_SIG_EOCD 0x06054b50u

/* Forward declarations of helper functions */
static uint32_t mzip_write_local_header(FILE *fp, const char *name, uint32_t comp_method, 
                                     uint32_t comp_size, uint32_t uncomp_size);
static uint32_t mzip_write_central_header(FILE *fp, const char *name, uint32_t comp_method,
                                       uint32_t comp_size, uint32_t uncomp_size,
                                       uint32_t local_header_offset);
static void mzip_write_end_of_central_directory(FILE *fp, uint32_t num_entries, 
                                            uint32_t central_dir_size, uint32_t central_dir_offset);
static int mzip_finalize_archive(zip_t *za);

/* helper: little-endian readers/writers (ZIP format is little-endian) */
static uint16_t mzip_rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t mzip_rd32(const uint8_t *p) {
    return  (uint32_t)(p[0]        | (p[1] << 8) |
                       (p[2] << 16) | (p[3] << 24));
}
static void mzip_wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void mzip_wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* ----  internal helpers  ---- */

static int mzip_read_fully(FILE *fp, void *dst, size_t n) {
    return fread(dst, 1, n, fp) == n ? 0 : -1;
}

static int mzip_seek(FILE *fp, long offset, int whence) {
    return fseek(fp, offset, whence);
}

/* locate EOCD record (last 64KiB + 22 bytes) */
static long mzip_find_eocd(FILE *fp, uint8_t *eocd_out /*22+*/, size_t *cd_size, uint32_t *cd_ofs, uint16_t *total_entries) {
    long file_size;
    if (mzip_seek(fp, 0, SEEK_END) != 0)
        return -1;
    file_size = ftell(fp);
    if (file_size < 22)
        return -1;

    const size_t max_back = 0x10000 + 22; /* spec: comment <= 65535 */
    size_t search_len = (size_t)(file_size < (long)max_back ? file_size : max_back);

    if (mzip_seek(fp, file_size - (long)search_len, SEEK_SET) != 0)
        return -1;

    uint8_t *buf = (uint8_t*)malloc(search_len);
    if (!buf)
        return -1;
    if (mzip_read_fully(fp, buf, search_len) != 0) {
        free(buf);
        return -1;
    }

    for (size_t i = search_len - 22; i != (size_t)-1; --i) {
        if (mzip_rd32(buf + i) == MZIP_SIG_EOCD) {
            memcpy(eocd_out, buf + i, 22);
            *total_entries  = mzip_rd16(buf + i + 10);
            *cd_size        = mzip_rd32(buf + i + 12);
            *cd_ofs         = mzip_rd32(buf + i + 16);
            free(buf);
            return (long)(file_size - search_len + i);
        }
    }

    /* not found */
    free(buf);
    return -1;
}

/* parse central directory into array of mzip_entry */
static int mzip_load_central(zip_t *za) {
    uint8_t  eocd[22];
    size_t   cd_size;
    uint32_t cd_ofs;
    uint16_t n_entries;

    if (mzip_find_eocd(za->fp, eocd, &cd_size, &cd_ofs, &n_entries) < 0)
        return -1;

    /* read entire central directory */
    if (mzip_seek(za->fp, cd_ofs, SEEK_SET) != 0)
        return -1;

    uint8_t *cd_buf = (uint8_t*)malloc(cd_size);
    if (!cd_buf)
        return -1;
    if (mzip_read_fully(za->fp, cd_buf, cd_size) != 0) {
        free(cd_buf);
        return -1;
    }

    za->entries    = (struct mzip_entry*)calloc(n_entries, sizeof(struct mzip_entry));
    za->n_entries  = n_entries;

    if (!za->entries) {
        free(cd_buf);
        return -1;
    }

    size_t off = 0;
    for (uint16_t i = 0; i < n_entries; ++i) {
        if (off + 46 > cd_size || mzip_rd32(cd_buf + off) != MZIP_SIG_CDH) {
            free(cd_buf);
            return -1; /* malformed */
        }
        const uint8_t *h = cd_buf + off;

        uint16_t filename_len = mzip_rd16(h + 28);
        uint16_t extra_len    = mzip_rd16(h + 30);
        uint16_t comment_len  = mzip_rd16(h + 32);

        struct mzip_entry *e = &za->entries[i];
        e->method            = mzip_rd16(h + 10);
        e->comp_size         = mzip_rd32(h + 20);
        e->uncomp_size       = mzip_rd32(h + 24);
        e->local_hdr_ofs     = mzip_rd32(h + 42);

        e->name = (char*)malloc(filename_len + 1u);
        if (!e->name) {
            free(cd_buf);
            return -1;
        }
        memcpy(e->name, h + 46, filename_len);
        e->name[filename_len] = '\0';

        off += 46 + filename_len + extra_len + comment_len;
    }

    free(cd_buf);
    return 0;
}

/* load entire (uncompressed) file into memory and hand ownership to caller */
static int mzip_extract_entry(zip_t *za, struct mzip_entry *e, uint8_t **out_buf, uint32_t *out_sz) {
    /* move to local header */
    if (mzip_seek(za->fp, e->local_hdr_ofs, SEEK_SET) != 0)
        return -1;

    uint8_t lfh[30];
    if (mzip_read_fully(za->fp, lfh, 30) != 0)
        return -1;
    if (mzip_rd32(lfh) != MZIP_SIG_LFH)
        return -1;

    uint16_t fn_len   = mzip_rd16(lfh + 26);
    uint16_t extra_len= mzip_rd16(lfh + 28);

    /* skip filename + extra */
    if (mzip_seek(za->fp, fn_len + extra_len, SEEK_CUR) != 0)
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

        if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
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
        ubuf = (uint8_t*)malloc(e->uncomp_size);
        if (!ubuf) { free(cbuf); return -1; }

        z_stream strm = {0};
        strm.next_in   = cbuf;
        strm.avail_in  = e->comp_size;
        strm.next_out  = ubuf;
        strm.avail_out = e->uncomp_size;

        if (zstdDecompressInit(&strm) != Z_OK) {
            free(cbuf); free(ubuf); return -1;
        }
        int zret = zstdDecompress(&strm, Z_FINISH);
        zstdDecompressEnd(&strm);
        if (zret != Z_STREAM_END || strm.total_out != e->uncomp_size) {
            free(cbuf);
            free(ubuf);
            return -1;
        }
        free(cbuf);
    }
#endif
    else {
        free(cbuf);
        return -1; /* unsupported method */
    }

    *out_buf = ubuf;
    *out_sz  = e->uncomp_size;
    return 0;
}

/* --------------  public API implementation  --------------- */

zip_t *zip_open(const char *path, int flags, int *errorp) {
    zip_t *za = (zip_t*)calloc(1, sizeof(zip_t));
    if (!za) {
        if (errorp) *errorp = -1;
        return NULL;
    }

    const char *mode;
    int exists = 0;
    
    if (flags & ZIP_CREATE) {
        if ((flags & ZIP_EXCL) && (flags & ZIP_TRUNCATE)) {
            if (errorp) *errorp = -1; /* incompatible flags */
            free(za);
            return NULL;
        }
        
        /* Check if file exists */
        FILE *test = fopen(path, "rb");
        exists = (test != NULL);
        if (test) fclose(test);
        
        if (exists && (flags & ZIP_EXCL)) {
            if (errorp) *errorp = -1; /* file exists but EXCL set */
            free(za);
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

    FILE *fp = fopen(path, mode);
    if (!fp) {
        if (errorp) *errorp = -1;
        free(za);
        return NULL;
    }

    za->fp = fp;
    
    if (za->mode == 0 || (exists && !(flags & ZIP_TRUNCATE))) {
        /* Load central directory for existing archive */
        if (mzip_load_central(za) != 0) {
            zip_close(za);
            if (errorp) *errorp = -1;
            return NULL;
        }
        
        /* Set next_index for append mode */
        if (za->mode == 1) {
            za->next_index = za->n_entries;
        }
    }
    
    if (errorp) *errorp = 0;
    return za;
}

/* Add file to ZIP archive */
zip_int64_t zip_file_add(zip_t *za, const char *name, zip_source_t *src, zip_flags_t flags) {
    (void)flags;
    if (!za || !name || !src || za->mode != 1) return -1;
    
    /* Allocate a new entry */
    struct mzip_entry *new_entries;
    new_entries = realloc(za->entries, (za->n_entries + 1) * sizeof(struct mzip_entry));
    if (!new_entries) return -1;
    za->entries = new_entries;
    
    /* Set up the new entry */
    struct mzip_entry *e = &za->entries[za->n_entries];
    memset(e, 0, sizeof(struct mzip_entry));
    
    e->name = strdup(name);
    if (!e->name) return -1;
    
    /* Store uncompressed by default */
    e->method = 0; 
    e->comp_size = (uint32_t)src->len;
    e->uncomp_size = (uint32_t)src->len;
    
    /* Get current position for local header offset */
    long current_pos = ftell(za->fp);
    if (current_pos < 0) {
        free(e->name);
        return -1;
    }
    
    e->local_hdr_ofs = (uint32_t)current_pos;
    
    /* Write local file header */
    mzip_write_local_header(za->fp, e->name, e->method, e->comp_size, e->uncomp_size);
    
    /* Write data */
    fwrite(src->buf, 1, src->len, za->fp);
    
    /* Free source data if requested */
    if (src->freep) {
        free((void*)src->buf);
    }
    free(src);
    
    /* Increment entry count */
    zip_uint64_t index = za->n_entries;
    za->n_entries++;
    za->next_index = za->n_entries;
    
    return (zip_int64_t)index;
}

/* Set file compression method */
int zip_set_file_compression(zip_t *za, zip_uint64_t index, zip_int32_t comp, zip_uint32_t comp_flags) {
    (void)comp_flags;
    
    if (!za || index >= za->n_entries || za->mode != 1) return -1;
    
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
    else {
        /* Unsupported compression method */
        return -1;
    }
    
    za->entries[index].method = (uint16_t)comp;
    return 0;
}

/* Finalize ZIP file before closing - write central directory */
static int mzip_finalize_archive(zip_t *za) {
    if (!za || !za->fp || za->mode != 1) return -1;
    
    /* Get offset for central directory */
    long cd_offset = ftell(za->fp);
    if (cd_offset < 0) return -1;
    
    /* Write central directory headers */
    uint32_t cd_size = 0;
    for (zip_uint64_t i = 0; i < za->n_entries; i++) {
        struct mzip_entry *e = &za->entries[i];
        cd_size += mzip_write_central_header(za->fp, e->name, e->method, 
                                          e->comp_size, e->uncomp_size, e->local_hdr_ofs);
    }
    
    /* Write end of central directory record */
    mzip_write_end_of_central_directory(za->fp, (uint32_t)za->n_entries, cd_size, (uint32_t)cd_offset);
    
    return 0;
}

int zip_close(zip_t *za) {
    if (!za) return -1;
    
    /* Finalize archive if in write mode */
    if (za->mode == 1) {
        mzip_finalize_archive(za);
    }
    
    if (za->fp) fclose(za->fp);
    for (zip_uint64_t i = 0; i < za->n_entries; ++i) {
        free(za->entries[i].name);
    }
    free(za->entries);
    free(za);
    return 0;
}

zip_uint64_t zip_get_num_files(zip_t *za) {
    return za ? za->n_entries : 0u;
}

zip_int64_t zip_name_locate(zip_t *za, const char *fname, zip_flags_t flags) {
    (void)flags; /* flags (case sensitivity etc.) not implemented */
    if (!za || !fname) return -1;

    for (zip_uint64_t i = 0; i < za->n_entries; ++i) {
        if (strcmp(za->entries[i].name, fname) == 0)
            return (zip_int64_t)i;
    }
    return -1;
}

zip_file_t *zip_fopen_index(zip_t *za, zip_uint64_t index, zip_flags_t flags) {
    (void)flags;
    if (!za || index >= za->n_entries)
        return NULL;

    uint8_t  *buf;
    uint32_t  sz;
    if (mzip_extract_entry(za, &za->entries[index], &buf, &sz) != 0)
        return NULL;

    zip_file_t *zf = (zip_file_t*)malloc(sizeof(zip_file_t));
    if (!zf) { free(buf); return NULL; }
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
                                       uint32_t comp_size, uint32_t uncomp_size) {
    uint16_t filename_len = (uint16_t)strlen(name);
    uint8_t header[30];
    
    /* Write local file header signature */
    mzip_wr32(header, MZIP_SIG_LFH);
    
    /* Version needed to extract (2.0) */
    mzip_wr16(header + 4, 20);
    
    /* General purpose bit flag */
    mzip_wr16(header + 6, 0);
    
    /* Compression method */
    mzip_wr16(header + 8, comp_method);
    
    /* Last mod file time & date (dummy values) */
    mzip_wr16(header + 10, 0);
    mzip_wr16(header + 12, 0);
    
    /* CRC-32 */
    mzip_wr32(header + 14, 0); /* Will be updated after data is written */
    
    /* Compressed size */
    mzip_wr32(header + 18, comp_size);
    
    /* Uncompressed size */
    mzip_wr32(header + 22, uncomp_size);
    
    /* File name length */
    mzip_wr16(header + 26, filename_len);
    
    /* Extra field length */
    mzip_wr16(header + 28, 0);
    
    /* Write header */
    fwrite(header, 1, sizeof(header), fp);
    
    /* Write filename */
    fwrite(name, 1, filename_len, fp);
    
    return 30 + filename_len;
}

/* Helper function to write central directory header */
static uint32_t mzip_write_central_header(FILE *fp, const char *name, uint32_t comp_method,
                                         uint32_t comp_size, uint32_t uncomp_size,
                                         uint32_t local_header_offset) {
    uint16_t filename_len = (uint16_t)strlen(name);
    uint8_t header[46];
    
    /* Central directory file header signature */
    mzip_wr32(header, MZIP_SIG_CDH);
    
    /* Version made by (UNIX, version 2.0) */
    mzip_wr16(header + 4, 0x031e); 
    
    /* Version needed to extract (2.0) */
    mzip_wr16(header + 6, 20);
    
    /* General purpose bit flag */
    mzip_wr16(header + 8, 0);
    
    /* Compression method */
    mzip_wr16(header + 10, comp_method);
    
    /* Last mod file time & date (dummy values) */
    mzip_wr16(header + 12, 0);
    mzip_wr16(header + 14, 0);
    
    /* CRC-32 */
    mzip_wr32(header + 16, 0);
    
    /* Compressed size */
    mzip_wr32(header + 20, comp_size);
    
    /* Uncompressed size */
    mzip_wr32(header + 24, uncomp_size);
    
    /* File name length */
    mzip_wr16(header + 28, filename_len);
    
    /* Extra field length */
    mzip_wr16(header + 30, 0);
    
    /* File comment length */
    mzip_wr16(header + 32, 0);
    
    /* Disk number start */
    mzip_wr16(header + 34, 0);
    
    /* Internal file attributes */
    mzip_wr16(header + 36, 0);
    
    /* External file attributes */
    mzip_wr32(header + 38, 0);
    
    /* Relative offset of local header */
    mzip_wr32(header + 42, local_header_offset);
    
    /* Write header */
    fwrite(header, 1, sizeof(header), fp);
    
    /* Write filename */
    fwrite(name, 1, filename_len, fp);
    
    return 46 + filename_len;
}

/* Helper function to write end of central directory record */
static void mzip_write_end_of_central_directory(FILE *fp, uint32_t num_entries, 
                                              uint32_t central_dir_size, uint32_t central_dir_offset) {
    uint8_t eocd[22];
    
    /* End of central directory signature */
    mzip_wr32(eocd, MZIP_SIG_EOCD);
    
    /* Number of this disk */
    mzip_wr16(eocd + 4, 0);
    
    /* Number of the disk with the start of the central directory */
    mzip_wr16(eocd + 6, 0);
    
    /* Total number of entries in the central directory on this disk */
    mzip_wr16(eocd + 8, num_entries);
    
    /* Total number of entries in the central directory */
    mzip_wr16(eocd + 10, num_entries);
    
    /* Size of the central directory */
    mzip_wr32(eocd + 12, central_dir_size);
    
    /* Offset of start of central directory with respect to the starting disk number */
    mzip_wr32(eocd + 16, central_dir_offset);
    
    /* .ZIP file comment length */
    mzip_wr16(eocd + 20, 0);
    
    /* Write end of central directory record */
    fwrite(eocd, 1, sizeof(eocd), fp);
}

zip_source_t *zip_source_buffer(zip_t *za, const void *data, zip_uint64_t len, int freep) {
    (void)za;
    zip_source_t *src = (zip_source_t*)malloc(sizeof(zip_source_t));
    if (!src) return NULL;
    src->buf = data;
    src->len = len;
    src->freep = freep;
    return src;
}

#endif /* MZIP_IMPLEMENTATION */

#endif /* MZIP_H_ */
