#include "physics_system.h"
#include "ecs/components.h"
#include "ecs/ecs.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#define PHYSICS_FIXED_DT     (1.0f / 60.0f)
#define PHYSICS_MAX_SUBSTEPS 5

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

static b3Filter collider_filter(const Collider *col) {
    b3Filter filter;
    filter.categoryBits = col->categoryBits;
    filter.maskBits      = col->maskBits;
    filter.groupIndex    = col->groupIndex;
    return filter;
}

#define PHYSICS_PI 3.14159265358979323846f
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
    }
}

static void b3quat_to_versor(b3Quat q, versor out) {
    out[0] = q.v.x;
    out[1] = q.v.y;
    out[2] = q.v.z;
    out[3] = q.s;
}

static void writeback_and_reap(PhysicsSystem *sys, Ecs *w) {
    float alpha = sys->fixedDt > 0.0f ? sys->accumulator / sys->fixedDt : 0.0f;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

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
        Transform *t  = ECS_GET(w, dst->entity, Transform);
        if (t) {
            b3WorldTransform xf = b3Body_GetTransform(dst->bodyId);
            b3Vec3 blendedPos = {
                dst->prevPos.x + (xf.p.x - dst->prevPos.x) * alpha,
                dst->prevPos.y + (xf.p.y - dst->prevPos.y) * alpha,
                dst->prevPos.z + (xf.p.z - dst->prevPos.z) * alpha
            };
            versor fromQ, toQ, blendedQ;
            b3quat_to_versor(dst->prevRot, fromQ);
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
        if (rb) {
            b3Vec3 lin = b3Body_GetLinearVelocity(dst->bodyId);
            b3Vec3 ang = b3Body_GetAngularVelocity(dst->bodyId);
            rb->linearVelocity[0] = lin.x; rb->linearVelocity[1] = lin.y; rb->linearVelocity[2] = lin.z;
            rb->angularVelocity[0] = ang.x; rb->angularVelocity[1] = ang.y; rb->angularVelocity[2] = ang.z;
        }
    }
    sys->bodyCount = writeIdx;
}

void physics_system_init(PhysicsSystem *sys) {
    memset(sys, 0, sizeof(PhysicsSystem));
    sys->subStepCount = 4;
    sys->fixedDt = PHYSICS_FIXED_DT;

    b3WorldDef def = b3DefaultWorldDef();
    sys->world = b3CreateWorld(&def);
    if (B3_IS_NULL(sys->world)) {
        return;
    }
    sys->gravity = sqrtf(def.gravity.x * def.gravity.x
                        + def.gravity.y * def.gravity.y
                        + def.gravity.z * def.gravity.z);
    return;
}

void physics_system_free(PhysicsSystem *sys) {
    if (B3_IS_NON_NULL(sys->world)) {
        b3DestroyWorld(sys->world);
    }
    free(sys->bodies);
    free(sys->entityToBody);
    memset(sys, 0, sizeof(PhysicsSystem));
}


static Entity entity_from_shape(b3ShapeId shapeId) {
    return userdata_to_entity(b3Shape_GetUserData(shapeId));
}

#define PHYSICS_MOVER_MAX_PLANES     16
#define PHYSICS_MOVER_MAX_ITERATIONS 4
#define PHYSICS_MOVER_SETTLE_EPS_SQ  (1e-8f)

static bool body_id_equals(b3BodyId a, b3BodyId b) {
    return memcmp(&a, &b, sizeof(b3BodyId)) == 0;
}

typedef struct {
    b3CollisionPlane planes[PHYSICS_MOVER_MAX_PLANES];
    int count;
} MoverPlaneCollectContext;

static bool mover_plane_collect_cb(b3ShapeId shapeId, const b3PlaneResult *plane, int planeCount, void *context) {
    (void)planeCount;
    MoverPlaneCollectContext *ctx = context;
    if (ctx->count >= PHYSICS_MOVER_MAX_PLANES) return false;

    b3CollisionPlane *out = &ctx->planes[ctx->count++];
    out->plane        = plane->plane;
    out->pushLimit     = FLT_MAX;
    out->push          = 0.0f;
    out->clipVelocity  = true;
    return true;
}

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

    out->center1 = (b3Vec3){baseX, baseY + radius, baseZ};
    out->center2 = (b3Vec3){baseX, baseY + radius + innerHeight, baseZ};
    out->radius  = radius;
    return true;
}

static bool mover_depenetrate(Ecs *w, b3WorldId world, const Collider *collider, b3QueryFilter filter,
                               vec3 origin, b3Capsule *mover,
                               b3CollisionPlane *touchedPlanes, int *touchedCount,
                               b3Vec3 *moverVelocity, float moverMass, float moverFriction, float gravity,
                               float dt) {
    b3Pos zero = {0.0f, 0.0f, 0.0f};
    bool foundAny = false;

    for (int iter = 0; iter < PHYSICS_MOVER_MAX_ITERATIONS; iter++) {
        MoverPlaneCollectContext ctx = {0};
        b3World_CollideMover(world, zero, mover, filter, mover_plane_collect_cb, &ctx);
        if (ctx.count == 0) break;
        foundAny = true;

        b3PlaneSolverResult result = b3SolvePlanes((b3Vec3){0.0f, 0.0f, 0.0f}, ctx.planes, ctx.count);

        for (int i = 0; i < ctx.count && *touchedCount < PHYSICS_MOVER_MAX_PLANES; i++) {
            touchedPlanes[(*touchedCount)++] = ctx.planes[i];
        }

        origin[0] += result.delta.x;
        origin[1] += result.delta.y;
        origin[2] += result.delta.z;
        mover_capsule_from_collider(collider, origin, mover);

        float deltaLenSq = result.delta.x * result.delta.x
                          + result.delta.y * result.delta.y
                          + result.delta.z * result.delta.z;
        if (deltaLenSq < PHYSICS_MOVER_SETTLE_EPS_SQ) break;
    }

    return foundAny;
}

