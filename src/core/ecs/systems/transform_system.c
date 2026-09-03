#include "transform_system.h"
#include "../ecs.h"
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#define STATE_COMPUTING 1
#define STATE_UPDATED   2

#define TRANSFORM_MAX_DEPTH 256

static void compute_local_to_world(const Transform *t, mat4 parent_mat, mat4 out) {
    mat4 local;
    glm_mat4_identity(local);

    if (t) {
        glm_translate(local, t->position);
        glm_rotate(local, t->rotation[1], (vec3){0.0f, 1.0f, 0.0f});
        glm_rotate(local, t->rotation[0], (vec3){1.0f, 0.0f, 0.0f});
        glm_rotate(local, t->rotation[2], (vec3){0.0f, 0.0f, 1.0f});
        glm_scale(local, t->scale);
    }

    glm_mat4_mul(parent_mat, local, out);
}

static void update_transform(Ecs *w, Entity e, uint8_t *state, int depth) {
    uint32_t idx = ECS_ENTITY_INDEX(e);
    if (state[idx] == STATE_UPDATED) {
        return;
    }

    if (state[idx] == STATE_COMPUTING) {
        fprintf(stderr, "transform_system: parent cycle detected at entity %u, breaking cycle\n", e);
        Transform *wt = ECS_GET(w, e, Transform);
        if (wt) {
            mat4 identity;
            glm_mat4_identity(identity);
            compute_local_to_world(wt, identity, wt->matrix);
        }
        state[idx] = STATE_UPDATED;
        return;
    }

    if (depth >= TRANSFORM_MAX_DEPTH) {
        state[idx] = STATE_UPDATED;
        Transform *wt = ECS_GET(w, e, Transform);
        if (wt) {
            mat4 identity;
            glm_mat4_identity(identity);
            compute_local_to_world(wt, identity, wt->matrix);
        }
        return;
    }

    state[idx] = STATE_COMPUTING;
    Transform *wt = ECS_GET(w, e, Transform);
    if (!wt) {
        state[idx] = STATE_UPDATED;
        return;
    }
    if (wt->worldOverrideActive) {
        mat4 m;
        glm_mat4_copy(wt->worldOverride, m);
        glm_scale(m, wt->scale);
        glm_mat4_copy(m, wt->matrix);
        state[idx] = STATE_UPDATED;
        return;
    }

    mat4 parent_mat;
    glm_mat4_identity(parent_mat);

    Parent *p = ECS_GET(w, e, Parent);
    if (p && ecs_entity_alive(w, p->entity)) {
        update_transform(w, p->entity, state, depth + 1);

        uint32_t pidx = ECS_ENTITY_INDEX(p->entity);
        if (state[pidx] == STATE_UPDATED) {
            Transform *pwt = ECS_GET(w, p->entity, Transform);
            if (pwt) {
                glm_mat4_copy(pwt->matrix, parent_mat);
            }
        }
    }

    compute_local_to_world(wt, parent_mat, wt->matrix);

    state[idx] = STATE_UPDATED;
}

void transform_system_init(TransformSystem* sys) {
    memset(sys, 0, sizeof(TransformSystem));
}

void transform_system_free(TransformSystem* sys) {
    if (sys->state) {
        free(sys->state);
    }
    memset(sys, 0, sizeof(TransformSystem));
}

void transform_system_update(TransformSystem* sys, Ecs *w) {
    if (sys->capacity < w->capacity) {
        uint8_t* new_state = realloc(sys->state, w->capacity * sizeof(uint8_t));
        if (!new_state) return;
        sys->state = new_state;
        sys->capacity = w->capacity;
    }

    if (!sys->state) return;

    memset(sys->state, 0, w->capacity * sizeof(uint8_t));

    ECS_EACH(w, ECS_MASK(COMPONENT_Transform), e) {
        update_transform(w, e, sys->state, 0);
    }
}

static void transform_system_type_free(void *data) {
    transform_system_free(data);
    free(data);
}

static void transform_system_type_update(void *data, SystemManager *mgr, float dt, float alpha) {
    (void)dt;
    transform_system_update(data, mgr->ecs);
}

bool transform_system_type_init(SystemManager *mgr) {
    TransformSystem *sys = calloc(1, sizeof(TransformSystem));
    if (!sys) return false;
    transform_system_init(sys);
    system_type_register(mgr, SYSTEM_TYPE_Transform, sys, transform_system_type_free, transform_system_type_update, NULL);
    return true;
}