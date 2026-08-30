#ifndef ASSET_H
#define ASSET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct Ecs Ecs;
typedef struct Vfs Vfs;

typedef enum {
#define ASSET_TYPE(Name, InitFn) ASSET_TYPE_##Name,
#include "assets.def"
#undef ASSET_TYPE
    ASSET_TYPE_COUNT
} AssetType;

typedef uint64_t AssetHandle;
#define ASSET_INVALID_HANDLE ((AssetHandle)0)

typedef enum {
    ASSET_SCOPE_PERSISTENT = 0,
    ASSET_SCOPE_SCENE,
} AssetScope;

typedef uint32_t StringId; 

typedef struct {
    StringId    vpath_id;
    AssetHandle handle;
} AssetRef;

void asset_ref_set(struct Ecs *ecs, AssetRef *ref, const char *vpath);

typedef enum {
    ASSET_OK = 0,
    ASSET_ERR_OUT_OF_MEMORY,
    ASSET_ERR_NOT_FOUND,
    ASSET_ERR_LOAD_FAILED,
    ASSET_ERR_INVALID_ARGUMENT,
    ASSET_ERR_TYPE_MISMATCH,
    ASSET_ERR_HASH_COLLISION,
    ASSET_ERR_IO,
} AssetResult;

const char *asset_result_str(AssetResult result);

typedef void *(*AssetLoadFn)(const char *vpath, const void *data, size_t size);
typedef void (*AssetFreeFn)(void *asset_data);

typedef struct AssetTypeInfo {
    AssetLoadFn load_fn;
    AssetFreeFn free_fn;
    bool        registered;
} AssetTypeInfo;

typedef struct AssetSlot AssetSlot;

typedef struct AssetManager {
    Vfs          *vfs;
    AssetTypeInfo types[ASSET_TYPE_COUNT];
    AssetSlot    *slots;
    uint32_t      count;
    uint32_t      capacity;
    AssetScope    currentScope;
} AssetManager;

AssetResult asset_manager_init(AssetManager *mgr, Vfs *vfs);
void        asset_manager_free(AssetManager *mgr);

AssetResult asset_type_register(AssetManager *mgr, AssetType type, AssetLoadFn load_fn, AssetFreeFn free_fn);
AssetResult asset_load(AssetManager *mgr, AssetType type, const char *vpath, AssetHandle *out);
AssetResult asset_create(AssetManager *mgr, AssetType type, const char *name, void *asset_data, AssetHandle *out);

void       *asset_get(AssetManager *mgr, AssetHandle handle, AssetType expected_type);
bool        asset_handle_valid(AssetManager *mgr, AssetHandle handle);
AssetType   asset_get_type(AssetManager *mgr, AssetHandle handle);
const char *asset_get_key(AssetManager *mgr, AssetHandle handle);
void        asset_unload(AssetManager *mgr, AssetHandle handle);

AssetHandle asset_ref_resolve(AssetManager *mgr, Ecs* ecs, AssetType type, AssetRef *ref);

#define ASSET_GET(mgr, handle, type) asset_get((mgr), (handle), ASSET_TYPE_ ## type)\

void asset_manager_clear(AssetManager *mgr);
void asset_manager_begin_scope(AssetManager *mgr, AssetScope scope);
void asset_manager_end_scope(AssetManager *mgr);
void asset_manager_clear_scope(AssetManager *mgr, AssetScope scope);

uint32_t asset_manager_count_by_type(AssetManager *mgr, AssetType type);

typedef struct {
    const char  *vpath; 
    AssetHandle handle;
} AssetEntryInfo;

uint32_t asset_manager_list_by_type(AssetManager *mgr, AssetType type, AssetEntryInfo *out, uint32_t maxOut);

#endif