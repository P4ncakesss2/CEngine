#include "ecs/scene.h"
#include <string.h>
#include <stdlib.h>
#include "ecs/ecs.h"

static EcsResult scene_manager_load_base_scene(SceneManager *mgr, const char *vpath) {
    ecs_destroy_all_scene_instances(mgr->world);
    mgr->baseInstanceId = ECS_INVALID_SCENE_INSTANCE;

    asset_manager_clear_scope(mgr->assets, ASSET_SCOPE_SCENE);
    asset_manager_begin_scope(mgr->assets, ASSET_SCOPE_SCENE);

    AssetHandle sceneHandle;
    AssetResult assetResult = asset_load(mgr->assets, ASSET_TYPE_Scene, vpath, &sceneHandle);
    if (assetResult != ASSET_OK) {
        asset_manager_end_scope(mgr->assets);
        return ECS_ERR_IO;
    }
    Ecs *scene = asset_get(mgr->assets, sceneHandle, ASSET_TYPE_Scene);
    if (!scene) {
        asset_manager_end_scope(mgr->assets);
        return ECS_ERR_IO;
    }

    uint32_t instanceId;
    EcsResult r = ecs_instantiate(mgr->world, mgr->assets, scene, ECS_INVALID_ENTITY, false, &instanceId);
    asset_manager_end_scope(mgr->assets);
    if (r != ECS_OK) return r;

    mgr->baseInstanceId = instanceId;
    return ECS_OK;
}

EcsResult scene_manager_init(SceneManager *mgr, Ecs *world, Vfs* vfs, AssetManager *assets, Renderer* renderer, const char *level_pak_path) {
    memset(mgr, 0, sizeof(*mgr));
    mgr->world          = world;
    mgr->assets         = assets;
    mgr->vfs            = vfs;
    mgr->renderer       = renderer;
    mgr->baseInstanceId = ECS_INVALID_SCENE_INSTANCE;

    if (!level_pak_path) return ECS_OK;
    return scene_manager_load_base_scene(mgr, level_pak_path);
}

void scene_manager_free(SceneManager *mgr) {
    if (!mgr) return;
    free(mgr->pendingVpath);
}

void scene_manager_request_base_scene(SceneManager *mgr, const char *vpath) {
    free(mgr->pendingVpath);
    mgr->pendingVpath = strdup(vpath);
    mgr->hasPending = true;
}

typedef struct {
    uint32_t instanceId;
} PendingDespawn;

typedef struct {
    Entity anchor;
    char   vpath[256];
} PendingSpawn;

EcsResult scene_manager_update(SceneManager *mgr) {
    uint32_t capacityHint = mgr->world->alive_count > 0 ? mgr->world->alive_count : 1;

    PendingDespawn *despawns = malloc(capacityHint * sizeof(PendingDespawn));
    PendingSpawn   *spawns   = malloc(capacityHint * sizeof(PendingSpawn));
    uint32_t despawnCount = 0;
    uint32_t spawnCount = 0;

    if (!despawns || !spawns) {
        free(despawns);
        free(spawns);
        return ECS_ERR_OUT_OF_MEMORY;
    }

    ECS_EACH(mgr->world, ECS_MASK(COMPONENT_Scene), e) {
        Scene* sceneComp = ECS_GET(mgr->world, e, Scene);
        if (!sceneComp) continue;

        uint32_t currentInstanceId = ECS_INVALID_SCENE_INSTANCE;
        bool exists = scene_manager_find_instance_by_anchor(mgr, e, &currentInstanceId);
        bool refCleared = sceneComp->sceneRef.vpath_id == ECS_INVALID_STRING_ID;

        if (exists && refCleared) {
            despawns[despawnCount++].instanceId = currentInstanceId;
            continue;
        }

        AssetHandle handle = asset_ref_resolve(mgr->assets, mgr->world, ASSET_TYPE_Scene, &sceneComp->sceneRef);
        if (!exists && handle != ASSET_INVALID_HANDLE) {
            const char* vpath = ecs_string_get(mgr->world, sceneComp->sceneRef.vpath_id);
            if (vpath) {
                spawns[spawnCount].anchor = e;
                snprintf(spawns[spawnCount].vpath, sizeof(spawns[spawnCount].vpath), "%s", vpath);
                spawnCount++;
            }
        }
    }

    for (uint32_t i = 0; i < despawnCount; i++) {
        scene_manager_despawn_instance(mgr, despawns[i].instanceId);
    }

    for (uint32_t i = 0; i < spawnCount; i++) {
        if (!ecs_entity_alive(mgr->world, spawns[i].anchor)) continue;
        uint32_t newInstanceId = ECS_INVALID_SCENE_INSTANCE;
        scene_manager_spawn_scene(mgr, spawns[i].vpath, spawns[i].anchor, &newInstanceId);
    }

    free(despawns);
    free(spawns);

    if (!mgr->hasPending) return ECS_OK;
    mgr->hasPending = false;

    char *vpath_to_load = mgr->pendingVpath;
    mgr->pendingVpath = NULL;

    EcsResult result = scene_manager_load_base_scene(mgr, vpath_to_load);
    free(vpath_to_load);
    return result;
}
bool scene_manager_transition_pending(const SceneManager *mgr) {
    return mgr->hasPending;
}

EcsResult scene_manager_spawn_scene(SceneManager *mgr, const char *vpath,
                                     Entity anchor, uint32_t *out_instance_id) {
    if (!mgr || !vpath) return ECS_ERR_INVALID_ARGUMENT;
    if (anchor != ECS_INVALID_ENTITY && !ecs_entity_alive(mgr->world, anchor))
        return ECS_ERR_INVALID_ENTITY;

    asset_manager_begin_scope(mgr->assets, ASSET_SCOPE_SCENE);

    AssetHandle sceneHandle;
    if (asset_load(mgr->assets, ASSET_TYPE_Scene, vpath, &sceneHandle) != ASSET_OK) {
        asset_manager_end_scope(mgr->assets);
        return ECS_ERR_IO;
    }
    Ecs *scene = asset_get(mgr->assets, sceneHandle, ASSET_TYPE_Scene);
    if (!scene) {
        asset_manager_end_scope(mgr->assets);
        return ECS_ERR_IO;
    }

    uint32_t instanceId;
    EcsResult r = ecs_instantiate(mgr->world, mgr->assets, scene, anchor, true, &instanceId);
    asset_manager_end_scope(mgr->assets);
    if (r != ECS_OK) return r;

    if (out_instance_id) *out_instance_id = instanceId;
    return ECS_OK;
}

EcsResult scene_manager_despawn_instance(SceneManager *mgr, uint32_t instance_id) {
    if (!mgr) return ECS_ERR_INVALID_ARGUMENT;
    return ecs_destroy_scene_instance(mgr->world, instance_id);
}

bool scene_manager_find_instance_by_anchor(const SceneManager *mgr, Entity anchor,
                                            uint32_t *out_instance_id) {
    if (!mgr) return false;
    return ecs_find_scene_instance_by_anchor(mgr->world, anchor, out_instance_id);
}