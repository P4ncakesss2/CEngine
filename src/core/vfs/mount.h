#ifndef MOUNT_H
#define MOUNT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t MountHandle;
#define MOUNT_HANDLE_INVALID ((MountHandle)0)

typedef struct {
    bool (*exists)(void *impl, const char *vpath);
    bool (*read)(void *impl, const char *vpath, void **out_data, size_t *out_size);
    bool (*write)(void *impl, const char *vpath, const void *data, size_t size);
    void (*close)(void *impl);
    const char *name;
} MountOps;

typedef struct {
    void           *impl;
    const MountOps *ops;
} Mount;

#endif
