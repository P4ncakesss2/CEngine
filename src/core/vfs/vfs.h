#ifndef VFS_H
#define VFS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "mount.h"

typedef enum {
    VFS_OK = 0,
    VFS_ERR_OUT_OF_MEMORY,
    VFS_ERR_MOUNT_FAILED,
    VFS_ERR_NOT_FOUND,
    VFS_ERR_WRITE_UNSUPPORTED,
    VFS_ERR_INVALID_ARGUMENT,
} VfsResult;

const char *vfs_result_str(VfsResult result);

typedef struct Vfs {
    Mount   *mounts;
    uint32_t count;
    uint32_t capacity;
    MountHandle *handles;
    uint32_t  nextHandle;
} Vfs;

VfsResult vfs_init(Vfs *vfs);
void vfs_free(Vfs *vfs);

VfsResult vfs_mount_dir(Vfs *vfs, const char *dir_path, MountHandle *out_handle);
VfsResult vfs_mount_pak(Vfs *vfs, const char *pak_path, MountHandle *out_handle);
VfsResult vfs_unmount(Vfs *vfs, MountHandle handle);

bool vfs_exists(Vfs *vfs, const char *vpath);

VfsResult vfs_read_file(Vfs *vfs, const char *vpath, void **out_data, size_t *out_size);
VfsResult vfs_write_file(Vfs *vfs, const char *vpath, const void *data, size_t size);

#endif
