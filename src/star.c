/*
 * <limits.h>
 *  CHAR_BIT
 *  UCHAR_MAX
 *
 * <stdio.h>
 *  FILE
 *  fread()
 *  fseek()
 *  ftell()
 *  fwrite()
 *
 * <stdlib.h>
 *  bsearch()
 *  calloc()
 *  free()
 *  malloc()
 *
 * <string.h>
 *  memcmp()
 *  strcmp()
 *  strlen()
 *  strncmp()
 */
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * <utils/ifjmp.h>
 *  ifjmp()
 */
#include <utils/ifjmp.h>

#include "star.h"

#define star_max(x, y) (((x) > (y)) ? (x) : (y))
#define star_min(x, y) (((x) < (y)) ? (x) : (y))

const u8 MAGIC[4] = { 0x53, 0x54, 0x41, 0x52 };

/***********************************************************
 * utility functions
 **********************************************************/

/*
 * `_star_uint_width_encode()` and `_star_uint_width_decode()`
 * from http://www.iso-9899.info/wiki/Temp
 */

/**
 * @brief Serialize @a in with @a width into @a out
 * @param out Where to serialize to
 * @param in The integer to serialize
 * @param width The witdh of the integer to serialize
 */
static void _star_uint_width_encode (u8 * out, u64 in, size_t width)
{
    for (size_t i = 0; i < width; i++)
        out[i] = (in >> (i * CHAR_BIT)) & UCHAR_MAX;
}

/**
 * @brief Deserialize @a out from @a in with @a width
 * @param out Where to deserialize to
 * @param in The data to deserialize from
 * @param width The witdh of the integer to deserialize
 */
static void _star_uint_width_decode (u64 * out, u8 * in, size_t width)
{
    *out = 0;
    for (size_t i = 0; i < width; i++)
        *out |= (u64) in[i] << (i * CHAR_BIT);
}

bool star_check_header (const struct star_file * self)
{
    return (self != NULL) ?
        memcmp(self->header.magic, MAGIC, 4) == 0 :
        false ;
}

//
// This function works only on certain circumstances.
//
// # Use case:
//
// Given a directory tree similar to the following:
//
//   ```
//   directory/
//   ├── file1
//   ├── file2
//   ├── file3
//   ├── file4
//   ├── file5
//   ├── file6
//   ├── file7
//   ├── file8
//   ├── file9
//   ├── file10
//   └── file11
//   ```
//
// and calling a `program` this way:
//
//   ```sh
//   program directory/*
//   ```
//
// `argv` will look like:
//
//   ```c
//   argv = {
//       [0]  = "program",
//       [1]  = "directory/file1",
//       [2]  = "directory/file10",
//       [3]  = "directory/file11",
//       [4]  = "directory/file2",
//       [5]  = "directory/file3",
//       [6]  = "directory/file4",
//       [7]  = "directory/file5",
//       [8]  = "directory/file6",
//       [9]  = "directory/file7",
//       [10] = "directory/file8",
//       [11] = "directory/file9",
//   }
//   ```
//
// This isn't ideal if you want to process these files in
// order.
//
// # Assumptions
//
// The strings to be compared are composed of common a
// prefix and a number after that prefix (`pn`). It
// doesn't matter what the prefix is.
//
// e.g.:
//   pre1 < pre2 < pre10
//
// # Limitations
//
// The numbers aren't read nor compared as numbers, so the
// same number with a different representation will not
// give the correct order.
//
// e.g.:
//   1 < 01 < 001
//   2 < 01 < 000
//
int star_strcmp (const void * _l, const void * _r)
{
    const char * l = _l;
    const char * r = _r;

    /* `l.len == r.len` <=> `l.len - r.len == 0` */
    size_t nl = strlen(l);
    size_t nr = strlen(r);
    int diff = nl - nr;

    return (diff == 0) ?
        strcmp(l, r) :
        diff ;
}

void star_free (struct star_file * self)
{
    if (self == NULL)
        return;

    u64 n = self->header.nfiles;

    if (self->fheaders != NULL) {
        for (u64 i = 0; i < n; i++)
            if (self->fheaders[i].path != NULL)
                free(self->fheaders[i].path);

        free(self->fheaders);
    }

    if (self->fdata != NULL) {
        for (u64 i = 0; i < n; i++)
            if (self->fdata[i] != NULL)
                free(self->fdata[i]);

        free(self->fdata);
    }

    free(self);
}

/***********************************************************
 * read functions (assume `in` was opened in read mode)
 **********************************************************/

bool star_read_header (struct star_file * self, FILE * in)
{
    bool ret = false;

    ifjmp(self == NULL, out);
    ifjmp(in == NULL, out);

    u64 r = fread(&self->header, sizeof(struct star_header), 1, in);
    ifjmp(r != 1, out);

    ret = star_check_header(self);

out:
    return ret;
}

