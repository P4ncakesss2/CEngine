#ifndef RENDER_SYSTEM_H
#define RENDER_SYSTEM_H

#include "../ecs.h"
#include "camera_system.h"
#include "../system.h"
#include "../../graphics/renderer.h"

typedef struct RenderSystem {
    float _pad;
} RenderSystem;

void render_system_init(RenderSystem* sys);
void render_system_free(RenderSystem* sys);
void render_system_update(RenderSystem* sys, Ecs* ecs, CameraSystem* camera, Renderer* renderer);

void render_system_render(RenderSystem* sys, Ecs* ecs, Renderer* renderer,
                           mat4 viewproj, vec3 camPos, bool camValid);

bool render_system_type_init(SystemManager *mgr);

#endif