#include "pak_reader.h"
#include "pak_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zstd.h"

struct PakReader {
    FILE     *f;
    PakEntry *entries;
    uint32_t  count;
    int32_t  *hash_slots;
    uint32_t  hash_size;
};

static uint32_t fnv1a(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

static size_t name_len(const char *name) {
    size_t n = 0;
    while (n < PAK_MAX_NAME && name[n]) n++;
    return n;
}

static bool build_hash_index(PakReader *pr) {
    uint32_t size = 16;
    while (size < pr->count * 2 + 1) size *= 2;

    int32_t *slots = malloc(size * sizeof(int32_t));
    if (!slots) return false;
    for (uint32_t i = 0; i < size; i++) slots[i] = -1;

    for (uint32_t i = 0; i < pr->count; i++) {
        uint32_t h = fnv1a(pr->entries[i].name, name_len(pr->entries[i].name)) & (size - 1);
        while (slots[h] != -1) h = (h + 1) & (size - 1);
        slots[h] = (int32_t)i;
    }

    pr->hash_slots = slots;
    pr->hash_size = size;
    return true;
}

PakReader *pak_reader_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    PakHeader header;
    if (fread(&header, sizeof(header), 1, f) != 1 ||
        header.magic != PAK_MAGIC || header.version != PAK_VERSION) {
        fclose(f);
        return NULL;
    }

    PakEntry *entries = NULL;
    if (header.file_count) {
        entries = malloc(header.file_count * sizeof(PakEntry));
        if (!entries) { fclose(f); return NULL; }
        if (fseek(f, (long)header.index_offset, SEEK_SET) != 0 ||
            fread(entries, sizeof(PakEntry), header.file_count, f) != header.file_count) {
            free(entries);
            fclose(f);
            return NULL;
        }
    }

    PakReader *pr = calloc(1, sizeof(PakReader));
    if (!pr) { free(entries); fclose(f); return NULL; }
    pr->f = f;
    pr->entries = entries;
    pr->count = header.file_count;

    if (pr->count && !build_hash_index(pr)) {
        free(entries);
        fclose(f);
        free(pr);
        return NULL;
    }

    return pr;
}

void pak_reader_close(PakReader *pr) {
    if (!pr) return;
    if (pr->f) fclose(pr->f);
    free(pr->entries);
    free(pr->hash_slots);
    free(pr);
}

static PakEntry *find_entry(PakReader *pr, const char *name) {
    if (!pr->count) return NULL;
    size_t len = name_len(name);
    uint32_t h = fnv1a(name, len) & (pr->hash_size - 1);

    for (;;) {
        int32_t slot = pr->hash_slots[h];
        if (slot == -1) return NULL;
        PakEntry *e = &pr->entries[slot];
        if (name_len(e->name) == len && memcmp(e->name, name, len) == 0) return e;
        h = (h + 1) & (pr->hash_size - 1);
    }
}

bool pak_reader_stat(PakReader *pr, const char *name, size_t *out_size) {
    if (!pr) return false;
    PakEntry *e = find_entry(pr, name);
    if (!e) return false;
    if (out_size) *out_size = e->size;
    return true;
}

void *pak_reader_read_alloc(PakReader *pr, const char *name, size_t *out_size) {
    if (!pr) return NULL;
    PakEntry *e = find_entry(pr, name);
    if (!e) return NULL;

    if (fseek(pr->f, (long)e->offset, SEEK_SET) != 0) return NULL;

    if (!e->compressed_size) {
        void *buf = malloc(e->size ? e->size : 1);
        if (!buf) return NULL;
        if (e->size && fread(buf, 1, e->size, pr->f) != e->size) {
            free(buf);
            return NULL;
        }
        if (out_size) *out_size = e->size;
        return buf;
    }

    void *raw = malloc(e->compressed_size);
    if (!raw) return NULL;
    if (fread(raw, 1, e->compressed_size, pr->f) != e->compressed_size) {
        free(raw);
        return NULL;
    }

    void *buf = malloc(e->size ? e->size : 1);
    if (!buf) { free(raw); return NULL; }

    size_t dsize = ZSTD_decompress(buf, e->size, raw, e->compressed_size);
    free(raw);
    if (ZSTD_isError(dsize) || dsize != e->size) {
        free(buf);
        return NULL;
    }

    if (out_size) *out_size = e->size;
    return buf;
}

uint32_t pak_reader_count(PakReader *pr) {
    return pr ? pr->count : 0;
}

const char *pak_reader_name_at(PakReader *pr, uint32_t index) {
    if (!pr || index >= pr->count) return NULL;
    return pr->entries[index].name;
}