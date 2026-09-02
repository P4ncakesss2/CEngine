#include "physics_system.h"
#include "ecs/components.h"
#include "ecs/ecs.h"
#include "ecs/system.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#define PHYSICS_PI 3.14159265358979323846f
#define PHYSICS_MOVER_MAX_PLANES 16
#define PHYSICS_MOVER_SUBITERATIONS 5
#define PHYSICS_MOVER_SKIN 0.01f

static b3Vec3 to_b3vec3(const vec3 v) {
    b3Vec3 out;
    out.x = v[0];
    out.y = v[1];
    out.z = v[2];
    return out;
}

static b3Pos to_b3pos(const vec3 v) {
    b3Pos out;
    out.x = v[0];
    out.y = v[1];
    out.z = v[2];
    return out;
}

static void* entity_to_userdata(Entity e) {
    return (void*)(uintptr_t)e;
}

static Entity userdata_to_entity(void *ud) {
    return (Entity)(uintptr_t)ud;
}

static void b3quat_to_versor(b3Quat q, versor out) {
    out[0] = q.v.x;
    out[1] = q.v.y;
    out[2] = q.v.z;
    out[3] = q.s;
}

static void mat4_to_euler_yxz(mat4 m, vec3 out_rotation) {
    float sx = glm_clamp(m[2][1], -1.0f, 1.0f);
    out_rotation[0] = asinf(-sx);

    const float poleEps = 1e-6f;
    if (1.0f - fabsf(sx) < poleEps) {
        out_rotation[1] = atan2f(-m[0][2], m[0][0]);
        out_rotation[2] = 0.0f;
    } else {
        out_rotation[1] = atan2f(m[2][0], m[2][2]);
        out_rotation[2] = atan2f(m[0][1], m[1][1]);
    }
}

static void euler_to_glm_quat(const vec3 rotation, versor out) {
    mat4 m;
    glm_mat4_identity(m);
    glm_rotate(m, rotation[1], (vec3){0.0f, 1.0f, 0.0f});
    glm_rotate(m, rotation[0], (vec3){1.0f, 0.0f, 0.0f});
    glm_rotate(m, rotation[2], (vec3){0.0f, 0.0f, 1.0f});
    glm_mat4_quat(m, out);
}

static void mat4_rotation_only(mat4 in, mat4 out) {
    glm_mat4_copy(in, out);
    glm_vec3_normalize(out[0]);
    glm_vec3_normalize(out[1]);
    glm_vec3_normalize(out[2]);
    out[3][0] = out[3][1] = out[3][2] = 0.0f;
}

static void compute_world_transform(Ecs *w, Entity e, const Transform *t, vec3 out_pos, b3Quat *out_rot) {
    mat4 local;
    glm_mat4_identity(local);
    glm_translate(local, (float *)t->position);
    glm_rotate(local, t->rotation[1], (vec3){0.0f, 1.0f, 0.0f});
    glm_rotate(local, t->rotation[0], (vec3){1.0f, 0.0f, 0.0f});
    glm_rotate(local, t->rotation[2], (vec3){0.0f, 0.0f, 1.0f});

    mat4 parent_mat;
    glm_mat4_identity(parent_mat);

    Parent *p = ECS_GET(w, e, Parent);
    if (p && ecs_entity_alive(w, p->entity)) {
        Transform *pt = ECS_GET(w, p->entity, Transform);
        if (pt) {
            glm_mat4_copy(pt->matrix, parent_mat);
        }
    }

    mat4 world;
    glm_mat4_mul(parent_mat, local, world);
    out_pos[0] = world[3][0];
    out_pos[1] = world[3][1];
    out_pos[2] = world[3][2];

    mat4 parentRotOnly;
    mat4_rotation_only(parent_mat, parentRotOnly);

    versor parentRotQuat, localRotQuat, rotQuat;
    glm_mat4_quat(parentRotOnly, parentRotQuat);
    euler_to_glm_quat(t->rotation, localRotQuat);
    glm_quat_mul(parentRotQuat, localRotQuat, rotQuat);

    out_rot->v.x = rotQuat[0];
    out_rot->v.y = rotQuat[1];
    out_rot->v.z = rotQuat[2];
    out_rot->s   = rotQuat[3];
}

