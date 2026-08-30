#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include "ecs.h"
#include "asset/asset.h"
#include "vfs/vfs.h"
#include "graphics/renderer.h"

typedef struct SceneManager {
    Ecs          *world;
    Vfs          *vfs;
    AssetManager *assets;
    Renderer     *renderer;

    uint32_t baseInstanceId;
    char     *pendingVpath;
    bool     hasPending;
} SceneManager;

EcsResult scene_manager_init(SceneManager *mgr, Ecs *world, Vfs* vfs, AssetManager *assets, Renderer* renderer, const char *level_pak_path);
void      scene_manager_free(SceneManager *mgr);

void      scene_manager_request_base_scene(SceneManager *mgr, const char *level_pak_path);
bool      scene_manager_transition_pending(const SceneManager *mgr);

EcsResult scene_manager_update(SceneManager *mgr);

EcsResult scene_manager_spawn_scene(SceneManager *mgr, const char *vpath, Entity anchor, uint32_t *out_instance_id);
EcsResult scene_manager_despawn_instance(SceneManager *mgr, uint32_t instance_id);
bool scene_manager_find_instance_by_anchor(const SceneManager *mgr, Entity anchor, uint32_t *out_instance_id);

#endif