#include "ecs.h"
#include "vfs/vfs.h"
#include "asset/asset.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define ECS_INITIAL_CAPACITY 64

const char *ecs_result_str(EcsResult result) {
    switch (result) {
        case ECS_OK:                    return "ok";
        case ECS_ERR_OUT_OF_MEMORY:     return "out of memory";
        case ECS_ERR_INVALID_ENTITY:    return "invalid entity";
        case ECS_ERR_INVALID_ARGUMENT:  return "invalid argument";
        case ECS_ERR_IO:                return "vfs read/write failed";
        case ECS_ERR_BAD_MAGIC:         return "bad file magic";
        case ECS_ERR_BAD_VERSION:       return "unsupported file version";
        case ECS_ERR_CORRUPT:           return "corrupt or truncated file";
    }
    return "unknown ecs error";
}

static void ecs_invalidate_asset_refs(Ecs *w, uint32_t idx) {
#define ASSET_REF_FIELD(Component, field)                                        \
    if (w->mask[idx].bits[ECS_WORD_OF(Component)] &                             \
        ((uint64_t)1u << ECS_BIT_OF(Component))) {                              \
        w->Component##_data[idx].field.handle = ASSET_INVALID_HANDLE;           \
    }
#define ASSET_REF_ARRAY(Component, field, count)                                 \
    if (w->mask[idx].bits[ECS_WORD_OF(Component)] &                             \
        ((uint64_t)1u << ECS_BIT_OF(Component))) {                              \
        for (int _i = 0; _i < (count); _i++)                                    \
            w->Component##_data[idx].field[_i].handle = ASSET_INVALID_HANDLE;   \
    }
#include "asset_refs.def"
#undef ASSET_REF_FIELD
#undef ASSET_REF_ARRAY
}