static bool bodies_reserve(PhysicsSystem *sys, uint32_t needed) {
    if (needed <= sys->bodyCapacity) return true;
    uint32_t new_cap = sys->bodyCapacity ? sys->bodyCapacity * 2 : 64;
    while (new_cap < needed) new_cap *= 2;
    PhysicsBodyEntry *p = realloc(sys->bodies, new_cap * sizeof(PhysicsBodyEntry));
    if (!p) return false;
    sys->bodies = p;
    sys->bodyCapacity = new_cap;
    return true;
}

static bool movers_reserve(PhysicsSystem *sys, uint32_t needed) {
    if (needed <= sys->moverCapacity) return true;
    uint32_t new_cap = sys->moverCapacity ? sys->moverCapacity * 2 : 64;
    while (new_cap < needed) new_cap *= 2;
    PhysicsMoverEntry *p = realloc(sys->movers, new_cap * sizeof(PhysicsMoverEntry));
    if (!p) return false;
    sys->movers = p;
    sys->moverCapacity = new_cap;
    return true;
}

static bool entity_to_body_reserve(PhysicsSystem *sys, Entity e) {
    if (e < sys->entityToBodyCapacity) return true;
    uint32_t new_cap = sys->entityToBodyCapacity ? sys->entityToBodyCapacity : 64;
    while (new_cap <= e) {
        if (new_cap > UINT32_MAX / 2) { new_cap = e + 1; break; }
        new_cap *= 2;
    }
    uint32_t *p = realloc(sys->entityToBody, new_cap * sizeof(uint32_t));
    if (!p) return false;
    for (uint32_t i = sys->entityToBodyCapacity; i < new_cap; ++i) p[i] = PHYSICS_NO_BODY;
    sys->entityToBody = p;
    sys->entityToBodyCapacity = new_cap;
    return true;
}

static bool entity_to_mover_reserve(PhysicsSystem *sys, Entity e) {
    if (e < sys->entityToMoverCapacity) return true;
    uint32_t new_cap = sys->entityToMoverCapacity ? sys->entityToMoverCapacity : 64;
    while (new_cap <= e) {
        if (new_cap > UINT32_MAX / 2) { new_cap = e + 1; break; }
        new_cap *= 2;
    }
    uint32_t *p = realloc(sys->entityToMover, new_cap * sizeof(uint32_t));
    if (!p) return false;
    for (uint32_t i = sys->entityToMoverCapacity; i < new_cap; ++i) p[i] = PHYSICS_NO_MOVER;
    sys->entityToMover = p;
    sys->entityToMoverCapacity = new_cap;
    return true;
}

static int32_t find_body_index(const PhysicsSystem *sys, Entity e) {
    if (!sys) return -1;
    if (e < sys->entityToBodyCapacity) {
        uint32_t index = sys->entityToBody[e];
        if (index != PHYSICS_NO_BODY && index < sys->bodyCount && sys->bodies[index].entity == e)
            return (int32_t)index;
    }
    for (uint32_t i = 0; i < sys->bodyCount; ++i)
        if (sys->bodies[i].entity == e) return (int32_t)i;
    return -1;
}

static int32_t find_mover_index(const PhysicsSystem *sys, Entity e) {
    if (!sys) return -1;
    if (e < sys->entityToMoverCapacity) {
        uint32_t index = sys->entityToMover[e];
        if (index != PHYSICS_NO_MOVER && index < sys->moverCount && sys->movers[index].entity == e)
            return (int32_t)index;
    }
    for (uint32_t i = 0; i < sys->moverCount; ++i)
        if (sys->movers[i].entity == e) return (int32_t)i;
    return -1;
}

