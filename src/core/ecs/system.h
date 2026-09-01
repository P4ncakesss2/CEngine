#ifndef SYSTEM_H
#define SYSTEM_H

#include "ecs.h"
#include <stdbool.h>

typedef struct Renderer     Renderer;
typedef struct AssetManager AssetManager;
typedef struct Window       Window;

typedef enum {
#define SYSTEM_TYPE(Name, InitFn) SYSTEM_TYPE_##Name,
#include "systems.def"
#undef SYSTEM_TYPE
    SYSTEM_TYPE_COUNT
} SystemType;

typedef struct SystemManager SystemManager;

typedef void (*SystemFreeFn)(void *sys_data);
typedef void (*SystemUpdateFn)(void *sys_data, SystemManager *mgr, float dt, float alpha);
typedef void (*SystemFixedUpdateFn)(void *sys_data, SystemManager *mgr, float fixed_dt);
typedef bool (*SystemInitFn)(SystemManager *mgr);

#define SYSTEM_TYPE(Name, InitFn) bool InitFn(SystemManager *mgr);
#include "systems.def"
#undef SYSTEM_TYPE

typedef struct SystemSlot {
    void          *data;
    SystemFreeFn   free_fn;
    SystemUpdateFn update_fn;
    SystemFixedUpdateFn fixed_update_fn;
    bool           registered;
} SystemSlot;

struct SystemManager {
    Ecs          *ecs;
    Renderer     *renderer;
    AssetManager *assets;
    Window       *window;

    SystemSlot slots[SYSTEM_TYPE_COUNT];
};

void system_type_register(SystemManager *mgr, SystemType type, void *data, SystemFreeFn free_fn, SystemUpdateFn update_fn, SystemFixedUpdateFn fixed_update_fn);

bool system_manager_init(SystemManager *mgr, Ecs *ecs, Renderer *renderer, AssetManager *assets, Window *window);
void system_manager_free(SystemManager *mgr);
void system_manager_update(SystemManager *mgr, float dt, float alpha);
void system_manager_fixed_update(SystemManager* mgr, float fixed_dt);

void *system_get_data(SystemManager *mgr, SystemType type);
#define SYSTEM_GET(mgr, Type) ((Type##System *)system_get_data((mgr), SYSTEM_TYPE_##Type))

#endif