static EcsResult ecs_grow(Ecs *w, uint32_t min_capacity) {
    uint32_t old_cap = w->capacity;
    uint32_t new_cap = old_cap ? old_cap : ECS_INITIAL_CAPACITY;
    while (new_cap < min_capacity) new_cap *= 2;
    if (new_cap == old_cap) return ECS_OK;

    bool          *alive             = malloc(new_cap * sizeof(bool));
    uint32_t      *generation        = malloc(new_cap * sizeof(uint32_t));
    ComponentMask *mask              = malloc(new_cap * sizeof(ComponentMask));
    Entity        *dense             = malloc(new_cap * sizeof(Entity));
    uint32_t      *dense_index       = malloc(new_cap * sizeof(uint32_t));
    uint32_t      *instanceId        = malloc(new_cap * sizeof(uint32_t));
    bool          *instanceIsNested  = malloc(new_cap * sizeof(bool));

    void *components[COMPONENT_COUNT] = {0};
    bool components_ok = true;
    int i = 0;
#define COMPONENT(Name)                                   \
    components[i] = malloc(new_cap * sizeof(Name));       \
    if (!components[i]) components_ok = false;            \
    i++;
#include "components.def"
#undef COMPONENT

    if (!alive || !generation || !mask || !dense || !dense_index ||
        !instanceId || !instanceIsNested || !components_ok) {
        free(alive); free(generation); free(mask); free(dense); free(dense_index);
        free(instanceId); free(instanceIsNested);
        for (int j = 0; j < COMPONENT_COUNT; j++) free(components[j]);
        return ECS_ERR_OUT_OF_MEMORY;
    }

    if (old_cap) {
        memcpy(alive,            w->alive,            old_cap * sizeof(bool));
        memcpy(generation,       w->generation,        old_cap * sizeof(uint32_t));
        memcpy(mask,             w->mask,              old_cap * sizeof(ComponentMask));
        memcpy(dense,            w->dense,             old_cap * sizeof(Entity));
        memcpy(dense_index,      w->dense_index,       old_cap * sizeof(uint32_t));
        memcpy(instanceId,       w->instanceId,        old_cap * sizeof(uint32_t));
        memcpy(instanceIsNested, w->instanceIsNested,  old_cap * sizeof(bool));
    }
    memset(alive            + old_cap, 0, (new_cap - old_cap) * sizeof(bool));
    memset(generation       + old_cap, 0, (new_cap - old_cap) * sizeof(uint32_t));
    memset(mask              + old_cap, 0, (new_cap - old_cap) * sizeof(ComponentMask));
    memset(instanceId       + old_cap, 0, (new_cap - old_cap) * sizeof(uint32_t));
    memset(instanceIsNested + old_cap, 0, (new_cap - old_cap) * sizeof(bool));

    free(w->alive); free(w->generation); free(w->mask);
    free(w->dense); free(w->dense_index);
    free(w->instanceId); free(w->instanceIsNested);
    w->alive = alive; w->generation = generation; w->mask = mask;
    w->dense = dense; w->dense_index = dense_index;
    w->instanceId = instanceId; w->instanceIsNested = instanceIsNested;

    i = 0;
#define COMPONENT(Name)                                                     \
    do {                                                                    \
        if (old_cap) memcpy(components[i], w->Name##_data, old_cap * sizeof(Name)); \
        free(w->Name##_data);                                               \
        w->Name##_data = components[i];                                     \
        i++;                                                                \
    } while (0);
#include "components.def"
#undef COMPONENT

    w->capacity = new_cap;
    return ECS_OK;
}

EcsResult ecs_init(Ecs *w) {
    if (!w) return ECS_ERR_INVALID_ARGUMENT;
    memset(w, 0, sizeof(*w));
    
    w->string_pool.capacity = 1024;
    w->string_pool.data = malloc(w->string_pool.capacity);
    if (!w->string_pool.data) return ECS_ERR_OUT_OF_MEMORY;
    w->string_pool.data[0] = '\0';
    w->string_pool.size = 1;
    
    return ecs_grow(w, ECS_INITIAL_CAPACITY);
}

void ecs_free(Ecs *w) {
    if (!w) return;
    free(w->string_pool.data);
    free(w->free_list);
    free(w->alive);
    free(w->generation);
    free(w->mask);
    free(w->dense);
    free(w->dense_index);
    free(w->instanceId);
    free(w->instanceIsNested);
#define COMPONENT(Name) free(w->Name##_data);
#include "components.def"
#undef COMPONENT
    memset(w, 0, sizeof(*w));
}

static bool free_list_push(Ecs *w, uint32_t idx) {
    if (w->free_count == w->free_capacity) {
        uint32_t new_cap = w->free_capacity ? w->free_capacity * 2 : 64;
        uint32_t *p = realloc(w->free_list, new_cap * sizeof(uint32_t));
        if (!p) return false;
        w->free_list = p;
        w->free_capacity = new_cap;
    }
    w->free_list[w->free_count++] = idx;
    return true;
}

StringId ecs_string_intern(Ecs *w, const char *str) {
    if (!w || !str || !*str) return ECS_INVALID_STRING_ID;
    
    uint32_t pos = 1; 
    while (pos < w->string_pool.size) {
        if (strcmp(w->string_pool.data + pos, str) == 0) return (StringId)pos;
        pos += (uint32_t)strlen(w->string_pool.data + pos) + 1;
    }
    
    uint32_t len = (uint32_t)strlen(str) + 1;
    if (w->string_pool.size + len > w->string_pool.capacity) {
        uint32_t new_cap = w->string_pool.capacity ? w->string_pool.capacity * 2 : 1024;
        while (new_cap < w->string_pool.size + len) new_cap *= 2;
        char *new_data = realloc(w->string_pool.data, new_cap);
        if (!new_data) return ECS_INVALID_STRING_ID;
        w->string_pool.data = new_data;
        w->string_pool.capacity = new_cap;
    }
    
    StringId id = (StringId)w->string_pool.size;
    memcpy(w->string_pool.data + w->string_pool.size, str, len);
    w->string_pool.size += len;
    return id;
}

const char *ecs_string_get(const Ecs *w, StringId id) {
    if (!w || id == ECS_INVALID_STRING_ID || id >= w->string_pool.size) return "";
    return w->string_pool.data + id;
}

EcsResult ecs_entity_create(Ecs *w, Entity *out) {
    if (!w || !out) return ECS_ERR_INVALID_ARGUMENT;
    *out = ECS_INVALID_ENTITY;

    uint32_t idx;
    bool from_free_list = w->free_count > 0;

    if (from_free_list) {
        idx = w->free_list[w->free_count - 1];
    } else {
        if (w->high_water == w->capacity) {
            EcsResult grow_result = ecs_grow(w, w->capacity + 1);
            if (grow_result != ECS_OK) return grow_result;
        }
        idx = w->high_water;
    }

    if (from_free_list) w->free_count--;
    else w->high_water++;

    w->alive[idx] = true;
    memset(&w->mask[idx], 0, sizeof(ComponentMask));
    w->instanceId[idx] = ECS_INVALID_SCENE_INSTANCE;
    w->instanceIsNested[idx] = false;

    w->dense_index[idx] = w->alive_count;
    Entity e = ECS_MAKE_ENTITY(idx, w->generation[idx]);
    w->dense[w->alive_count] = e;
    w->alive_count++;

    *out = e;
    return ECS_OK;
}

EcsResult ecs_entity_destroy(Ecs *w, Entity e) {
    if (!w) return ECS_ERR_INVALID_ARGUMENT;

    uint32_t idx = ECS_ENTITY_INDEX(e);
    if (idx >= w->high_water || !w->alive[idx]) return ECS_ERR_INVALID_ENTITY;
    if (w->generation[idx] != ECS_ENTITY_GEN(e)) return ECS_ERR_INVALID_ENTITY;

    w->alive[idx] = false;
    memset(&w->mask[idx], 0, sizeof(ComponentMask));
    w->instanceId[idx] = ECS_INVALID_SCENE_INSTANCE;
    w->instanceIsNested[idx] = false;

    uint32_t pos = w->dense_index[idx];
    uint32_t last_pos = w->alive_count - 1;
    Entity last_entity = w->dense[last_pos];
    uint32_t last_idx = ECS_ENTITY_INDEX(last_entity);

    w->dense[pos] = last_entity;
    w->dense_index[last_idx] = pos;
    w->alive_count--;
    w->generation[idx] = (w->generation[idx] + 1) & ECS_GEN_MASK;

    for (uint32_t i = 0; i < w->alive_count; i++) {
        Entity ce = w->dense[i];
        if (ECS_HAS(w, ce, Parent)) {
            Parent *p = ECS_GET(w, ce, Parent);
            if (p->entity == e) {
                ECS_REMOVE(w, ce, Parent);
            }
        }
    }

    free_list_push(w, idx);

    return ECS_OK;
}

bool ecs_entity_alive(const Ecs *w, Entity e) {
    uint32_t idx = ECS_ENTITY_INDEX(e);
    return idx < w->high_water && w->alive[idx] &&
           w->generation[idx] == ECS_ENTITY_GEN(e);
}

EcsResult ecs_entity_clone(Ecs *w, Entity src, Entity *out) {
    if (!w || !out) return ECS_ERR_INVALID_ARGUMENT;
    if (!ecs_entity_alive(w, src)) return ECS_ERR_INVALID_ENTITY;

    Entity dst;
    EcsResult result = ecs_entity_create(w, &dst);
    if (result != ECS_OK) return result;

    uint32_t src_idx = ECS_ENTITY_INDEX(src);
    uint32_t dst_idx = ECS_ENTITY_INDEX(dst);

#define COMPONENT(Name)                                                          \
    if ((w->mask[src_idx].bits[ECS_WORD_OF(Name)] >> ECS_BIT_OF(Name)) & 1u) {   \
        w->Name##_data[dst_idx] = w->Name##_data[src_idx];                       \
        w->mask[dst_idx].bits[ECS_WORD_OF(Name)] |= ((uint64_t)1u << ECS_BIT_OF(Name)); \
    }
#include "components.def"
#undef COMPONENT

    *out = dst;
    return ECS_OK;
}

#define ECS_MAGIC   0x53434533u
#define ECS_VERSION 10u

typedef struct {
    uint8_t *data;
    size_t   size;
    size_t   capacity;
} ByteBuf;

static bool bytebuf_write(ByteBuf *b, const void *src, size_t n) {
    if (b->size + n > b->capacity) {
        size_t new_cap = b->capacity ? b->capacity * 2 : 1024;
        while (new_cap < b->size + n) new_cap *= 2;
        uint8_t *p = realloc(b->data, new_cap);
        if (!p) return false;
        b->data = p;
        b->capacity = new_cap;
    }
    memcpy(b->data + b->size, src, n);
    b->size += n;
    return true;
}

typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         pos;
} ByteReader;

static bool byteread(ByteReader *r, void *dst, size_t n) {
    if (r->pos + n > r->size) return false;
    memcpy(dst, r->data + r->pos, n);
    r->pos += n;
    return true;
}

EcsResult ecs_serialize(const Ecs *w, uint8_t **out_data, size_t *out_size) {
    if (!w || !out_data || !out_size)
        return ECS_ERR_INVALID_ARGUMENT;

    ByteBuf b = {0};

    uint32_t magic = ECS_MAGIC;
    uint32_t version = ECS_VERSION;

    EcsResult result = ECS_OK;

    #define WB(ptr, size, count)                                              \
        do {                                                                  \
            if (!bytebuf_write(&b, (ptr), (size) * (count))) {                \
                result = ECS_ERR_OUT_OF_MEMORY;                               \
                goto error;                                                   \
            }                                                                 \
        } while (0)

    WB(&magic, sizeof(magic), 1);
    WB(&version, sizeof(version), 1);

    WB(&w->capacity, sizeof(w->capacity), 1);
    WB(&w->high_water, sizeof(w->high_water), 1);

    uint32_t saved_count = 0;
    for (uint32_t i = 0; i < w->alive_count; i++) {
        uint32_t idx = ECS_ENTITY_INDEX(w->dense[i]);
        if (!w->instanceIsNested[idx]) saved_count++;
    }
    WB(&saved_count, sizeof(saved_count), 1);
    WB(&w->free_count, sizeof(w->free_count), 1);

    WB(&w->string_pool.size, sizeof(w->string_pool.size), 1);

    if (w->string_pool.size > 0) {
        WB(w->string_pool.data, sizeof(char), w->string_pool.size);
    }

    if (w->free_count > 0) {
        WB(w->free_list, sizeof(uint32_t), w->free_count);
    }

    if (w->high_water > 0) {
        WB(w->generation, sizeof(uint32_t), w->high_water);
        WB(w->instanceId, sizeof(uint32_t), w->high_water);
        WB(w->instanceIsNested, sizeof(bool), w->high_water);
    }

    for (uint32_t i = 0; i < w->alive_count; i++) {
        Entity e = w->dense[i];
        uint32_t idx = ECS_ENTITY_INDEX(e);

        if (w->instanceIsNested[idx]) continue;

        WB(&idx, sizeof(idx), 1);
        WB(&w->mask[idx], sizeof(ComponentMask), 1);

        #define COMPONENT(Name)                                               \
            if (ECS_HAS(w, e, Name)) {                                        \
                WB(&w->Name##_data[idx], sizeof(Name), 1);                    \
            }

        #include "components.def"

        #undef COMPONENT
    }

    #undef WB

    *out_data = b.data;
    *out_size = b.size;

    return ECS_OK;

error:
    #undef WB

    free(b.data);
    return result;
}

EcsResult ecs_deserialize(Ecs *w, const uint8_t *data, size_t size) {
    if (!w || !data)
        return ECS_ERR_INVALID_ARGUMENT;

    memset(w, 0, sizeof(*w));

    ByteReader r = {
        .data = data,
        .size = size,
        .pos = 0
    };

    EcsResult result = ECS_OK;

    #define RB(ptr, size, count)                                              \
        do {                                                                  \
            if (!byteread(&r, (ptr), (size) * (count))) {                     \
                result = ECS_ERR_CORRUPT;                                     \
                goto error;                                                   \
            }                                                                 \
        } while (0)

    uint32_t magic = 0;
    uint32_t version = 0;

    RB(&magic, sizeof(magic), 1);
    RB(&version, sizeof(version), 1);

    if (magic != ECS_MAGIC) {
        result = ECS_ERR_BAD_MAGIC;
        goto error;
    }

    if (version != ECS_VERSION) {
        result = ECS_ERR_BAD_VERSION;
        goto error;
    }

    uint32_t capacity = 0;

    RB(&capacity, sizeof(capacity), 1);

    if (capacity == 0) {
        result = ECS_ERR_CORRUPT;
        goto error;
    }

    {
        EcsResult grow_result = ecs_grow(w, capacity);

        if (grow_result != ECS_OK) {
            result = grow_result;
            goto error;
        }
    }

    RB(&w->high_water, sizeof(w->high_water), 1);
    RB(&w->alive_count, sizeof(w->alive_count), 1);
    RB(&w->free_count, sizeof(w->free_count), 1);

    if (w->high_water > capacity ||
        w->alive_count > w->high_water) {
        result = ECS_ERR_CORRUPT;
        goto error;
    }
    uint32_t string_pool_size = 0;

    RB(&string_pool_size, sizeof(string_pool_size), 1);

    if (string_pool_size == 0) {
        result = ECS_ERR_CORRUPT;
        goto error;
    }

    w->string_pool.data = malloc(string_pool_size);

    if (!w->string_pool.data) {
        result = ECS_ERR_OUT_OF_MEMORY;
        goto error;
    }

    w->string_pool.size = string_pool_size;
    w->string_pool.capacity = string_pool_size;

    RB(
        w->string_pool.data,
        sizeof(char),
        w->string_pool.size
    );

    if (w->free_count > 0) {
        w->free_capacity = w->free_count;

        w->free_list = malloc(
            w->free_capacity * sizeof(uint32_t)
        );

        if (!w->free_list) {
            result = ECS_ERR_OUT_OF_MEMORY;
            goto error;
        }

        RB(
            w->free_list,
            sizeof(uint32_t),
            w->free_count
        );
    }

    if (w->high_water > 0) {
        RB(
            w->generation,
            sizeof(uint32_t),
            w->high_water
        );
        RB(
            w->instanceId,
            sizeof(uint32_t),
            w->high_water
        );
        RB(
            w->instanceIsNested,
            sizeof(bool),
            w->high_water
        );
    }

    for (uint32_t i = 0; i < w->alive_count; i++) {
        uint32_t idx = 0;

        RB(&idx, sizeof(idx), 1);

        if (idx >= w->high_water) {
            result = ECS_ERR_CORRUPT;
            goto error;
        }

        ComponentMask mask;

        RB(&mask, sizeof(mask), 1);

        w->mask[idx] = mask;
        w->alive[idx] = true;

        w->dense[i] =
            ECS_MAKE_ENTITY(idx, w->generation[idx]);

        w->dense_index[idx] = i;

        #define COMPONENT(Name)                                               \
            if ((mask.bits[ECS_WORD_OF(Name)] >>                             \
                 ECS_BIT_OF(Name)) & 1u) {                                   \
                RB(                                                          \
                    &w->Name##_data[idx],                                    \
                    sizeof(Name),                                             \
                    1                                                         \
                );                                                           \
            }

        #include "components.def"

        #undef COMPONENT
        ecs_invalidate_asset_refs(w, idx);
    }

    #undef RB

    return ECS_OK;

error:
    #undef RB

    ecs_free(w);

    return result;
}

EcsResult ecs_save(const Ecs *w, Vfs *vfs, const char *vpath) {
    if (!w || !vfs || !vpath) return ECS_ERR_INVALID_ARGUMENT;

    uint8_t *data = NULL;
    size_t size = 0;
    EcsResult result = ecs_serialize(w, &data, &size);
    if (result != ECS_OK) return result;

    VfsResult write_result = vfs_write_file(vfs, vpath, data, size);
    free(data);
    return (write_result == VFS_OK) ? ECS_OK : ECS_ERR_IO;
}

EcsResult ecs_load(Ecs *w, Vfs *vfs, const char *vpath) {
    if (!w || !vfs || !vpath) return ECS_ERR_INVALID_ARGUMENT;

    void *file_data = NULL;
    size_t file_size = 0;
    if (vfs_read_file(vfs, vpath, &file_data, &file_size) != VFS_OK) return ECS_ERR_IO;

    EcsResult result = ecs_deserialize(w, file_data, file_size);
    free(file_data);
    return result;
}

bool ecs_equal(const Ecs *a, const Ecs *b) {
    if (a->alive_count != b->alive_count) return false;

    for (uint32_t i = 0; i < a->alive_count; i++) {
        Entity e = a->dense[i];
        if (!ecs_entity_alive(b, e)) return false;

        uint32_t idx = ECS_ENTITY_INDEX(e);
        if (memcmp(&a->mask[idx], &b->mask[idx], sizeof(ComponentMask)) != 0)
            return false;
#define COMPONENT(Name)                                                     \
        if (ECS_HAS(a, e, Name) && strcmp(#Name, "Transform") != 0) {  \
            if (memcmp(&a->Name##_data[idx], &b->Name##_data[idx], sizeof(Name)) != 0) \
                return false;                                               \
        }
#include "components.def"
#undef COMPONENT
    }

    return true;
}

EcsResult ecs_clone(Ecs *dst, const Ecs *src) {
    if (!dst || !src) return ECS_ERR_INVALID_ARGUMENT;

    memset(dst, 0, sizeof(*dst));

    EcsResult grow_result = ecs_grow(dst, src->capacity ? src->capacity : ECS_INITIAL_CAPACITY);
    if (grow_result != ECS_OK) return grow_result;

    if (src->free_count) {
        dst->free_capacity = src->free_count;
        dst->free_list = malloc(dst->free_capacity * sizeof(uint32_t));
        if (!dst->free_list) { ecs_free(dst); return ECS_ERR_OUT_OF_MEMORY; }
        memcpy(dst->free_list, src->free_list, src->free_count * sizeof(uint32_t));
    }
    dst->free_count = src->free_count;

    dst->high_water = src->high_water;
    dst->alive_count = src->alive_count;
    dst->nextInstanceId = src->nextInstanceId;

    memcpy(dst->alive,            src->alive,            src->high_water * sizeof(bool));
    memcpy(dst->generation,       src->generation,        src->high_water * sizeof(uint32_t));
    memcpy(dst->mask,             src->mask,              src->high_water * sizeof(ComponentMask));
    memcpy(dst->dense,            src->dense,             src->alive_count * sizeof(Entity));
    memcpy(dst->dense_index,      src->dense_index,       src->high_water * sizeof(uint32_t));
    memcpy(dst->instanceId,       src->instanceId,        src->high_water * sizeof(uint32_t));
    memcpy(dst->instanceIsNested, src->instanceIsNested,  src->high_water * sizeof(bool));

#define COMPONENT(Name) memcpy(dst->Name##_data, src->Name##_data, src->high_water * sizeof(Name));
#include "components.def"
#undef COMPONENT

    return ECS_OK;
}

static EcsResult ecs_instantiate_recursive(Ecs *dst, AssetManager *assets, const Ecs *scene, Entity anchor, uint32_t instance_id, bool is_nested) {
    if (scene->high_water == 0) return ECS_OK;

    Entity *remap = calloc(scene->high_water, sizeof(Entity));
    bool   *has   = calloc(scene->high_water, sizeof(bool));
    if (!remap || !has) { free(remap); free(has); return ECS_ERR_OUT_OF_MEMORY; }

    for (uint32_t i = 0; i < scene->alive_count; i++) {
        Entity se = scene->dense[i];
        uint32_t sidx = ECS_ENTITY_INDEX(se);
        Entity de;
        EcsResult r = ecs_entity_create(dst, &de);
        if (r != ECS_OK) { free(remap); free(has); return r; }
        remap[sidx] = de;
        has[sidx] = true;
    }

    for (uint32_t i = 0; i < scene->alive_count; i++) {
        Entity se = scene->dense[i];
        uint32_t sidx = ECS_ENTITY_INDEX(se);
        Entity de = remap[sidx];
        uint32_t didx = ECS_ENTITY_INDEX(de);

#define COMPONENT(Name)                                                            \
        if ((scene->mask[sidx].bits[ECS_WORD_OF(Name)] >> ECS_BIT_OF(Name)) & 1u) { \
            dst->Name##_data[didx] = scene->Name##_data[sidx];                     \
            dst->mask[didx].bits[ECS_WORD_OF(Name)] |= ((uint64_t)1u << ECS_BIT_OF(Name)); \
        }
#include "components.def"

#define ASSET_REF_FIELD(Component, field) \
        if (dst->mask[didx].bits[ECS_WORD_OF(Component)] & ((uint64_t)1u << ECS_BIT_OF(Component))) { \
            const char *str = ecs_string_get(scene, scene->Component##_data[sidx].field.vpath_id); \
            dst->Component##_data[didx].field.vpath_id = ecs_string_intern(dst, str); \
            dst->Component##_data[didx].field.handle = ASSET_INVALID_HANDLE; \
        }
#define ASSET_REF_ARRAY(Component, field, count) \
        if (dst->mask[didx].bits[ECS_WORD_OF(Component)] & ((uint64_t)1u << ECS_BIT_OF(Component))) { \
            for (int _i = 0; _i < (count); _i++) { \
                const char *str = ecs_string_get(scene, scene->Component##_data[sidx].field[_i].vpath_id); \
                dst->Component##_data[didx].field[_i].vpath_id = ecs_string_intern(dst, str); \
                dst->Component##_data[didx].field[_i].handle = ASSET_INVALID_HANDLE; \
            } \
        }
#include "asset_refs.def"
#undef ASSET_REF_FIELD
#undef ASSET_REF_ARRAY

        /* Re-intern Name string from source scene string pool into target world string pool */
        if (dst->mask[didx].bits[ECS_WORD_OF(Name)] & ((uint64_t)1u << ECS_BIT_OF(Name))) {
            const char *str = ecs_string_get(scene, scene->Name_data[sidx].text);
            dst->Name_data[didx].text = ecs_string_intern(dst, str);
        }

        dst->instanceId[didx] = instance_id;
        dst->instanceIsNested[didx] = is_nested;

        if (ECS_HAS(scene, se, Parent)) {
            Parent *p = ECS_GET(scene, se, Parent);
            uint32_t pidx = ECS_ENTITY_INDEX(p->entity);
            if (pidx < scene->high_water && has[pidx]) {
                ECS_ADD(dst, de, Parent, ((Parent){ .entity = remap[pidx] }));
            } else {
                ECS_REMOVE(dst, de, Parent);
            }
        } else if (ecs_entity_alive(dst, anchor)) {
            ECS_ADD(dst, de, Parent, ((Parent){ .entity = anchor }));
        }
    }

    EcsResult result = ECS_OK;
    for (uint32_t i = 0; i < scene->alive_count; i++) {
        Entity se = scene->dense[i];
        if (!ECS_HAS(scene, se, Scene)) continue;
        if (!assets) continue;

        Scene *node = ECS_GET(scene, se, Scene);
        const char *vpath = ecs_string_get(scene, node->sceneRef.vpath_id);
        if (!vpath || !vpath[0]) continue;

        AssetHandle sceneHandle;
        if (asset_load(assets, ASSET_TYPE_Scene, vpath, &sceneHandle) != ASSET_OK) continue;
        Ecs *nested = asset_get(assets, sceneHandle, ASSET_TYPE_Scene);
        if (!nested) continue;

        uint32_t sidx = ECS_ENTITY_INDEX(se);
        Entity de = remap[sidx];

        result = ecs_instantiate_recursive(dst, assets, nested, de, instance_id, true);
        if (result != ECS_OK) break;
    }

    free(remap);
    free(has);
    return result;
}

EcsResult ecs_instantiate(Ecs *dst, AssetManager *assets, const Ecs *scene, Entity anchor, bool is_nested, uint32_t *out_instance_id) {
    if (!dst || !scene) return ECS_ERR_INVALID_ARGUMENT;

    uint32_t instance_id = ++dst->nextInstanceId;
    EcsResult result = ecs_instantiate_recursive(dst, assets, scene, anchor, instance_id, is_nested);
    if (result != ECS_OK) return result;

    if (out_instance_id) *out_instance_id = instance_id;
    return ECS_OK;
}

bool ecs_find_scene_instance_by_anchor(const Ecs *w, Entity anchor, uint32_t *out_instance_id) {
    if (!w || anchor == ECS_INVALID_ENTITY) return false;

    ECS_EACH(w, ECS_MASK(COMPONENT_Parent), e) {
        if (ECS_GET(w, e, Parent)->entity != anchor) continue;

        uint32_t idx = ECS_ENTITY_INDEX(e);
        if (w->instanceId[idx] == ECS_INVALID_SCENE_INSTANCE) continue;

        if (out_instance_id) *out_instance_id = w->instanceId[idx];
        return true;
    }
    return false;
}

EcsResult ecs_destroy_scene_instance(Ecs *w, uint32_t instance_id) {
    if (!w) return ECS_ERR_INVALID_ARGUMENT;
    if (w->alive_count == 0 || instance_id == ECS_INVALID_SCENE_INSTANCE) return ECS_OK;

    Entity *victims = malloc(w->alive_count * sizeof(Entity));
    if (!victims) return ECS_ERR_OUT_OF_MEMORY;
    uint32_t n = 0;

    for (uint32_t i = 0; i < w->alive_count; i++) {
        Entity e = w->dense[i];
        uint32_t idx = ECS_ENTITY_INDEX(e);
        if (w->instanceId[idx] == instance_id) victims[n++] = e;
    }

    for (uint32_t i = 0; i < n; i++) ecs_entity_destroy(w, victims[i]);

    free(victims);
    return ECS_OK;
}

EcsResult ecs_destroy_all_scene_instances(Ecs *w) {
    if (!w) return ECS_ERR_INVALID_ARGUMENT;
    if (w->alive_count == 0) return ECS_OK;

    Entity *victims = malloc(w->alive_count * sizeof(Entity));
    if (!victims) return ECS_ERR_OUT_OF_MEMORY;
    uint32_t n = 0;

    for (uint32_t i = 0; i < w->alive_count; i++) {
        Entity e = w->dense[i];
        uint32_t idx = ECS_ENTITY_INDEX(e);
        if (w->instanceId[idx] != ECS_INVALID_SCENE_INSTANCE) victims[n++] = e;
    }

    for (uint32_t i = 0; i < n; i++) ecs_entity_destroy(w, victims[i]);

    free(victims);
    return ECS_OK;
}