static b3Filter collider_filter(const Collider *col) {
    b3Filter filter;
    filter.categoryBits = col->categoryBits;
    filter.maskBits      = col->maskBits;
    filter.groupIndex    = col->groupIndex;
    return filter;
}

static float collider_volume(const Collider *col) {
    switch (col->type) {
        case COLLIDER_Box: {
            float hx = col->box.halfExtents[0];
            float hy = col->box.halfExtents[1];
            float hz = col->box.halfExtents[2];
            return 8.0f * hx * hy * hz;
        }
        case COLLIDER_Sphere: {
            float r = col->sphere.radius;
            return (4.0f / 3.0f) * PHYSICS_PI * r * r * r;
        }
        case COLLIDER_Capsule: {
            float r = col->capsule.radius;
            float cylinderHeight = col->capsule.height - 2.0f * r;
            if (cylinderHeight < 0.0f) cylinderHeight = 0.0f;
            float cylinderVolume = PHYSICS_PI * r * r * cylinderHeight;
            float sphereVolume   = (4.0f / 3.0f) * PHYSICS_PI * r * r * r;
            return cylinderVolume + sphereVolume;
        }
        default:
            return 0.0f;
    }
}

static b3ShapeId create_collider(PhysicsSystem *sys, b3BodyId bodyId, const Collider *col, Entity e) {
    if (!sys || B3_IS_NULL(bodyId) || !col) return b3_nullShapeId;

    b3ShapeDef shapeDef = b3DefaultShapeDef();
    shapeDef.density                  = col->density;
    shapeDef.baseMaterial.friction    = col->friction;
    shapeDef.baseMaterial.restitution = col->restitution;
    shapeDef.isSensor                 = col->isSensor;
    shapeDef.userData                 = entity_to_userdata(e);
    shapeDef.enableSensorEvents       = col->isSensor;
    shapeDef.enableContactEvents      = col->enableContactEvents;
    shapeDef.enableHitEvents          = col->enableHitEvents;
    shapeDef.filter                   = collider_filter(col);

    if (col->mass > 0.0f) {
        float volume = collider_volume(col);
        if (volume > 0.0f) shapeDef.density = col->mass / volume;
    }

    switch (col->type) {
        case COLLIDER_Box: {
            b3BoxHull hull = b3MakeOffsetBoxHull(col->box.halfExtents[0],
                                                 col->box.halfExtents[1],
                                                 col->box.halfExtents[2],
                                                 to_b3vec3(col->offset));
            return b3CreateHullShape(bodyId, &shapeDef, &hull.base);
        }
        case COLLIDER_Sphere: {
            b3Sphere sphere;
            sphere.center = to_b3vec3(col->offset);
            sphere.radius = col->sphere.radius;
            return b3CreateSphereShape(bodyId, &shapeDef, &sphere);
        }
        case COLLIDER_Capsule: {
            float halfHeight = col->capsule.height * 0.5f;
            vec3 c1 = {col->offset[0], col->offset[1] - halfHeight, col->offset[2]};
            vec3 c2 = {col->offset[0], col->offset[1] + halfHeight, col->offset[2]};
            b3Capsule capsule;
            capsule.center1 = to_b3vec3(c1);
            capsule.center2 = to_b3vec3(c2);
            capsule.radius  = col->capsule.radius;
            return b3CreateCapsuleShape(bodyId, &shapeDef, &capsule);
        }
        default:
            return b3_nullShapeId;
    }
}

