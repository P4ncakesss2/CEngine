#include "grab_system.h"
#include "GLFW/glfw3.h"
#include "../system.h"
#include "../ecs.h"
#include "../components.h"
#include "graphics/window.h"
#include "physics_system.h"
#include <stdlib.h>
#include <math.h>

typedef struct {
    char dummy;
} GrabSystem;

static void grab_free(void *sys_data) {
    if (sys_data) {
        free(sys_data);
    }
}

static void camera_forward(const FlyingCamera *cam, vec3 out) {
    out[0] = -sinf(cam->yaw) * cosf(cam->pitch);
    out[1] = sinf(cam->pitch);
    out[2] = -cosf(cam->yaw) * cosf(cam->pitch);
}

static void grab_release(Ecs *ecs, Grabber *grabber) {
    if (!grabber->holding) return;

    Entity held = (Entity)grabber->heldEntity;
    if (ecs_entity_alive(ecs, held)) {
        RigidBody *heldRb = ECS_GET(ecs, held, RigidBody);
        if (heldRb) {
            heldRb->gravityScale = grabber->savedGravityScale;
        }
    }
    grabber->holding = false;
}

static void grab_try_pickup(PhysicsSystem *phys, Ecs *ecs, Transform *t, Grabber *grabber, const vec3 forward) {
    vec3 castTranslation = {
        forward[0] * grabber->maxGrabDistance,
        forward[1] * grabber->maxGrabDistance,
        forward[2] * grabber->maxGrabDistance,
    };

    PhysicsRaycastHit hit;
    if (!physics_system_raycast(phys, ecs, t->position, castTranslation, UINT64_MAX, &hit)) {
        return;
    }
    if (!hit.hit) return;
    if (!ECS_HAS(ecs, hit.entity, RigidBody) || !ECS_HAS(ecs, hit.entity, Collider)) return;

    RigidBody *heldRb = ECS_GET(ecs, hit.entity, RigidBody);
    if (!heldRb || heldRb->type != RIGID_BODY_Dynamic) return;

    grabber->holding          = true;
    grabber->heldEntity       = (uint32_t)hit.entity;
    grabber->savedGravityScale = heldRb->gravityScale;
    heldRb->gravityScale = 0.0f;
}

static void grab_update_held(Ecs *ecs, Transform *t, Grabber *grabber, const vec3 forward) {
    Entity held = (Entity)grabber->heldEntity;

    bool stillValid = ecs_entity_alive(ecs, held)
                     && ECS_HAS(ecs, held, RigidBody)
                     && ECS_HAS(ecs, held, Transform);
    if (!stillValid) {
        grabber->holding = false;
        return;
    }

    RigidBody *heldRb = ECS_GET(ecs, held, RigidBody);
    Transform *heldT  = ECS_GET(ecs, held, Transform);

    vec3 holdPoint = {
        t->position[0] + forward[0] * grabber->holdDistance,
        t->position[1] + forward[1] * grabber->holdDistance,
        t->position[2] + forward[2] * grabber->holdDistance,
    };

    vec3 toHoldPoint;
    glm_vec3_sub(holdPoint, heldT->position, toHoldPoint);
    float dist = glm_vec3_norm(toHoldPoint);
    if (dist > grabber->maxGrabDistance) {
        grab_release(ecs, grabber);
        return;
    }

    vec3 desiredVel;
    glm_vec3_scale(toHoldPoint, grabber->pullStrength, desiredVel);

    float speed = glm_vec3_norm(desiredVel);
    if (speed > grabber->maxPullSpeed) {
        glm_vec3_scale(desiredVel, grabber->maxPullSpeed / speed, desiredVel);
    }

    glm_vec3_copy(desiredVel, heldRb->linearVelocity);
    glm_vec3_zero(heldRb->angularVelocity);
}

static void grab_system_update(void *sys_data, SystemManager *mgr, float dt) {
    (void)sys_data;
    (void)dt;
    Ecs *ecs = mgr->ecs;
    Window *win = mgr->window;
    PhysicsSystem *phys = SYSTEM_GET(mgr, Physics);

    ECS_EACH(ecs, ECS_MASK(COMPONENT_FlyingCamera, COMPONENT_Transform, COMPONENT_Grabber), e) {
        FlyingCamera *cam     = ECS_GET(ecs, e, FlyingCamera);
        Transform    *t       = ECS_GET(ecs, e, Transform);
        Grabber      *grabber = ECS_GET(ecs, e, Grabber);

        vec3 forward;
        camera_forward(cam, forward);

        if (window_key_pressed(win, GLFW_KEY_F)) {
            if (grabber->holding) {
                grab_release(ecs, grabber);
            } else {
                grab_try_pickup(phys, ecs, t, grabber, forward);
            }
        }

        if (grabber->holding) {
            grab_update_held(ecs, t, grabber, forward);
        }
    }
}

bool grab_init(SystemManager *mgr) {
    if (!mgr) return false;

    GrabSystem *sys_data = calloc(1, sizeof(GrabSystem));
    if (!sys_data) return false;

    system_type_register(mgr, SYSTEM_TYPE_Grab, sys_data, grab_free, grab_system_update);

    return true;
}