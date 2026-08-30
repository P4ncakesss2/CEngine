#include "pak_writer.h"
#include "pak_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zstd.h"

#define PAK_ZSTD_LEVEL 19

struct PakWriter {
    FILE      *f;
    PakEntry  *entries;
    uint32_t   count;
    uint32_t   capacity;
    bool       error;
};

PakWriter *pak_writer_open(const char *path) {
    PakWriter *pw = calloc(1, sizeof(PakWriter));
    if (!pw) return NULL;

    pw->f = fopen(path, "wb");
    if (!pw->f) { free(pw); return NULL; }

    PakHeader placeholder = {0};
    if (fwrite(&placeholder, sizeof(placeholder), 1, pw->f) != 1) {
        fclose(pw->f);
        free(pw);
        return NULL;
    }
    return pw;
}

static bool name_exists(PakWriter *pw, const char *name) {
    for (uint32_t i = 0; i < pw->count; i++)
        if (strncmp(pw->entries[i].name, name, PAK_MAX_NAME) == 0) return true;
    return false;
}

bool pak_writer_add(PakWriter *pw, const char *name, const void *data, size_t size) {
    if (!pw || pw->error) return false;
    if (strlen(name) >= PAK_MAX_NAME) return false;
    if (name_exists(pw, name)) return false;

    if (pw->count == pw->capacity) {
        uint32_t new_cap = pw->capacity ? pw->capacity * 2 : 16;
        PakEntry *p = realloc(pw->entries, new_cap * sizeof(PakEntry));
        if (!p) { pw->error = true; return false; }
        pw->entries = p;
        pw->capacity = new_cap;
    }

    long offset = ftell(pw->f);
    if (offset < 0) { pw->error = true; return false; }

    /* Try to compress; fall back to storing raw if compression doesn't help
     * (e.g. tiny files, or data that's already high-entropy) or fails. */
    const void *write_data = data;
    size_t      write_size = size;
    uint32_t    compressed_size = 0; /* 0 = stored raw */
    void       *cbuf = NULL;

    if (size > 0) {
        size_t bound = ZSTD_compressBound(size);
        cbuf = malloc(bound);
        if (cbuf) {
            size_t csize = ZSTD_compress(cbuf, bound, data, size, PAK_ZSTD_LEVEL);
            if (!ZSTD_isError(csize) && csize < size) {
                write_data = cbuf;
                write_size = csize;
                compressed_size = (uint32_t)csize;
            }
        }
    }

    if (write_size && fwrite(write_data, 1, write_size, pw->f) != write_size) {
        free(cbuf);
        pw->error = true;
        return false;
    }
    free(cbuf);

    PakEntry *e = &pw->entries[pw->count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, PAK_MAX_NAME - 1);
    e->offset = (uint32_t)offset;
    e->size = (uint32_t)size;
    e->compressed_size = compressed_size;
    return true;
}

bool pak_writer_close(PakWriter *pw) {
    if (!pw) return false;
    bool ok = !pw->error;

    long index_offset = 0;
    if (ok) {
        index_offset = ftell(pw->f);
        if (index_offset < 0) ok = false;
    }
    if (ok && pw->count &&
        fwrite(pw->entries, sizeof(PakEntry), pw->count, pw->f) != pw->count) {
        ok = false;
    }

    if (ok) {
        PakHeader header = {
            .magic = PAK_MAGIC,
            .version = PAK_VERSION,
            .file_count = pw->count,
            .index_offset = (uint32_t)index_offset,
        };
        if (fseek(pw->f, 0, SEEK_SET) != 0 ||
            fwrite(&header, sizeof(header), 1, pw->f) != 1) {
            ok = false;
        }
    }

    if (fclose(pw->f) != 0) ok = false;
    free(pw->entries);
    free(pw);
    return ok;
}