// Handles entities whose Transform was written directly by gameplay code
// this frame (teleports, respawns, cutscene placement, etc). Without this,
// a teleport would either get physically resolved as an infinite-speed
// move (bodies) or get blended from the old position over `alpha`
// (interpolation), producing a visible slide/wall-clip instead of a clean
// snap. Existing bodies/movers are hard-set to the new transform and their
// previous-frame cache is snapped to match so interpolation doesn't blend
// across the jump.
static void apply_dirty_transforms(PhysicsSystem *sys, Ecs *w) {
    ECS_EACH(w, ECS_MASK(COMPONENT_Transform, COMPONENT_RigidBody), e) {
        Transform *t = ECS_GET(w, e, Transform);
        if (!t->dirty) continue;

        int32_t bodyIndex = find_body_index(sys, e);
        if (bodyIndex >= 0) {
            vec3 worldPos;
            b3Quat worldRot;
            compute_world_transform(w, e, t, worldPos, &worldRot);

            b3Body_SetTransform(sys->bodies[bodyIndex].bodyId, to_b3pos(worldPos), worldRot);
            sys->bodies[bodyIndex].prevPos = to_b3vec3(worldPos);
            sys->bodies[bodyIndex].prevRot = worldRot;
        }
        t->dirty = false;
    }

    ECS_EACH(w, ECS_MASK(COMPONENT_Transform, COMPONENT_CharacterMover), e) {
        Transform *t = ECS_GET(w, e, Transform);
        if (!t->dirty) continue;

        int32_t moverIndex = find_mover_index(sys, e);
        if (moverIndex >= 0) {
            b3Vec3 pos = to_b3vec3(t->position);
            sys->movers[moverIndex].prevPos = pos;
            sys->movers[moverIndex].currPos = pos;
        }
        t->dirty = false;
    }
}

static void spawn_new_bodies(PhysicsSystem *sys, Ecs *w) {
    ECS_EACH(w, ECS_MASK(COMPONENT_RigidBody, COMPONENT_Transform, COMPONENT_Collider), e) {
        RigidBody *rb = ECS_GET(w, e, RigidBody);
        Transform *t  = ECS_GET(w, e, Transform);
        Collider *col = ECS_GET(w, e, Collider);
        if (!rb || !t || !col || find_body_index(sys, e) >= 0) continue;

        vec3 worldPos;
        b3Quat worldRot;
        compute_world_transform(w, e, t, worldPos, &worldRot);

        b3BodyDef bodyDef = b3DefaultBodyDef();
        bodyDef.position = to_b3pos(worldPos);
        bodyDef.rotation = worldRot;
        bodyDef.linearVelocity  = to_b3vec3(rb->linearVelocity);
        bodyDef.angularVelocity = to_b3vec3(rb->angularVelocity);
        bodyDef.gravityScale    = rb->gravityScale;
        bodyDef.linearDamping   = rb->linearDamping;
        bodyDef.angularDamping  = rb->angularDamping;
        bodyDef.isBullet        = false;
        bodyDef.motionLocks.angularX = rb->angularMotionLocks.x;
        bodyDef.motionLocks.angularY = rb->angularMotionLocks.y;
        bodyDef.motionLocks.angularZ = rb->angularMotionLocks.z;
        bodyDef.motionLocks.linearX = rb->linearMotionLocks.x;
        bodyDef.motionLocks.linearY = rb->linearMotionLocks.y;
        bodyDef.motionLocks.linearZ = rb->linearMotionLocks.z;

        switch (rb->type) {
            case RIGID_BODY_Static:    bodyDef.type = b3_staticBody;    break;
            case RIGID_BODY_Kinematic: bodyDef.type = b3_kinematicBody; break;
            case RIGID_BODY_Dynamic:   bodyDef.type = b3_dynamicBody;   break;
            default: continue;
        }

        b3BodyId bodyId = b3CreateBody(sys->world, &bodyDef);
        if (B3_IS_NULL(bodyId)) continue;
        b3ShapeId shapeId = create_collider(sys, bodyId, col, e);
        if (B3_IS_NULL(shapeId)) { b3DestroyBody(bodyId); continue; }

        if (!bodies_reserve(sys, sys->bodyCount + 1) || !entity_to_body_reserve(sys, e)) {
            b3DestroyBody(bodyId);
            continue;
        }

        uint32_t index = sys->bodyCount++;
        sys->bodies[index].entity   = e;
        sys->bodies[index].bodyId   = bodyId;
        sys->bodies[index].shapeId  = shapeId;
        sys->bodies[index].isStatic = (rb->type == RIGID_BODY_Static);
        sys->bodies[index].prevPos  = to_b3vec3(worldPos);
        sys->bodies[index].prevRot  = worldRot;
        sys->entityToBody[e] = index;
        t->worldOverrideActive = false;
        // Initial velocity was already baked into bodyDef above, so there's
        // nothing pending for the velocity-sync pass to push.
        rb->dirty = false;
    }
}

