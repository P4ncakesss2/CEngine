#define _POSIX_C_SOURCE 200809L
#include "mount_pak.h"
#include "pak/pak_format.h"
#include "pak/pak_reader.h"
#include "pak/pak_writer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char      *path;
    PakReader *reader;
} PakMountState;

static bool pak_mount_exists(void *impl, const char *vpath) {
    PakMountState *s = (PakMountState *)impl;
    return pak_reader_stat(s->reader, vpath, NULL);
}

static bool pak_mount_read(void *impl, const char *vpath, void **out_data, size_t *out_size) {
    PakMountState *s = (PakMountState *)impl;
    size_t size = 0;
    void *buf = pak_reader_read_alloc(s->reader, vpath, &size);
    if (!buf) return false;
    *out_data = buf;
    *out_size = size;
    return true;
}

static void pak_mount_close(void *impl) {
    PakMountState *s = (PakMountState *)impl;
    if (!s) return;
    pak_reader_close(s->reader);
    free(s->path);
    free(s);
}

static const MountOps g_pak_mount_ops = {
    .exists = pak_mount_exists,
    .read = pak_mount_read,
    .write = NULL,
    .close = pak_mount_close,
    .name = "pak",
};

bool mount_pak_create(const char *pak_path, Mount *out) {
    PakReader *r = pak_reader_open(pak_path);
    if (!r) return false;

    PakMountState *s = malloc(sizeof(PakMountState));
    if (!s) { pak_reader_close(r); return false; }

    s->path = strdup(pak_path);
    if (!s->path) { free(s); pak_reader_close(r); return false; }
    s->reader = r;

    out->impl = s;
    out->ops = &g_pak_mount_ops;
    return true;
}
