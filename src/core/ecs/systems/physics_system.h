#ifndef PHYSICS_SYSTEM_H
#define PHYSICS_SYSTEM_H

#include "../ecs.h"
#include "../system.h"
#include "../components.h"
#include <box3d/box3d.h>
#include <stdint.h>

typedef struct {
    Entity   entity;
    b3BodyId bodyId;
    bool     isStatic;

    b3Vec3 prevPos;
    b3Quat prevRot;
} PhysicsBodyEntry;

typedef struct {
    bool   hit;
    Entity entity;
    vec3   point;
    vec3   normal;
    float  fraction;
} PhysicsRaycastHit;

typedef struct PhysicsSystem {
    b3WorldId world;

    PhysicsBodyEntry *bodies;
    uint32_t          bodyCount;
    uint32_t          bodyCapacity;
    int subStepCount;

    float fixedDt;
    float accumulator;
    float gravity; 
} PhysicsSystem;

void physics_system_init(PhysicsSystem *sys);
void physics_system_free(PhysicsSystem *sys);
void physics_system_update(PhysicsSystem *sys, Ecs *world, float dt);
bool physics_system_type_init(SystemManager *mgr);

bool physics_system_raycast(PhysicsSystem *sys, Ecs *w, vec3 origin, vec3 translation, uint64_t categoryMask, PhysicsRaycastHit *out);

#endif