static void sync_movers(PhysicsSystem *sys, Ecs *w) {
    ECS_EACH(w, ECS_MASK(COMPONENT_Collider, COMPONENT_CharacterMover, COMPONENT_Transform), e) {
        if (find_mover_index(sys, e) >= 0) continue;
        Transform *t = ECS_GET(w, e, Transform);
        if (!movers_reserve(sys, sys->moverCount + 1) || !entity_to_mover_reserve(sys, e)) continue;

        uint32_t index = sys->moverCount++;
        sys->movers[index].entity = e;
        sys->movers[index].prevPos = to_b3vec3(t->position);
        sys->movers[index].currPos = to_b3vec3(t->position);
        sys->entityToMover[e] = index;
    }
}

static void physics_system_reap_and_sync(PhysicsSystem *sys, Ecs *w) {
    uint32_t writeIdx = 0;
    for (uint32_t i = 0; i < sys->bodyCount; i++) {
        PhysicsBodyEntry *entry = &sys->bodies[i];
        bool stillOwnsBody = ecs_entity_alive(w, entry->entity)
                           && ECS_HAS(w, entry->entity, RigidBody)
                           && ECS_HAS(w, entry->entity, Collider)
                           && ECS_HAS(w, entry->entity, Transform);
        if (!stillOwnsBody) {
            b3DestroyBody(entry->bodyId);
            if (entry->entity < sys->entityToBodyCapacity)
                sys->entityToBody[entry->entity] = PHYSICS_NO_BODY;
            continue;
        }

        if (writeIdx != i) {
            sys->bodies[writeIdx] = *entry;
            if (entry->entity < sys->entityToBodyCapacity)
                sys->entityToBody[entry->entity] = writeIdx;
        }
        PhysicsBodyEntry *dst = &sys->bodies[writeIdx++];
        if (dst->isStatic) continue;

        RigidBody *rb = ECS_GET(w, dst->entity, RigidBody);
        if (rb) {
            b3Vec3 lin = b3Body_GetLinearVelocity(dst->bodyId);
            b3Vec3 ang = b3Body_GetAngularVelocity(dst->bodyId);
            rb->linearVelocity[0] = lin.x; rb->linearVelocity[1] = lin.y; rb->linearVelocity[2] = lin.z;
            rb->angularVelocity[0] = ang.x; rb->angularVelocity[1] = ang.y; rb->angularVelocity[2] = ang.z;
        }
    }
    sys->bodyCount = writeIdx;

    uint32_t moverWriteIdx = 0;
    for (uint32_t i = 0; i < sys->moverCount; i++) {
        PhysicsMoverEntry *entry = &sys->movers[i];
        bool stillOwnsMover = ecs_entity_alive(w, entry->entity)
                            && ECS_HAS(w, entry->entity, CharacterMover)
                            && ECS_HAS(w, entry->entity, Collider)
                            && ECS_HAS(w, entry->entity, Transform);
        if (!stillOwnsMover) {
            if (entry->entity < sys->entityToMoverCapacity)
                sys->entityToMover[entry->entity] = PHYSICS_NO_MOVER;
            continue;
        }

        if (moverWriteIdx != i) {
            sys->movers[moverWriteIdx] = *entry;
            if (entry->entity < sys->entityToMoverCapacity)
                sys->entityToMover[entry->entity] = moverWriteIdx;
        }
        moverWriteIdx++;
    }
    sys->moverCount = moverWriteIdx;
}

