#ifndef CAMERA_SYSTEM_H
#define CAMERA_SYSTEM_H

#include "../ecs.h"
#include "../system.h"
#include <stdbool.h>

typedef struct CameraSystem {
    Entity currentCamera;

    bool overrideActive;
    mat4 overrideViewproj;
    vec3 overridePosition;
} CameraSystem;

void camera_system_init(CameraSystem* sys);
void camera_system_free(CameraSystem* sys);
void camera_system_update(CameraSystem* sys, Ecs* ecs, float aspect);

bool camera_get_active(CameraSystem* sys, Ecs* ecs, Entity* out);

void camera_system_set_override(CameraSystem* sys, mat4 viewproj, vec3 position);
void camera_system_clear_override(CameraSystem* sys);

bool camera_system_get_view(CameraSystem* sys, Ecs* ecs, mat4 out_viewproj, vec3 out_position);

bool camera_system_type_init(SystemManager *mgr);

#endif