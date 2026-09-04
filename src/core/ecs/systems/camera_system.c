#include "camera_system.h"
#include <string.h>
#include "graphics/renderer.h"
#include <cglm/cglm.h>

void camera_system_init(CameraSystem *sys) {
    memset(sys, 0, sizeof(CameraSystem));
}

void camera_system_free(CameraSystem *sys) {
    memset(sys, 0, sizeof(CameraSystem));
}

void camera_system_update(CameraSystem *sys, Ecs *ecs, float aspect) {
    (void)sys;
    ECS_EACH(ecs, ECS_MASK(COMPONENT_Camera, COMPONENT_Transform), e) {
        Camera         *cam = ECS_GET(ecs, e, Camera);
        Transform *wt  = ECS_GET(ecs, e, Transform);
        if (!cam || !wt) continue;
        sys->currentCamera = e;

        vec3 up, forward, position;
        glm_vec3_copy(wt->matrix[1], up);
        glm_vec3_copy(wt->matrix[2], forward);
        glm_vec3_copy(wt->matrix[3], position);

        glm_vec3_normalize(up);
        glm_vec3_normalize(forward);

        vec3 center;
        center[0] = position[0] - forward[0];
        center[1] = position[1] - forward[1];
        center[2] = position[2] - forward[2];

        glm_lookat(position, center, up, cam->view);

        cam->aspect = aspect;
        if (cam->type == CAMERA_TYPE_Perspective) {
            glm_perspective(glm_rad(cam->fov), cam->aspect, cam->nearPlane, cam->farPlane, cam->proj);
        } else {
            float halfH = cam->orthoSize;
            float halfW = cam->orthoSize * cam->aspect;
            glm_ortho(-halfW, halfW, -halfH, halfH, cam->nearPlane, cam->farPlane, cam->proj);
        }

        cam->proj[1][1] *= -1.0f;
        glm_mat4_mul(cam->proj, cam->view, cam->viewproj);
        break;
    }
}

bool camera_get_active(CameraSystem* sys, Ecs* ecs, Entity* out) {
    if (out) *out = ECS_INVALID_ENTITY;
    if (!sys) return false;

    if (out) *out = sys->currentCamera;
    return ecs && ecs_entity_alive(ecs, sys->currentCamera);
}

void camera_system_set_override(CameraSystem* sys, mat4 viewproj, vec3 position) {
    if (!sys) return;
    sys->overrideActive = true;
    glm_mat4_copy(viewproj, sys->overrideViewproj);
    glm_vec3_copy(position, sys->overridePosition);
}

void camera_system_clear_override(CameraSystem* sys) {
    if (!sys) return;
    sys->overrideActive = false;
}

bool camera_system_get_view(CameraSystem* sys, Ecs* ecs, mat4 out_viewproj, vec3 out_position) {
    glm_mat4_identity(out_viewproj);
    glm_vec3_zero(out_position);
    if (!sys) return false;

    if (sys->overrideActive) {
        glm_mat4_copy(sys->overrideViewproj, out_viewproj);
        glm_vec3_copy(sys->overridePosition, out_position);
        return true;
    }

    Entity camEntity;
    bool camAlive = camera_get_active(sys, ecs, &camEntity);
    Camera*    cam       = camAlive ? ECS_GET(ecs, camEntity, Camera) : NULL;
    Transform* camTransform = camAlive ? ECS_GET(ecs, camEntity, Transform) : NULL;
    if (!cam || !camTransform) return false;

    glm_mat4_copy(cam->viewproj, out_viewproj);
    glm_vec3_copy(camTransform->position, out_position);
    return true;
}

#include <stdlib.h>

static void camera_system_type_free(void *data) {
    camera_system_free(data);
    free(data);
}

static void camera_system_type_update(void *data, SystemManager *mgr, float dt, float alpha) {
    (void)dt;
    VkExtent2D extent = renderer_get_extent(mgr->renderer);
    float aspect = (float)extent.width / extent.height;
    camera_system_update(data, mgr->ecs, aspect);
}

bool camera_system_type_init(SystemManager *mgr) {
    CameraSystem *sys = calloc(1, sizeof(CameraSystem));
    if (!sys) return false;
    camera_system_init(sys);
    system_type_register(mgr, SYSTEM_TYPE_Camera, sys, camera_system_type_free, camera_system_type_update, NULL);
    return true;
}