static void physics_system_interpolate(PhysicsSystem *sys, Ecs *w, float alpha) {
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    for (uint32_t i = 0; i < sys->bodyCount; i++) {
        PhysicsBodyEntry *entry = &sys->bodies[i];
        if (entry->isStatic) continue;

        Transform *t = ECS_GET(w, entry->entity, Transform);
        if (!t) continue;

        b3WorldTransform xf = b3Body_GetTransform(entry->bodyId);
        b3Vec3 blendedPos = {
            entry->prevPos.x + (xf.p.x - entry->prevPos.x) * alpha,
            entry->prevPos.y + (xf.p.y - entry->prevPos.y) * alpha,
            entry->prevPos.z + (xf.p.z - entry->prevPos.z) * alpha
        };
        versor fromQ, toQ, blendedQ;
        b3quat_to_versor(entry->prevRot, fromQ);
        b3quat_to_versor(xf.q, toQ);
        glm_quat_slerp(fromQ, toQ, alpha, blendedQ);
        mat4 bodyWorld;
        glm_quat_mat4(blendedQ, bodyWorld);
        bodyWorld[3][0] = (float)blendedPos.x;
        bodyWorld[3][1] = (float)blendedPos.y;
        bodyWorld[3][2] = (float)blendedPos.z;
        t->position[0] = bodyWorld[3][0];
        t->position[1] = bodyWorld[3][1];
        t->position[2] = bodyWorld[3][2];
        mat4_to_euler_yxz(bodyWorld, t->rotation);
        glm_mat4_copy(bodyWorld, t->worldOverride);
        t->worldOverrideActive = true;
    }

    for (uint32_t i = 0; i < sys->moverCount; i++) {
        PhysicsMoverEntry *entry = &sys->movers[i];
        Transform *t = ECS_GET(w, entry->entity, Transform);
        if (!t) continue;

        t->position[0] = entry->prevPos.x + (entry->currPos.x - entry->prevPos.x) * alpha;
        t->position[1] = entry->prevPos.y + (entry->currPos.y - entry->prevPos.y) * alpha;
        t->position[2] = entry->prevPos.z + (entry->currPos.z - entry->prevPos.z) * alpha;
        t->worldOverrideActive = false;
    }
}

static Entity entity_from_shape(b3ShapeId shapeId) {
    return userdata_to_entity(b3Shape_GetUserData(shapeId));
}

bool physics_system_raycast(PhysicsSystem *sys, Ecs *w, vec3 origin, vec3 translation, uint64_t categoryMask, PhysicsRaycastHit *out) {
    memset(out, 0, sizeof(*out));
    if (!sys || !w || B3_IS_NULL(sys->world)) return false;

    b3QueryFilter filter;
    filter.categoryBits = UINT64_MAX;
    filter.maskBits      = categoryMask;

    b3RayResult result = b3World_CastRayClosest(sys->world, to_b3pos(origin), to_b3vec3(translation), filter);
    if (!result.hit || !b3Shape_IsValid(result.shapeId)) return false;

    Entity hitEntity = entity_from_shape(result.shapeId);
    if (!ecs_entity_alive(w, hitEntity)) return false;

    out->hit      = true;
    out->entity   = hitEntity;
    out->fraction = result.fraction;
    out->point[0] = (float)result.point.x;
    out->point[1] = (float)result.point.y;
    out->point[2] = (float)result.point.z;
    out->normal[0] = result.normal.x;
    out->normal[1] = result.normal.y;
    out->normal[2] = result.normal.z;
    return true;
}

typedef struct {
    b3CollisionPlane planes[PHYSICS_MOVER_MAX_PLANES];
    int              count;
} PhysicsMoverPlaneContext;

