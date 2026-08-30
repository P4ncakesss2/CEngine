#ifndef SCENES_H
#define SCENES_H

#include "ecs/ecs.h"
typedef void (*SceneBuildFn)(Ecs *w);

typedef struct {
    const char   *name;
    SceneBuildFn  build;
} SceneDef;

void scene_build_level1(Ecs *w);

static const SceneDef kScenes[] = {
    { "level1",    scene_build_level1    },
};
#define kSceneCount ((int)(sizeof(kScenes) / sizeof(kScenes[0])))

#endif
