#include "camera_controller_system.h"
#include "GLFW/glfw3.h"
#include "../system.h"
#include "../ecs.h"
#include "../components.h"
#include "graphics/window.h"
#include "cvar.h"
#include <stdlib.h>

DEFINE_CVAR_FLOAT(cam_lookSensitivity, "cam_lookSensitivity", 0.005f);
DEFINE_CVAR_FLOAT(cam_fieldOfView, "cam_fieldOfView", 90.0f);
DEFINE_CVAR_FLOAT(cam_nearPlane, "cam_nearPlane", 0.025f);
DEFINE_CVAR_FLOAT(cam_farPlane, "cam_farPlane", 1000.0f);

typedef struct { char dummy; } CameraControllerSystem;

static void camera_controller_free(void *sys_data) {
    if (sys_data) free(sys_data);
}

static void camera_controller_update(void *sys_data, SystemManager *mgr, float dt, float alpha) {
    (void)sys_data; (void)dt; (void)alpha;
    Ecs *ecs = mgr->ecs;
    Window *win = mgr->window;

    if (window_key_pressed(win, GLFW_KEY_TAB)) {
        window_set_cursor_mode(win, win->cursorMode == CURSOR_MODE_NORMAL ? CURSOR_MODE_DISABLED : CURSOR_MODE_NORMAL);
    }

    ECS_EACH(ecs, ECS_MASK(COMPONENT_FirstPersonCamera, COMPONENT_Transform), e) {
        FirstPersonCamera *cam = ECS_GET(ecs, e, FirstPersonCamera);
        Transform *camT = ECS_GET(ecs, e, Transform);
        Camera* camC = ECS_GET(ecs, e, Camera);
        Parent* camP = ECS_GET(ecs, e, Parent);

        double dx, dy;
        window_get_mouse_delta(win, &dx, &dy);

        cam->yaw   -= (float)dx * cam_lookSensitivity.value.f;
        cam->pitch -= (float)dy * cam_lookSensitivity.value.f;
        if (cam->pitch > 1.5f)  cam->pitch = 1.5f;
        if (cam->pitch < -1.5f) cam->pitch = -1.5f;

        camT->rotation[0] = cam->pitch;

        if (camC) {
            camC->fov = cam_fieldOfView.value.f;
            camC->farPlane = cam_farPlane.value.f;
            camC->nearPlane = cam_nearPlane.value.f;
        }
        
        if (camP && ecs_entity_alive(ecs, camP->entity)) {
            Transform *bodyT = ECS_GET(ecs, camP->entity, Transform);
            if (bodyT) bodyT->rotation[1] = cam->yaw;
        }
    }
}

bool camera_controller_type_init(SystemManager *mgr) {
    if (!mgr) return false;
    CameraControllerSystem *sys_data = calloc(1, sizeof(CameraControllerSystem));
    if (!sys_data) return false;
    system_type_register(mgr, SYSTEM_TYPE_CameraController, sys_data, camera_controller_free, camera_controller_update, NULL);
    return true;
}