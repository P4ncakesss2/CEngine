#include "player_controller_system.h"
#include "../system.h"
#include "../ecs.h"
#include "../components.h" 
#include "cglm/vec3.h"
#include "graphics/window.h"     
#include <stdlib.h>
#include <math.h>
#include "physics_system.h"
#include "cvar.h"

DEFINE_CVAR_FLOAT(phys_groundSnapVelocity, "phys_groundSnapVelocity", -0.5f);
DEFINE_CVAR_FLOAT(phys_maxGroundSpeed, "phys_maxGroundSpeed", 6.0f);
DEFINE_CVAR_FLOAT(phys_maxAirSpeed,    "phys_maxAirSpeed",    1.5f);
DEFINE_CVAR_FLOAT(phys_groundAccel,    "phys_groundAccel",    10.0f);
DEFINE_CVAR_FLOAT(phys_airAccel,       "phys_airAccel",       10.0f);
DEFINE_CVAR_FLOAT(phys_groundFriction, "phys_groundFriction", 6.0f);
DEFINE_CVAR_FLOAT(phys_jumpForce,      "phys_jumpForce",      5.0f);

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

    float scale = expf(-friction * dt);
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
        (void)t; (void)col;

        float yaw = t->rotation[1];
        vec3 forward = { -sinf(yaw), 0.0f, -cosf(yaw) };
        vec3 right   = { cosf(yaw), 0.0f, -sinf(yaw) };

        vec3 wishDir = {0.0f, 0.0f, 0.0f};
        if (window_key_down(win, KEY_W)) glm_vec3_add(wishDir, forward, wishDir);
        if (window_key_down(win, KEY_S)) glm_vec3_sub(wishDir, forward, wishDir);
        if (window_key_down(win, KEY_D)) glm_vec3_add(wishDir, right, wishDir);
        if (window_key_down(win, KEY_A)) glm_vec3_sub(wishDir, right, wishDir);

        if (glm_vec3_norm2(wishDir) > 0.0f) {
            glm_vec3_normalize(wishDir);
        }

        bool grounded = mover->isFloor;

        if (grounded) {
            apply_friction(mover->velocity, phys_groundFriction.value.f, fixed_dt);
            accelerate(mover->velocity, wishDir, phys_maxGroundSpeed.value.f, phys_groundAccel.value.f, fixed_dt);
        } else {
            accelerate(mover->velocity, wishDir, phys_maxAirSpeed.value.f, phys_airAccel.value.f, fixed_dt);
        }

        vec3 scaledGravity;
        glm_vec3_scale(phys->gravity, fixed_dt, scaledGravity);
        glm_vec3_add(mover->velocity, scaledGravity, mover->velocity);

        if (grounded && window_key_down(win, KEY_SPACE)) {
            mover->velocity[1] = phys_jumpForce.value.f;
        } else if (grounded && mover->velocity[1] < 0.0f) {
            mover->velocity[1] = phys_groundSnapVelocity.value.f;
        } else if (mover->isCeiling && mover->velocity[1] > 0.0f) {
            mover->velocity[1] = 0.0f; // bonk
        }

        mover->dirty = true;
    }
}

bool player_controller_type_init(SystemManager *mgr) {
    if (!mgr) return false;
    PlayerControllerSystem *sys_data = calloc(1, sizeof(PlayerControllerSystem));
    if (!sys_data) return false;

    system_type_register(mgr, SYSTEM_TYPE_PlayerController, sys_data, player_controller_free, NULL, player_controller_fixed_update);
    return true;
}