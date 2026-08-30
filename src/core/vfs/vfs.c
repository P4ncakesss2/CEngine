#include "vfs.h"
#include "mount.h"
#include "mount_dir.h"
#include "mount_pak.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const char *vfs_result_str(VfsResult result) {
    switch (result) {
        case VFS_OK:                     return "ok";
        case VFS_ERR_OUT_OF_MEMORY:      return "out of memory";
        case VFS_ERR_MOUNT_FAILED:       return "mount failed";
        case VFS_ERR_NOT_FOUND:          return "not found";
        case VFS_ERR_WRITE_UNSUPPORTED:  return "write not supported by any mount";
        case VFS_ERR_INVALID_ARGUMENT:   return "invalid argument";
    }
    return "unknown vfs error";
}

VfsResult vfs_init(Vfs *vfs) {
    if (!vfs) return VFS_ERR_INVALID_ARGUMENT;
    memset(vfs, 0, sizeof(*vfs));
    return VFS_OK;
}

void vfs_free(Vfs *vfs) {
    if (!vfs) return;
    for (uint32_t i = 0; i < vfs->count; i++) {
        vfs->mounts[i].ops->close(vfs->mounts[i].impl);
    }
    free(vfs->mounts);
    free(vfs->handles);
    memset(vfs, 0, sizeof(*vfs));
}

static VfsResult push_mount(Vfs *vfs, Mount m, MountHandle h) {
    if (vfs->count == vfs->capacity) {
        uint32_t new_cap = vfs->capacity ? vfs->capacity * 2 : 4;
        Mount *mp = realloc(vfs->mounts, new_cap * sizeof(Mount));
        if (!mp) return VFS_ERR_OUT_OF_MEMORY;
        vfs->mounts = mp;

        MountHandle *hp = realloc(vfs->handles, new_cap * sizeof(MountHandle));
        if (!hp) return VFS_ERR_OUT_OF_MEMORY;  // vfs->mounts is already resized+valid; capacity just won't be bumped below
        vfs->handles = hp;

        vfs->capacity = new_cap;
    }
    vfs->mounts[vfs->count] = m;
    vfs->handles[vfs->count] = h;
    vfs->count++;
    return VFS_OK;
}

VfsResult vfs_mount_dir(Vfs *vfs, const char *dir_path, MountHandle *out_handle) {
    if (!vfs || !dir_path) return VFS_ERR_INVALID_ARGUMENT;

    Mount m;
    if (!mount_dir_create(dir_path, &m)) return VFS_ERR_MOUNT_FAILED;

    MountHandle h = ++vfs->nextHandle;
    VfsResult result = push_mount(vfs, m, h);
    if (result != VFS_OK) { m.ops->close(m.impl); return result; }
    if (out_handle) *out_handle = h;
    return VFS_OK;
}

VfsResult vfs_mount_pak(Vfs *vfs, const char *pak_path, MountHandle *out_handle) {
    if (!vfs || !pak_path) return VFS_ERR_INVALID_ARGUMENT;

    Mount m;
    if (!mount_pak_create(pak_path, &m)) return VFS_ERR_MOUNT_FAILED;

    MountHandle h = ++vfs->nextHandle;
    VfsResult result = push_mount(vfs, m, h);
    if (result != VFS_OK) { m.ops->close(m.impl); return result; }
    if (out_handle) *out_handle = h;
    return VFS_OK;
}

VfsResult vfs_unmount(Vfs *vfs, MountHandle handle) {
    if (!vfs || handle == MOUNT_HANDLE_INVALID) return VFS_ERR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < vfs->count; i++) {
        if (vfs->handles[i] != handle) continue;

        vfs->mounts[i].ops->close(vfs->mounts[i].impl);

        memmove(&vfs->mounts[i], &vfs->mounts[i + 1], (vfs->count - i - 1) * sizeof(Mount));
        memmove(&vfs->handles[i], &vfs->handles[i + 1], (vfs->count - i - 1) * sizeof(MountHandle));
        vfs->count--;
        return VFS_OK;
    }
    return VFS_ERR_NOT_FOUND;
}

bool vfs_exists(Vfs *vfs, const char *vpath) {
    if (!vfs || !vpath) return false;
    for (uint32_t i = vfs->count; i-- > 0; ) {
        if (vfs->mounts[i].ops->exists(vfs->mounts[i].impl, vpath)) return true;
    }
    return false;
}

VfsResult vfs_read_file(Vfs *vfs, const char *vpath, void **out_data, size_t *out_size) {
    if (!vfs || !vpath || !out_data || !out_size) return VFS_ERR_INVALID_ARGUMENT;

    for (uint32_t i = vfs->count; i-- > 0; ) {
        if (vfs->mounts[i].ops->read(vfs->mounts[i].impl, vpath, out_data, out_size)) return VFS_OK;
    }
    return VFS_ERR_NOT_FOUND;
}

VfsResult vfs_write_file(Vfs *vfs, const char *vpath, const void *data, size_t size) {
    if (!vfs || !vpath) return VFS_ERR_INVALID_ARGUMENT;

    for (uint32_t i = vfs->count; i-- > 0; ) {
        Mount *m = &vfs->mounts[i];
        if (m->ops->write) {
            if (m->ops->write(m->impl, vpath, data, size)) {
                return VFS_OK;
            }
        }
    }
    return VFS_ERR_WRITE_UNSUPPORTED;
}