u64 star_read_fheaders (struct star_file * self, FILE * in)
{
    u64 ret = 0;

    ifjmp(self == NULL, out);
    ifjmp(in == NULL, out);

    u64 nfiles = self->header.nfiles;

    /* if fheaders already has memory, use it */
    struct star_file_header * fheaders = (self->fheaders == NULL) ?
        calloc(self->header.nfiles, sizeof(struct star_file_header)) :
        self->fheaders ;
    ifjmp(fheaders == NULL, out);

    /* assume `fheaders` has enough space  */
    for (ret = 0; ret < nfiles; ret++) {
        /*
         * dont change original memory if possible,
         * write only at the end of the loop
         */
        struct star_file_header tmp;

        { /* read `size,` `offset` and `path_len` */
            void * ptr = &tmp;
            size_t size = sizeof(u64);
            size_t nmemb = 3;

            u64 r = fread(ptr, size, nmemb, in);

            if (r != nmemb)
                break;
        }

        { /* alloc path */
            tmp.path = malloc(tmp.path_len);

            if (tmp.path == NULL)
                break;
        }

        { /* read path */
            void * ptr = tmp.path;
            size_t size = sizeof(u8);
            size_t nmemb = tmp.path_len;

            u64 r = fread(ptr, size, nmemb, in);

            /* we alloc so we have to free in case of error */
            if (r != nmemb) {
                free(ptr);
                break;
            }
        }

        fheaders[ret] = tmp;
    }

    self->fheaders = fheaders;

out:
    return ret;
}

u64 star_read_fdata (struct star_file * self, FILE * in)
{
    u64 ret = 0;

    ifjmp(self == NULL, out);
    ifjmp(in == NULL, out);

    u8 ** fdata = (self->fdata == NULL) ?
        calloc(self->header.nfiles, sizeof(u8 *)) :
        self->fdata ;
    ifjmp(fdata == NULL, out);

    /* assume `fdata` has enough space */
    for (ret = 0; ret < self->header.nfiles; ret++) {
        size_t size = self->fheaders[ret].size;
        u8 * tmp = malloc(size);
        if (tmp == NULL)
            break;

        u64 r = fread(tmp, size, 1, in);

        /* we alloc so we have to free in case of error */
        if (r != 1) {
            free(tmp);
            break;
        }

        fdata[ret] = tmp;
    }

    self->fdata = fdata;

out:
    return ret;
}

struct star_file * star_read (FILE * in)
{
    struct star_file * ret = NULL;
    struct star_file _tmp = {0};

    /* failed to read header or `in` is not a STAR file */
    ifjmp(!star_read_header(&_tmp, in), out);

    {
        ret = malloc(sizeof(struct star_file));
        ifjmp(ret == NULL, out);

        *ret = _tmp;
    }

    /* number of successfully read file headers */
    u64 nfheaders = star_read_fheaders(ret, in);
    ifjmp(ret->header.nfiles != nfheaders, ko_fheaders);

    /* number of successfully read files */
    u64 nfdata = star_read_fdata(ret, in);
    ifjmp(ret->header.nfiles != nfdata, ko_fdata);

out:
    return ret;

ko_fdata:
    for (u64 i = 0; i < nfdata; i++)
        free(ret->fdata[i]);
    free(ret->fdata);

ko_fheaders:
    for (u64 i = 0; i < nfheaders; i++)
        free(ret->fheaders[i].path);
    free(ret->fheaders);

    free(ret);
    ret = NULL;
    goto out;
}

/***********************************************************
 * write functions (assume `out` was opened in write mode)
 **********************************************************/

bool star_write (const struct star_file * self, FILE * out)
{
#define fwrite_ifjmp(PTR, SIZE, NMEMB, STREAM, LBL) \
    ifjmp((fwrite((PTR), (SIZE), (NMEMB), (STREAM)) != (NMEMB)), LBL)

    bool ret = false;

    ifjmp(self == NULL, out);
    ifjmp(out == NULL, out);
    ifjmp(!star_check_header(self), out);
    ifjmp(self->fheaders == NULL, out);
    ifjmp(self->fdata == NULL, out);

    /* assume both `self->fheaders` and `self->fdata` have the correct size */
    for (u64 i = 0; i < self->header.nfiles; i++) {
        ifjmp(self->fdata[i] == NULL, out);
        ifjmp(self->fheaders[i].path == NULL, out);
        ifjmp((strlen((void *) self->fheaders[i].path) + 1) != self->fheaders[i].path_len, out);
    }

    /* write STAR header */
    fwrite_ifjmp(&self->header,
            sizeof(struct star_header),
            1,
            out,
            out);

    /* write file headers */
    for (u64 i = 0; i < self->header.nfiles; i++) {
        /* write `size`, `offset` and `path_len` */
        fwrite_ifjmp(self->fheaders + i,
                sizeof(u64),
                3,
                out,
                out);

        /* write `path` */
        fwrite_ifjmp(self->fheaders[i].path,
                sizeof(u8),
                self->fheaders[i].path_len,
                out,
                out);
    }

    /* write file data */
    for (u64 i = 0; i < self->header.nfiles; i++)
        fwrite_ifjmp(self->fdata[i],
                self->fheaders[i].size,
                1,
                out,
                out);

    ret = true;

out:
    return ret;

#undef fwrite_ifjmp
}

