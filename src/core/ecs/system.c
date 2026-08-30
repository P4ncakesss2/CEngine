#include "system.h"
#include <string.h>

void system_type_register(SystemManager *mgr, SystemType type, void *data,
                           SystemFreeFn free_fn, SystemUpdateFn update_fn) {
    if (!mgr || type < 0 || type >= SYSTEM_TYPE_COUNT || !free_fn || !update_fn) return;
    SystemSlot *slot = &mgr->slots[type];
    slot->data       = data;
    slot->free_fn    = free_fn;
    slot->update_fn  = update_fn;
    slot->registered = true;
}

void *system_get_data(SystemManager *mgr, SystemType type) {
    if (!mgr || type < 0 || type >= SYSTEM_TYPE_COUNT) return NULL;
    SystemSlot *slot = &mgr->slots[type];
    return slot->registered ? slot->data : NULL;
}

bool system_manager_init(SystemManager *mgr, Ecs *ecs, Renderer *renderer,
                          AssetManager *assets, Window *window) {
    if (!mgr || !ecs) return false;
    memset(mgr, 0, sizeof(*mgr));
    mgr->ecs      = ecs;
    mgr->renderer = renderer;
    mgr->assets   = assets;
    mgr->window   = window;

#define SYSTEM_TYPE(Name, InitFn)                     \
    do {                                                \
        if (!InitFn(mgr)) {                              \
            system_manager_free(mgr);                     \
            return false;                                  \
        }                                                    \
    } while (0);
#include "systems.def"
#undef SYSTEM_TYPE

    return true;
}

void system_manager_free(SystemManager *mgr) {
    if (!mgr) return;
    for (int i = 0; i < SYSTEM_TYPE_COUNT; i++) {
        SystemSlot *slot = &mgr->slots[i];
        if (slot->registered && slot->data && slot->free_fn) {
            slot->free_fn(slot->data);
        }
    }
    memset(mgr, 0, sizeof(*mgr));
}

void system_manager_update(SystemManager *mgr, float dt) {
    if (!mgr) return;
    for (int i = 0; i < SYSTEM_TYPE_COUNT; i++) {
        SystemSlot *slot = &mgr->slots[i];
        if (slot->registered && slot->update_fn) {
            slot->update_fn(slot->data, mgr, dt);
        }
    }
}