static bool mover_capsule_from_collider(const Collider *collider, const vec3 origin, b3Capsule *out) {
    float radius, height;
    switch (collider->type) {
        case COLLIDER_Capsule:
            radius = collider->capsule.radius;
            height = collider->capsule.height;
            break;
        case COLLIDER_Sphere:
            radius = collider->sphere.radius;
            height = 2.0f * radius;
            break;
        default:
            return false;
    }

    float innerHeight = height - 2.0f * radius;
    if (innerHeight < 0.0f) innerHeight = 0.0f;

    float baseX = origin[0] + collider->offset[0];
    float baseY = origin[1] + collider->offset[1];
    float baseZ = origin[2] + collider->offset[2];

    out->center1 = (b3Vec3){baseX, baseY - innerHeight * 0.5f, baseZ};
    out->center2 = (b3Vec3){baseX, baseY + innerHeight * 0.5f, baseZ};
    out->radius  = radius;
    return true;
}

static bool physics_system_mover_plane_cb(b3ShapeId shapeId, const b3PlaneResult *plane, int planeCount, void *context) {
    (void)planeCount;
    PhysicsMoverPlaneContext *ctx = context;
    if (ctx->count >= PHYSICS_MOVER_MAX_PLANES) return false;

    b3CollisionPlane *cp = &ctx->planes[ctx->count++];
    cp->plane = plane->plane;
    cp->plane.offset += PHYSICS_MOVER_SKIN;
    cp->pushLimit    = FLT_MAX;
    cp->push         = 0.0f;
    cp->clipVelocity = true;
    return ctx->count < PHYSICS_MOVER_MAX_PLANES;
}

static void physics_system_move_mover(PhysicsSystem *sys, Ecs *w, const Collider *collider, vec3 origin, vec3 velocity, float dt, uint64_t categoryMask) {
    (void)w;
    if (!sys || B3_IS_NULL(sys->world) || !collider || dt <= 0.0f) return;

    b3Capsule capsule;
    if (!mover_capsule_from_collider(collider, origin, &capsule)) return;

    b3QueryFilter filter;
    filter.categoryBits = UINT64_MAX;
    filter.maskBits      = categoryMask;

    b3Vec3 desiredTranslation = b3MulSV(dt, to_b3vec3(velocity));
    float fraction = b3World_CastMover(sys->world, b3Pos_zero, &capsule, desiredTranslation, filter, NULL, NULL);
    b3Vec3 safeDelta = b3MulSV(fraction, desiredTranslation);
    capsule.center1 = b3Add(capsule.center1, safeDelta);
    capsule.center2 = b3Add(capsule.center2, safeDelta);

    b3Vec3 totalDelta = safeDelta;
    b3Vec3 finalVelocity = to_b3vec3(velocity);
    PhysicsMoverPlaneContext planeCtx;

    for (int i = 0; i < PHYSICS_MOVER_SUBITERATIONS; ++i) {
        planeCtx.count = 0;
        b3World_CollideMover(sys->world, b3Pos_zero, &capsule, filter, physics_system_mover_plane_cb, &planeCtx);
        if (planeCtx.count == 0) break;

        b3PlaneSolverResult solved = b3SolvePlanes(b3Vec3_zero, planeCtx.planes, planeCtx.count);
        finalVelocity = b3ClipVector(finalVelocity, planeCtx.planes, planeCtx.count);

        float pushFraction = b3World_CastMover(sys->world, b3Pos_zero, &capsule, solved.delta, filter, NULL, NULL);
        b3Vec3 safePush = b3MulSV(pushFraction, solved.delta);

        capsule.center1 = b3Add(capsule.center1, safePush);
        capsule.center2 = b3Add(capsule.center2, safePush);
        totalDelta = b3Add(totalDelta, safePush);
    }

    velocity[0] = finalVelocity.x;
    velocity[1] = finalVelocity.y;
    velocity[2] = finalVelocity.z;

    origin[0] += totalDelta.x;
    origin[1] += totalDelta.y;
    origin[2] += totalDelta.z;
}

