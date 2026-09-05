#ifndef CAMERA_SYSTEM_H
#define CAMERA_SYSTEM_H

#include "../ecs.h"
#include "../system.h"
#include "graphics/renderer.h"
#include <stdbool.h>

typedef struct CameraSystem {
    Entity currentCamera;
} CameraSystem;

void camera_system_init(CameraSystem* sys);
void camera_system_free(CameraSystem* sys);
void camera_system_update(CameraSystem* sys, Ecs* ecs, float aspect);

bool camera_get_active(CameraSystem* sys, Ecs* ecs, Entity* out);

bool camera_system_get_view(CameraSystem* sys, Ecs* ecs, CameraView* out);

bool camera_system_type_init(SystemManager *mgr);

#endif