/***********************************************************
 * create functions
 **********************************************************/
struct star_file * star_new (u64 nfiles)
{
    struct star_file * ret = NULL;
    struct star_file _tmp = {0};

    ifjmp(nfiles == 0, out);

    _tmp.fdata = calloc(nfiles, sizeof(u8 *));
    _tmp.fheaders = calloc(nfiles, sizeof(struct star_file_header));
    ret = malloc(sizeof(struct star_file));

    ifjmp(_tmp.fdata == NULL, ko);
    ifjmp(_tmp.fheaders == NULL, ko);
    ifjmp(ret == NULL, ko);

    memcpy(&_tmp.header.magic, MAGIC, 4);
    _tmp.header.nfiles = nfiles;

    *ret = _tmp;

out:
    return ret;

ko:
    if (ret != NULL)
        free(ret);

    if (_tmp.fdata != NULL)
        free(_tmp.fdata);

    if (_tmp.fheaders != NULL)
        free(_tmp.fheaders);

    goto out;
}

bool star_add_file (struct star_file * self, u64 idx, const u8 * path, u64 size, FILE * in)
{
    bool ret = false;
    u8 * fdata = NULL;
    struct star_file_header fheader = {0};

    ifjmp(self == NULL, out);
    ifjmp(in == NULL, out);
    ifjmp(path == NULL, out);
    ifjmp(idx >= self->header.nfiles, out);

    { /* file data */
        fdata = malloc(size);
        ifjmp(fdata == NULL, out);

        u64 r = fread(fdata, size, 1, in);
        ifjmp(r != 1, ko);
    }

    { /* path */
        fheader.path = (void *) strdup((void *) path);
        ifjmp(fheader.path == NULL, ko);
        fheader.path_len = strlen((void *) fheader.path) + 1;
    }

    fheader.size = size;

    self->fdata[idx] = fdata;
    self->fheaders[idx] = fheader;

    ret = true;

out:
    return ret;

ko:
    if (fdata != NULL)
        free(fdata);

    if (fheader.path != NULL)
        free(fheader.path);

    goto out;
}

/*
 * assume self is a complete `struct star_file`, ready to
 * write, except for the file offsets
 */
bool star_file_offsets (struct star_file * self)
{
    bool ret = false;

    ifjmp(self == NULL, out);

    u64 offset = 0;

    /*
     * offset from the beggining of the STAR file to the
     * beggining of stored files' data
     */
    {
        offset += sizeof(struct star_header);
        offset += self->header.nfiles * sizeof(struct star_file_header);

        for (u64 i = 0; i < self->header.nfiles; i++)
            offset += self->fheaders[i].path_len;

        /* subtract size of pointers */
        offset -= self->header.nfiles * sizeof(u8 *);
    }

    /* beggining of fdata */
    self->fheaders[0].offset = offset;

    for (u64 i = 1; i < self->header.nfiles; i++)
        self->fheaders[i].offset = self->fheaders[i - 1].offset + self->fheaders[i - 1].size;

    ret = true;

out:
    return ret;
}

/***********************************************************
 * search functions
 **********************************************************/
u64 star_search (const struct star_file * self, const u8 * fname)
{
    bool match = false;
    u64 ret = 0;

    ifjmp(self == NULL, out);
    ifjmp(self->fheaders == NULL, out);
    ifjmp(fname == NULL, out);

    size_t fl = strlen((void *) fname);

    for (ret = 0; ret < self->header.nfiles; ret++) {
        u64 n = self->fheaders[ret].path_len - 1;

        if (n != fl)
            continue;

        n = star_min(fl, n);

        match = strncmp((void *) fname, (void *) self->fheaders[ret].path, n) == 0;

        if (match)
            break;
    }

out:
    return (!match) ?
        UINT64_MAX :
        ret ;
}

/**
 * @brief Compare @a path with a file header @a fh
 * @param path Path to look for on @a fh
 * @param fh A file header
 * @returns Similar to `strcmp()`
 */
static int _star_compar_path (const void * path, const void * fh)
{
    return (path != NULL && fh != NULL) ?
        star_strcmp(path, ((const struct star_file_header *) fh)->path) :
        0 ;
}

/* FIXME: Wrong matches, sometimes SEGV */
u64 star_bsearch (const struct star_file * self, const u8 * fname)
{
    u64 ret = UINT64_MAX;

    ifjmp(self == NULL, out);
    ifjmp(self->fheaders == NULL, out);
    ifjmp(fname == NULL, out);

    const void * key = fname;
    const struct star_file_header * base = self->fheaders;
    size_t nmemb = self->header.nfiles;
    size_t size = sizeof(struct star_file_header);
    const struct star_file_header * match = bsearch(key, base, nmemb, size, _star_compar_path);

    ret = (match != NULL) ?
        (u64) (match - base) :
        UINT64_MAX;

out:
    return ret;
}
