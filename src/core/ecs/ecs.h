#ifndef ECS_H
#define ECS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "components.h"

#define ECS_INDEX_BITS 20u
#define ECS_GEN_BITS   (32u - ECS_INDEX_BITS)
#define ECS_INDEX_MASK ((1u << ECS_INDEX_BITS) - 1u)
#define ECS_GEN_MASK   ((1u << ECS_GEN_BITS) - 1u)

#define ECS_ENTITY_INDEX(e) ((e) & ECS_INDEX_MASK)
#define ECS_ENTITY_GEN(e)   (((e) >> ECS_INDEX_BITS) & ECS_GEN_MASK)
#define ECS_MAKE_ENTITY(idx, gen) \
    ((Entity)(((uint32_t)(gen) & ECS_GEN_MASK) << ECS_INDEX_BITS | ((uint32_t)(idx) & ECS_INDEX_MASK)))

#define ECS_INVALID_ENTITY ((Entity)0xFFFFFFFFU)

#define ECS_INVALID_SCENE_INSTANCE ((uint32_t)0u)

typedef enum {
    ECS_OK = 0,
    ECS_ERR_OUT_OF_MEMORY,
    ECS_ERR_INVALID_ENTITY,
    ECS_ERR_INVALID_ARGUMENT,
    ECS_ERR_IO,
    ECS_ERR_BAD_MAGIC,
    ECS_ERR_BAD_VERSION,
    ECS_ERR_CORRUPT,
} EcsResult;

const char *ecs_result_str(EcsResult result);

typedef enum {
#define COMPONENT(Name) COMPONENT_##Name,
#include "components.def"
#undef COMPONENT
    COMPONENT_COUNT
} ComponentType;

#define ECS_MASK_WORDS ((COMPONENT_COUNT + 63) / 64)

typedef struct {
    uint64_t bits[ECS_MASK_WORDS];
} ComponentMask;

typedef uint32_t StringId;
#define ECS_INVALID_STRING_ID 0

typedef struct {
    char     *data;
    uint32_t  size;
    uint32_t  capacity;
} StringPool;

typedef struct Ecs {
    uint32_t capacity;
    uint32_t high_water;
    uint32_t alive_count;

    uint32_t *free_list;
    uint32_t  free_count;
    uint32_t  free_capacity;

    uint32_t nextInstanceId;
    StringPool string_pool;

    bool          *alive;
    uint32_t      *generation;
    ComponentMask *mask;
    Entity        *dense;
    uint32_t      *dense_index;

    uint32_t *instanceId;
    bool     *instanceIsNested;

#define COMPONENT(Name) Name *Name##_data;
#include "components.def"
#undef COMPONENT
} Ecs;

EcsResult ecs_init(Ecs *w);
void      ecs_free(Ecs *w);

StringId ecs_string_intern(Ecs *w, const char *str);
const char *ecs_string_get(const Ecs *w, StringId id);

EcsResult ecs_entity_create(Ecs *w, Entity *out);
EcsResult ecs_entity_destroy(Ecs *w, Entity e);
bool      ecs_entity_alive(const Ecs *w, Entity e);
EcsResult ecs_entity_clone(Ecs *w, Entity src, Entity *out);

#define ECS_WORD_OF(Name) (COMPONENT_##Name / 64)
#define ECS_BIT_OF(Name)  (COMPONENT_##Name % 64)

#define ECS_ADD(w, e, Name, ...)                                      \
    do {                                                              \
        assert(ecs_entity_alive((w), (e)));                           \
        uint32_t _idx = ECS_ENTITY_INDEX(e);                          \
        (w)->Name##_data[_idx] = (__VA_ARGS__);                       \
        (w)->mask[_idx].bits[ECS_WORD_OF(Name)] |=                    \
            ((uint64_t)1u << ECS_BIT_OF(Name));                       \
    } while (0)
    
#define ECS_HAS(w, e, Name)                                                 \
    (assert(ecs_entity_alive((w), (e))),                                    \
     (((w)->mask[ECS_ENTITY_INDEX(e)].bits[ECS_WORD_OF(Name)] >> ECS_BIT_OF(Name)) & 1u) != 0)

#define ECS_GET(w, e, Name)                                                 \
    (ECS_HAS((w), (e), Name) ? &(w)->Name##_data[ECS_ENTITY_INDEX(e)] : NULL)

#define ECS_REMOVE(w, e, Name)                                              \
    (assert(ecs_entity_alive((w), (e))),                                    \
     (w)->mask[ECS_ENTITY_INDEX(e)].bits[ECS_WORD_OF(Name)] &= ~((uint64_t)1u << ECS_BIT_OF(Name)))

static inline ComponentMask ecs_mask_v(int n, ...) {
    ComponentMask m;
    memset(&m, 0, sizeof(m));
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        int c = va_arg(ap, int);
        m.bits[c / 64] |= ((uint64_t)1u << (c % 64));
    }
    va_end(ap);
    return m;
}

#define ECS_MASK(...) \
    ecs_mask_v((int)(sizeof((int[]){__VA_ARGS__}) / sizeof(int)), __VA_ARGS__)

static inline bool ecs_has_all(const Ecs *w, Entity e, ComponentMask required) {
    uint32_t idx = ECS_ENTITY_INDEX(e);
    const ComponentMask *have = &w->mask[idx];
    for (int i = 0; i < ECS_MASK_WORDS; i++) {
        if ((have->bits[i] & required.bits[i]) != required.bits[i]) return false;
    }
    return true;
}

#define ECS_EACH(w, required, e)                                            \
    for (uint32_t _ecs_each_idx = 0; _ecs_each_idx < (w)->alive_count; _ecs_each_idx++) \
        for (Entity e = (w)->dense[_ecs_each_idx], _ecs_each_once = 1;      \
             _ecs_each_once && ecs_has_all((w), e, (required));             \
             _ecs_each_once = 0)

typedef struct Vfs Vfs;
typedef struct AssetManager AssetManager;

EcsResult ecs_save(const Ecs *w, Vfs *vfs, const char *path);
EcsResult ecs_load(Ecs *w, Vfs *vfs, const char *path);

EcsResult ecs_serialize(const Ecs *w, uint8_t **out_data, size_t *out_size);
EcsResult ecs_deserialize(Ecs *w, const uint8_t *data, size_t size);

bool      ecs_equal(const Ecs *a, const Ecs *b);
EcsResult ecs_clone(Ecs *dst, const Ecs *src);

EcsResult ecs_instantiate(Ecs *dst, AssetManager *assets, const Ecs *scene, Entity anchor, bool is_nested, uint32_t *out_instance_id);
EcsResult ecs_destroy_scene_instance(Ecs *w, uint32_t instance_id);

EcsResult ecs_destroy_all_scene_instances(Ecs *w);

bool ecs_find_scene_instance_by_anchor(const Ecs *w, Entity anchor, uint32_t *out_instance_id);

#endif