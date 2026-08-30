#define _POSIX_C_SOURCE 200809L
#include "mount_dir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dir_join(const char *dir, const char *vpath) {
    size_t len = strlen(dir) + 1 + strlen(vpath) + 1;
    char *out = malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s/%s", dir, vpath);
    return out;
}

static bool dir_exists(void *impl, const char *vpath) {
    char *full = dir_join((const char *)impl, vpath);
    if (!full) return false;
    FILE *f = fopen(full, "rb");
    free(full);
    if (!f) return false;
    fclose(f);
    return true;
}

static bool dir_read(void *impl, const char *vpath, void **out_data, size_t *out_size) {
    char *full = dir_join((const char *)impl, vpath);
    if (!full) return false;
    FILE *f = fopen(full, "rb");
    free(full);
    if (!f) return false;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }

    void *buf = malloc(size ? (size_t)size : 1);
    if (!buf) { fclose(f); return false; }
    if (size && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return false;
    }
    fclose(f);

    *out_data = buf;
    *out_size = (size_t)size;
    return true;
}

static bool dir_write(void *impl, const char *vpath, const void *data, size_t size) {
    char *full = dir_join((const char *)impl, vpath);
    if (!full) return false;
    FILE *f = fopen(full, "wb");
    free(full);
    if (!f) return false;

    bool ok = (size == 0) || (fwrite(data, 1, size, f) == size);
    if (fclose(f) != 0) ok = false;
    return ok;
}

static void dir_close(void *impl) {
    free(impl);
}

static const MountOps g_dir_mount_ops = {
    .exists = dir_exists,
    .read = dir_read,
    .write = dir_write,
    .close = dir_close,
    .name = "dir",
};

bool mount_dir_create(const char *dir_path, Mount *out) {
    char *copy = strdup(dir_path);
    if (!copy) return false;
    out->impl = copy;
    out->ops = &g_dir_mount_ops;
    return true;
}
