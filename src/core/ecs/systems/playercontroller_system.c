#include "playercontroller_system.h"
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
} PlayerControllerSystem;

static void apply_friction(vec3 velocity, float friction, float dt) {
    float speed = sqrtf(velocity[0] * velocity[0] + velocity[2] * velocity[2]);
    if (speed < 0.0001f) {
        velocity[0] = 0.0f;
        velocity[2] = 0.0f;
        return;
    }

    float drop = speed * friction * dt;
    float newSpeed = speed - drop;
    if (newSpeed < 0.0f) newSpeed = 0.0f;

    float scale = newSpeed / speed;
    velocity[0] *= scale;
    velocity[2] *= scale;
}

static void accelerate(vec3 velocity, const vec3 wishDir, float wishSpeed, float accel, float dt) {
    float currentSpeed = velocity[0] * wishDir[0] + velocity[2] * wishDir[2];
    float addSpeed = wishSpeed - currentSpeed;
    if (addSpeed <= 0.0f) return;

    float accelSpeed = accel * dt * wishSpeed;
    if (accelSpeed > addSpeed) accelSpeed = addSpeed;

    velocity[0] += accelSpeed * wishDir[0];
    velocity[2] += accelSpeed * wishDir[2];
}

static void player_controller_free(void *sys_data) {
    if (sys_data) {
        free(sys_data);
    }
}

static void player_controller_update(void *sys_data, SystemManager *mgr, float dt, float alpha) {
    (void)sys_data;
    (void)dt;
    (void)alpha;
    Ecs *ecs = mgr->ecs;
    Window *win = mgr->window;

    ECS_EACH(ecs, ECS_MASK(COMPONENT_PlayerController, COMPONENT_Transform), e) {
        PlayerController *player = ECS_GET(ecs, e, PlayerController);
        Transform *t = ECS_GET(ecs, e, Transform);

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
    }
}

static void player_controller_fixed_update(void *sys_data, SystemManager *mgr, float fixed_dt) {
    (void)sys_data;
    Ecs *ecs = mgr->ecs;
    Window *win = mgr->window;

    PhysicsSystem *phys = SYSTEM_GET(mgr, Physics);

    ECS_EACH(ecs, ECS_MASK(COMPONENT_PlayerController, COMPONENT_Transform, COMPONENT_Collider, COMPONENT_CharacterMover), e) {
        PlayerController *player = ECS_GET(ecs, e, PlayerController);
        Transform *t = ECS_GET(ecs, e, Transform);
        Collider *col = ECS_GET(ecs, e, Collider);
        CharacterMover* mover = ECS_GET(ecs, e, CharacterMover);

        vec3 forward = { sinf(player->yaw), 0.0f, cosf(player->yaw) };
        vec3 right   = { cosf(player->yaw), 0.0f, -sinf(player->yaw) };

        vec3 wishDir = {0.0f, 0.0f, 0.0f};
        if (window_key_down(win, GLFW_KEY_W)) glm_vec3_sub(wishDir, forward, wishDir);
        if (window_key_down(win, GLFW_KEY_S)) glm_vec3_add(wishDir, forward, wishDir);
        if (window_key_down(win, GLFW_KEY_D)) glm_vec3_add(wishDir, right, wishDir);
        if (window_key_down(win, GLFW_KEY_A)) glm_vec3_sub(wishDir, right, wishDir);

        if (glm_vec3_norm2(wishDir) > 0.0f) {
            glm_vec3_normalize(wishDir);
        }

        PhysicsRaycastHit hit;
        vec3 rayOrigin;
        glm_vec3_copy(t->position, rayOrigin);
        vec3 rayDir = {0.0f, -1.75f, 0.0f};

        bool grounded = physics_system_raycast(phys, ecs, rayOrigin, rayDir, UINT64_MAX, &hit);

        if (grounded) {
            apply_friction(mover->velocity, player->groundFriction, fixed_dt);
            accelerate(mover->velocity, wishDir, player->maxGroundSpeed, player->groundAccel, fixed_dt);
        } else {
            accelerate(mover->velocity, wishDir, player->maxAirSpeed, player->airAccel, fixed_dt);
        }

        float verticalVelocity = mover->velocity[1];
        verticalVelocity -= phys->gravity * fixed_dt;

        if (grounded && verticalVelocity < 0.0f) {
            verticalVelocity = -0.5f;
            if (window_key_down(win, GLFW_KEY_SPACE)) {
                verticalVelocity = player->jumpForce;
            }
        }

        mover->velocity[1] = verticalVelocity;
        player->wasGrounded = grounded;
    }
}

bool player_controller_type_init(SystemManager *mgr) {
    if (!mgr) return false;
    PlayerControllerSystem *sys_data = calloc(1, sizeof(PlayerControllerSystem));
    if (!sys_data) return false;

    system_type_register(mgr, SYSTEM_TYPE_PlayerController, sys_data, player_controller_free, player_controller_update, player_controller_fixed_update);
    return true;
}