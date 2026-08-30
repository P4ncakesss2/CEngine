#include "flyingcamera_system.h"
#include "GLFW/glfw3.h"
#include "../system.h"
#include "../ecs.h"
#include "../components.h" 
#include "graphics/window.h"     
#include <stdlib.h>
#include <math.h>
#include "physics_system.h"

typedef struct {
    char dummy; 
} FlyingCameraSystem;

static void flying_camera_free(void *sys_data) {
    if (sys_data) {
        free(sys_data);
    }
}

static void flying_camera_update(void *sys_data, SystemManager *mgr, float dt) {
    (void)sys_data;
    Ecs *ecs = mgr->ecs;
    Window *win = mgr->window;

    PhysicsSystem *phys = SYSTEM_GET(mgr, Physics);

    ECS_EACH(ecs, ECS_MASK(COMPONENT_FlyingCamera, COMPONENT_Transform, COMPONENT_Collider), e) {
        FlyingCamera *flyCam = ECS_GET(ecs, e, FlyingCamera);
        Transform *t = ECS_GET(ecs, e, Transform);
        Collider *col = ECS_GET(ecs, e, Collider);

        double dx, dy;
        window_get_mouse_delta(win, &dx, &dy);

        flyCam->yaw   -= (float)dx * flyCam->lookSensitivity;
        flyCam->pitch -= (float)dy * flyCam->lookSensitivity;

        if (flyCam->pitch > 1.5f)  flyCam->pitch = 1.5f;
        if (flyCam->pitch < -1.5f) flyCam->pitch = -1.5f;

        if (window_key_pressed(win, GLFW_KEY_TAB)) {
            window_set_cursor_mode(win, win->cursorMode == CURSOR_MODE_NORMAL ? CURSOR_MODE_DISABLED : CURSOR_MODE_NORMAL);
        }

        t->rotation[0] = flyCam->pitch;
        t->rotation[1] = flyCam->yaw;
        t->rotation[2] = 0.0f;

        vec3 forward = { sinf(flyCam->yaw), 0.0f, cosf(flyCam->yaw) };
        vec3 right   = { cosf(flyCam->yaw), 0.0f, -sinf(flyCam->yaw) };
        vec3 up      = { 0.0f, 1.0f, 0.0f };

        vec3 targetVelocity = {0.0f, 0.0f, 0.0f};
        if (window_key_down(win, GLFW_KEY_W)) glm_vec3_sub(targetVelocity, forward, targetVelocity);
        if (window_key_down(win, GLFW_KEY_S)) glm_vec3_add(targetVelocity, forward, targetVelocity);
        if (window_key_down(win, GLFW_KEY_D)) glm_vec3_add(targetVelocity, right, targetVelocity);
        if (window_key_down(win, GLFW_KEY_A)) glm_vec3_sub(targetVelocity, right, targetVelocity);
        if (window_key_down(win, GLFW_KEY_E)) glm_vec3_add(targetVelocity, up, targetVelocity);
        if (window_key_down(win, GLFW_KEY_Q)) glm_vec3_sub(targetVelocity, up, targetVelocity);

        if (glm_vec3_norm2(targetVelocity) > 0.0f) {
            glm_vec3_normalize(targetVelocity);
        }
        glm_vec3_scale(targetVelocity, flyCam->moveSpeed, targetVelocity);

        float smoothingFactor = fminf(1.0f, dt * 25.0f);
        glm_vec3_lerp(flyCam->currentVelocity, targetVelocity, smoothingFactor, flyCam->currentVelocity);

        physics_system_move_mover(phys, ecs, col, t->position, flyCam->currentVelocity, dt, UINT64_MAX);
    }
}

bool flying_camera_init(SystemManager *mgr) {
    if (!mgr) return false;

    FlyingCameraSystem *sys_data = calloc(1, sizeof(FlyingCameraSystem));
    if (!sys_data) return false;

    system_type_register(mgr, SYSTEM_TYPE_FlyingCamera, sys_data, flying_camera_free, flying_camera_update);

    return true;
}