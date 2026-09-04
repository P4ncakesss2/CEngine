#include "asset.h"
#include "vfs/vfs.h"
#include "ecs/ecs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *asset_result_str(AssetResult result) {
    switch (result) {
        case ASSET_OK:                  return "ok";
        case ASSET_ERR_OUT_OF_MEMORY:   return "out of memory";
        case ASSET_ERR_NOT_FOUND:       return "not found";
        case ASSET_ERR_LOAD_FAILED:     return "load failed";
        case ASSET_ERR_INVALID_ARGUMENT:return "invalid argument";
        case ASSET_ERR_TYPE_MISMATCH:   return "type mismatch";
        case ASSET_ERR_HASH_COLLISION:  return "asset handle hash collision";
        case ASSET_ERR_IO:              return "vfs read/write failed";
    }
    return "unknown asset error";
}

struct AssetSlot {
    AssetHandle handle;
    AssetType   type;
    char       *key;
    void       *data;
    bool        occupied;
};

static uint64_t fnv1a64(const void *data, size_t len, uint64_t hash) {
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static AssetHandle compute_handle(AssetType type, const char *key) {
    uint64_t hash = 1469598103934665603ULL;
    uint32_t t = (uint32_t)type;
    hash = fnv1a64(&t, sizeof(t), hash);
    hash = fnv1a64(key, strlen(key), hash);
    if (hash == ASSET_INVALID_HANDLE) hash = 1;
    return (AssetHandle)hash;
}

static AssetSlot *table_find(AssetManager *mgr, AssetHandle handle) {
    if (mgr->capacity == 0) return NULL;
    uint32_t mask = mgr->capacity - 1;
    uint32_t idx = (uint32_t)(handle & mask);
    for (uint32_t probe = 0; probe < mgr->capacity; probe++) {
        AssetSlot *s = &mgr->slots[idx];
        if (!s->occupied) return NULL;
        if (s->handle == handle) return s;
        idx = (idx + 1) & mask;
    }
    return NULL;
}

static bool table_grow(AssetManager *mgr) {
    uint32_t new_cap = mgr->capacity ? mgr->capacity * 2 : 16;
    AssetSlot *new_slots = calloc(new_cap, sizeof(AssetSlot));
    if (!new_slots) return false;

    uint32_t mask = new_cap - 1;
    for (uint32_t i = 0; i < mgr->capacity; i++) {
        AssetSlot *s = &mgr->slots[i];
        if (!s->occupied) continue;
        uint32_t idx = (uint32_t)(s->handle & mask);
        while (new_slots[idx].occupied) idx = (idx + 1) & mask;
        new_slots[idx] = *s;
    }

    free(mgr->slots);
    mgr->slots = new_slots;
    mgr->capacity = new_cap;
    return true;
}

static AssetSlot *table_insert_new(AssetManager *mgr, AssetHandle handle, AssetType type, char *key) {
    if (mgr->capacity == 0 || (mgr->count + 1) * 10 >= mgr->capacity * 7) {
        if (!table_grow(mgr)) return NULL;
    }
    uint32_t mask = mgr->capacity - 1;
    uint32_t idx = (uint32_t)(handle & mask);
    while (mgr->slots[idx].occupied) idx = (idx + 1) & mask;

    AssetSlot *s = &mgr->slots[idx];
    memset(s, 0, sizeof(*s));
    s->handle = handle;
    s->type = type;
    s->key = key;
    s->occupied = true;
    mgr->count++;
    return s;
}

#define ASSET_TYPE(Name, InitFn) AssetResult InitFn(AssetManager *mgr);
#include "assets.def"
#undef ASSET_TYPE

static AssetResult asset_manager_init_all_types(AssetManager *mgr) {
    if (!mgr) return ASSET_ERR_INVALID_ARGUMENT;

#define ASSET_TYPE(Name, InitFn)                 \
    do {                                          \
        AssetResult r = InitFn(mgr);               \
        if (r != ASSET_OK) return r;               \
    } while (0);
#include "assets.def"
#undef ASSET_TYPE

    return ASSET_OK;
}

AssetResult asset_manager_init(AssetManager *mgr, Vfs *vfs) {
    if (!mgr || !vfs) return ASSET_ERR_INVALID_ARGUMENT;
    memset(mgr, 0, sizeof(*mgr));
    mgr->vfs = vfs;
    return asset_manager_init_all_types(mgr);
}

void asset_manager_free(AssetManager *mgr) {
    if (!mgr) return;
    for (uint32_t i = 0; i < mgr->capacity; i++) {
        AssetSlot *s = &mgr->slots[i];
        if (!s->occupied) continue;
        if (s->data && mgr->types[s->type].free_fn) {
            mgr->types[s->type].free_fn(s->data);
        }
        free(s->key);
    }
    free(mgr->slots);
    memset(mgr, 0, sizeof(*mgr));
}

AssetResult asset_type_register(AssetManager *mgr, AssetType type, AssetLoadFn load_fn, AssetFreeFn free_fn) {
    if (!mgr || type < 0 || type >= ASSET_TYPE_COUNT || !free_fn) return ASSET_ERR_INVALID_ARGUMENT;
    mgr->types[type].load_fn = load_fn;
    mgr->types[type].free_fn = free_fn;
    mgr->types[type].registered = true;
    return ASSET_OK;
}

static AssetResult build_from_vfs(AssetManager *mgr, AssetSlot *slot) {
    if (!mgr->types[slot->type].load_fn) return ASSET_ERR_INVALID_ARGUMENT;

    void *file_data = NULL;
    size_t file_size = 0;
    VfsResult vr = vfs_read_file(mgr->vfs, slot->key, &file_data, &file_size);

    void *asset_data = mgr->types[slot->type].load_fn(
        slot->key,
        vr == VFS_OK ? file_data : NULL,
        vr == VFS_OK ? file_size : 0);

    free(file_data);
    if (!asset_data) return ASSET_ERR_LOAD_FAILED;

    slot->data = asset_data;
    return ASSET_OK;
}

AssetResult asset_load(AssetManager *mgr, AssetType type, const char *vpath, AssetHandle *out) {
    if (!mgr || !vpath || !out || type < 0 || type >= ASSET_TYPE_COUNT) return ASSET_ERR_INVALID_ARGUMENT;
    if (!mgr->types[type].registered) return ASSET_ERR_INVALID_ARGUMENT;

    AssetHandle handle = compute_handle(type, vpath);
    AssetSlot *slot = table_find(mgr, handle);

    if (slot) {
        if (slot->type != type || strcmp(slot->key, vpath) != 0) return ASSET_ERR_HASH_COLLISION;
        if (!slot->data) {
            AssetResult r = build_from_vfs(mgr, slot);
            if (r != ASSET_OK) return r;
        }
        *out = handle;
        return ASSET_OK;
    }

    char *key_copy = strdup(vpath);
    if (!key_copy) return ASSET_ERR_OUT_OF_MEMORY;

    slot = table_insert_new(mgr, handle, type, key_copy);
    if (!slot) { free(key_copy); return ASSET_ERR_OUT_OF_MEMORY; }

    AssetResult r = build_from_vfs(mgr, slot);
    if (r != ASSET_OK) return r;

    *out = handle;
    return ASSET_OK;
}

AssetResult asset_create(AssetManager *mgr, AssetType type, const char *name, void *asset_data, AssetHandle *out) {
    if (!mgr || !name || !asset_data || !out || type < 0 || type >= ASSET_TYPE_COUNT) return ASSET_ERR_INVALID_ARGUMENT;
    if (!mgr->types[type].registered) return ASSET_ERR_INVALID_ARGUMENT;

    AssetHandle handle = compute_handle(type, name);
    AssetSlot *slot = table_find(mgr, handle);

    if (slot) {
        if (slot->type != type || strcmp(slot->key, name) != 0) return ASSET_ERR_HASH_COLLISION;
        if (slot->data) {
            if (mgr->types[type].free_fn) mgr->types[type].free_fn(asset_data);
        } else {
            slot->data = asset_data;
        }
        *out = handle;
        return ASSET_OK;
    }

    char *key_copy = strdup(name);
    if (!key_copy) return ASSET_ERR_OUT_OF_MEMORY;

    slot = table_insert_new(mgr, handle, type, key_copy);
    if (!slot) { free(key_copy); return ASSET_ERR_OUT_OF_MEMORY; }

    slot->data = asset_data;
    *out = handle;
    return ASSET_OK;
}

void *asset_get(AssetManager *mgr, AssetHandle handle, AssetType expected_type) {
    if (!mgr || handle == ASSET_INVALID_HANDLE) return NULL;
    AssetSlot *s = table_find(mgr, handle);
    if (!s || !s->data) return NULL;
    if (s->type != expected_type) return NULL;
    return s->data;
}

bool asset_handle_valid(AssetManager *mgr, AssetHandle handle) {
    if (!mgr || handle == ASSET_INVALID_HANDLE) return false;
    return table_find(mgr, handle) != NULL;
}

AssetType asset_get_type(AssetManager *mgr, AssetHandle handle) {
    AssetSlot *s = mgr ? table_find(mgr, handle) : NULL;
    return s ? s->type : ASSET_TYPE_COUNT;
}

const char *asset_get_key(AssetManager *mgr, AssetHandle handle) {
    AssetSlot *s = mgr ? table_find(mgr, handle) : NULL;
    return s ? s->key : NULL;
}

void asset_unload(AssetManager *mgr, AssetHandle handle) {
    if (!mgr) return;
    AssetSlot *s = table_find(mgr, handle);
    if (!s || !s->data) return;
    if (mgr->types[s->type].free_fn) mgr->types[s->type].free_fn(s->data);
    s->data = NULL;
}

AssetHandle asset_ref_resolve(AssetManager *mgr, Ecs* ecs, AssetType type, AssetRef *ref) {
    if (!ref) return ASSET_INVALID_HANDLE;
    if (ref->handle != ASSET_INVALID_HANDLE) return ref->handle;
    AssetHandle h;
    if (asset_load(mgr, type, ecs_string_get(ecs, ref->vpath_id), &h) != ASSET_OK) return ASSET_INVALID_HANDLE;
    ref->handle = h;
    return h;
}

void asset_manager_clear(AssetManager *mgr) {
    if (!mgr) return;
    for (uint32_t i = 0; i < mgr->capacity; i++) {
        AssetSlot *s = &mgr->slots[i];
        if (!s->occupied) continue;
        if (s->data && mgr->types[s->type].free_fn) {
            mgr->types[s->type].free_fn(s->data);
        }
        free(s->key);
    }
    free(mgr->slots);
    mgr->slots = NULL;
    mgr->capacity = 0;
    mgr->count = 0;
}

uint32_t asset_manager_count_by_type(AssetManager *mgr, AssetType type) {
    if (!mgr || type < 0 || type >= ASSET_TYPE_COUNT) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < mgr->capacity; i++) {
        AssetSlot *s = &mgr->slots[i];
        if (s->occupied && s->data && s->type == type) n++;
    }
    return n;
}

uint32_t asset_manager_list_by_type(AssetManager *mgr, AssetType type, AssetEntryInfo *out, uint32_t maxOut) {
    if (!mgr || type < 0 || type >= ASSET_TYPE_COUNT) return 0;

    uint32_t total = 0;
    for (uint32_t i = 0; i < mgr->capacity; i++) {
        AssetSlot *s = &mgr->slots[i];
        if (!s->occupied || !s->data || s->type != type) continue;

        if (out && total < maxOut) {
            out[total].vpath = s->key; // Point directly to the internal copy
            out[total].handle = s->handle;
        }
        total++;
    }
    return total;
}

void asset_ref_set(Ecs *ecs, AssetRef *ref, const char *vpath) {
    if (!ref) return;
    ref->vpath_id = ecs_string_intern(ecs, vpath);
    ref->handle   = ASSET_INVALID_HANDLE;
}