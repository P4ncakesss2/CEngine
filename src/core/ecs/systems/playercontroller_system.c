#include "playercontroller_system.h"
#include "GLFW/glfw3.h"
#include "../system.h"
#include "../ecs.h"
#include "../components.h" 
#include "graphics/window.h"     
#include <stdlib.h>
#include <math.h>
#include "physics_system.h"

static float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

typedef struct {
    char dummy; 
} PlayerControllerSystem;

static void player_controller_free(void *sys_data) {
    if (sys_data) {
        free(sys_data);
    }
}

static void player_controller_update(void *sys_data, SystemManager *mgr, float dt) {
    (void)sys_data;
    Ecs *ecs = mgr->ecs;
    Window *win = mgr->window;

    PhysicsSystem *phys = SYSTEM_GET(mgr, Physics);

    ECS_EACH(ecs, ECS_MASK(COMPONENT_PlayerController, COMPONENT_Transform, COMPONENT_Collider), e) {
        PlayerController *player = ECS_GET(ecs, e, PlayerController);
        Transform *t = ECS_GET(ecs, e, Transform);
        Collider *col = ECS_GET(ecs, e, Collider);

        double dx, dy;
        window_get_mouse_delta(win, &dx, &dy);

        player->yaw   -= (float)dx * player->lookSensitivity;
        player->pitch -= (float)dy * player->lookSensitivity;

        if (player->pitch > 1.5f)  player->pitch = 1.5f;
        if (player->pitch < -1.5f) player->pitch = -1.5f;

        if (window_key_pressed(win, GLFW_KEY_TAB)) {
            window_set_cursor_mode(win, win->cursorMode == CURSOR_MODE_NORMAL ? CURSOR_MODE_DISABLED : CURSOR_MODE_NORMAL);
        }

        t->rotation[0] = player->pitch;
        t->rotation[1] = player->yaw;
        t->rotation[2] = 0.0f;

        vec3 forward = { sinf(player->yaw), 0.0f, cosf(player->yaw) };
        vec3 right   = { cosf(player->yaw), 0.0f, -sinf(player->yaw) };

        vec3 targetVelocity = {0.0f, 0.0f, 0.0f};
        if (window_key_down(win, GLFW_KEY_W)) glm_vec3_sub(targetVelocity, forward, targetVelocity);
        if (window_key_down(win, GLFW_KEY_S)) glm_vec3_add(targetVelocity, forward, targetVelocity);
        if (window_key_down(win, GLFW_KEY_D)) glm_vec3_add(targetVelocity, right, targetVelocity);
        if (window_key_down(win, GLFW_KEY_A)) glm_vec3_sub(targetVelocity, right, targetVelocity);

        if (glm_vec3_norm2(targetVelocity) > 0.0f) {
            glm_vec3_normalize(targetVelocity);
        }
        
        targetVelocity[0] *= player->moveSpeed;
        targetVelocity[2] *= player->moveSpeed;

        float verticalVelocity = player->currentVelocity[1];
        verticalVelocity -= phys->gravity * dt; 

        PhysicsRaycastHit hit;
        vec3 rayOrigin;
        glm_vec3_copy(t->position, rayOrigin);
        vec3 rayDir = {0.0f, -1.75f, 0.0f}; 
        
        bool grounded = physics_system_raycast(phys, ecs, rayOrigin, rayDir, UINT64_MAX, &hit); //[cite: 1]

        if (grounded && verticalVelocity < 0.0f) {
            verticalVelocity = -0.5f; 
            if (window_key_down(win, GLFW_KEY_SPACE)) {
                verticalVelocity = 4.5f;
            }
        }

        float smoothingFactor = fminf(1.0f, dt * 25.0f);
        player->currentVelocity[0] = lerp(player->currentVelocity[0], targetVelocity[0], smoothingFactor);
        player->currentVelocity[2] = lerp(player->currentVelocity[2], targetVelocity[2], smoothingFactor);
        player->currentVelocity[1] = verticalVelocity;

        physics_system_move_mover(phys, ecs, col, t->position, player->currentVelocity, dt, UINT64_MAX); 
    }
}

bool player_controller_type_init(SystemManager *mgr) {
    if (!mgr) return false;
    PlayerControllerSystem *sys_data = calloc(1, sizeof(PlayerControllerSystem));
    if (!sys_data) return false;

    system_type_register(mgr, SYSTEM_TYPE_PlayerController, sys_data, player_controller_free, player_controller_update);
    return true;
}