static void physics_system_move_mover(PhysicsSystem *sys, Ecs *w, const Collider *collider, vec3 origin, vec3 velocity, float dt, uint64_t categoryMask) {
    if (!sys || !collider || B3_IS_NULL(sys->world) || dt <= 0.0f) return;

    b3Capsule mover;
    if (!mover_capsule_from_collider(collider, origin, &mover)) return;

    b3QueryFilter filter;
    filter.categoryBits = UINT64_MAX;
    filter.maskBits      = categoryMask;

    b3Pos zero = {0.0f, 0.0f, 0.0f};
    b3Vec3 moverVelocity = to_b3vec3(velocity);
    float moverMass = collider->mass;
    float moverFriction = collider->friction;
    float gravity = sys->gravity;

    b3CollisionPlane touchedPlanes[PHYSICS_MOVER_MAX_PLANES];
    int touchedCount = 0;
    b3Vec3 translation = to_b3vec3((vec3){velocity[0] * dt, velocity[1] * dt, velocity[2] * dt});

    float fraction = b3World_CastMover(sys->world, zero, &mover, translation, filter, NULL, NULL);
    origin[0] += velocity[0] * dt * fraction;
    origin[1] += velocity[1] * dt * fraction;
    origin[2] += velocity[2] * dt * fraction;
    mover_capsule_from_collider(collider, origin, &mover);
    mover_depenetrate(w, sys->world, collider, filter, origin, &mover, touchedPlanes, &touchedCount, &moverVelocity, moverMass, moverFriction, gravity, dt);
    
    b3Vec3 clipped = touchedCount > 0 ? b3ClipVector(moverVelocity, touchedPlanes, touchedCount) : moverVelocity;
    velocity[0] = clipped.x;
    velocity[1] = clipped.y;
    velocity[2] = clipped.z;
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

void physics_system_update(PhysicsSystem *sys, Ecs *w, float dt) {
    if (!sys || !w || dt < 0.0f) return;
    if (B3_IS_NULL(sys->world)) return;

    spawn_new_bodies(sys, w);
    ECS_EACH(w, ECS_MASK(COMPONENT_RigidBody), e) {
        RigidBody *rb = ECS_GET(w, e, RigidBody);
        if (!rb || rb->type == RIGID_BODY_Static) continue;
        int32_t bodyIndex = find_body_index(sys, e);
        if (bodyIndex < 0) continue;
        b3BodyId bodyId = sys->bodies[bodyIndex].bodyId;
        b3Body_SetLinearVelocity(bodyId, to_b3vec3(rb->linearVelocity));
        b3Body_SetAngularVelocity(bodyId, to_b3vec3(rb->angularVelocity));
    }

    sys->accumulator += dt;
    float maxAccum = sys->fixedDt * PHYSICS_MAX_SUBSTEPS;
    if (sys->accumulator > maxAccum) {
        sys->accumulator = maxAccum;
    }

    int steps = 0;
    if (sys->accumulator >= sys->fixedDt) {
        for (uint32_t i = 0; i < sys->bodyCount; i++) {
            PhysicsBodyEntry *entry = &sys->bodies[i];
            if (entry->isStatic) continue;
            b3WorldTransform xf = b3Body_GetTransform(entry->bodyId);
            entry->prevPos = xf.p;
            entry->prevRot = xf.q;
        }
    }

    ECS_EACH(w, ECS_MASK(COMPONENT_Collider, COMPONENT_CharacterMover, COMPONENT_Transform), e) {
        Collider* col = ECS_GET(w, e, Collider);
        Transform* trans = ECS_GET(w, e, Transform);
        CharacterMover* mover = ECS_GET(w, e, CharacterMover);
        physics_system_move_mover(sys, w, col, trans->position, mover->velocity, dt, col->maskBits);
    }

    while (sys->accumulator >= sys->fixedDt && steps < PHYSICS_MAX_SUBSTEPS) {
        b3World_Step(sys->world, sys->fixedDt, sys->subStepCount);
        sys->accumulator -= sys->fixedDt;
        steps++;
    }

    writeback_and_reap(sys, w);
}

static void physics_system_type_free(void *data) {
    physics_system_free(data);
    free(data);
}

static void physics_system_type_update(void *data, SystemManager *mgr, float dt) {
    physics_system_update(data, mgr->ecs, dt);
}

bool physics_system_type_init(SystemManager *mgr) {
    PhysicsSystem *sys = calloc(1, sizeof(PhysicsSystem));
    if (!sys) return false;
    physics_system_init(sys);
    system_type_register(mgr, SYSTEM_TYPE_Physics, sys, physics_system_type_free, physics_system_type_update);
    return true;
}