void physics_system_init(PhysicsSystem *sys) {
    memset(sys, 0, sizeof(PhysicsSystem));
    sys->subStepCount = 4;

    b3WorldDef def = b3DefaultWorldDef();
    sys->world = b3CreateWorld(&def);
    if (B3_IS_NULL(sys->world)) {
        return;
    }
    sys->gravity[0] = def.gravity.x;
    sys->gravity[1] = def.gravity.y;
    sys->gravity[2] = def.gravity.z;
    return;
}

void physics_system_free(PhysicsSystem *sys) {
    if (B3_IS_NON_NULL(sys->world)) {
        b3DestroyWorld(sys->world);
    }
    free(sys->bodies);
    free(sys->entityToBody);
    free(sys->movers);
    free(sys->entityToMover);
    memset(sys, 0, sizeof(PhysicsSystem));
}

static void physics_system_type_fixed_update(void* data, SystemManager* mgr, float fixed_dt) {
    PhysicsSystem *sys = data;
    Ecs *w = mgr->ecs;
    if (!sys || !w || fixed_dt < 0.0f) return;
    if (B3_IS_NULL(sys->world)) return;

    apply_dirty_transforms(sys, w);
    spawn_new_bodies(sys, w);
    sync_movers(sys, w);

    ECS_EACH(w, ECS_MASK(COMPONENT_RigidBody), e) {
        RigidBody *rb = ECS_GET(w, e, RigidBody);
        if (!rb || rb->type == RIGID_BODY_Static) continue;
        if (!rb->dirty) continue;
        int32_t bodyIndex = find_body_index(sys, e);
        if (bodyIndex < 0) continue;
        b3BodyId bodyId = sys->bodies[bodyIndex].bodyId;
        b3Body_SetLinearVelocity(bodyId, to_b3vec3(rb->linearVelocity));
        b3Body_SetAngularVelocity(bodyId, to_b3vec3(rb->angularVelocity));
        rb->dirty = false;
    }

    for (uint32_t i = 0; i < sys->bodyCount; i++) {
        PhysicsBodyEntry *entry = &sys->bodies[i];
        if (entry->isStatic) continue;
        b3WorldTransform xf = b3Body_GetTransform(entry->bodyId);
        entry->prevPos = xf.p;
        entry->prevRot = xf.q;
    }

    for (uint32_t i = 0; i < sys->moverCount; i++) {
        PhysicsMoverEntry *entry = &sys->movers[i];
        entry->prevPos = entry->currPos;
    }

    ECS_EACH(w, ECS_MASK(COMPONENT_Collider, COMPONENT_CharacterMover, COMPONENT_Transform), e) {
        Collider* col = ECS_GET(w, e, Collider);
        Transform* trans = ECS_GET(w, e, Transform);
        CharacterMover* mover = ECS_GET(w, e, CharacterMover);
        physics_system_move_mover(sys, w, col, trans->position, mover->velocity, fixed_dt, col->maskBits);
        mover->dirty = false;
    }

    for (uint32_t i = 0; i < sys->moverCount; i++) {
        PhysicsMoverEntry *entry = &sys->movers[i];
        Transform *t = ECS_GET(w, entry->entity, Transform);
        if (t) {
            entry->currPos = to_b3vec3(t->position);
        }
    }

    b3World_Step(sys->world, fixed_dt, sys->subStepCount);

    physics_system_reap_and_sync(sys, w);
}

static void physics_system_type_free(void *data) {
    physics_system_free(data);
    free(data);
}

static void physics_system_type_update(void *data, SystemManager *mgr, float dt, float alpha) {
    (void)dt;
    physics_system_interpolate(data, mgr->ecs, alpha);
}

bool physics_system_type_init(SystemManager *mgr) {
    PhysicsSystem *sys = calloc(1, sizeof(PhysicsSystem));
    if (!sys) return false;
    physics_system_init(sys);
    system_type_register(mgr, SYSTEM_TYPE_Physics, sys, physics_system_type_free, physics_system_type_update, physics_system_type_fixed_update);
    return true;
}