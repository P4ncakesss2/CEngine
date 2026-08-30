#include "scene.h"
#include <stdio.h>
#include <stdlib.h>

static void *scene_load(const char *vpath, const void *data, size_t size) {
    if (!data) {
        return NULL;
    }

    Ecs *scene = malloc(sizeof(Ecs));
    if (!scene) return NULL;

    EcsResult result = ecs_deserialize(scene, (const uint8_t *)data, size);
    if (result != ECS_OK) {
        free(scene);
        return NULL;
    }

    return scene;
}

static void scene_free(void *asset_data) {
    Ecs *scene = asset_data;
    if (!scene) return;
    ecs_free(scene);
    free(scene);
}

AssetResult scene_asset_type_init(AssetManager *mgr) {
    return asset_type_register(mgr, ASSET_TYPE_Scene, scene_load, scene_free);
}