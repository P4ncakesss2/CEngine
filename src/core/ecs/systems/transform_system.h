#ifndef TRANSFORM_SYSTEM_H
#define TRANSFORM_SYSTEM_H

#include "../ecs.h"
#include "../system.h"
#include <stdint.h>

typedef struct TransformSystem {
    uint8_t* state;
    uint32_t capacity;
} TransformSystem;

void transform_system_init(TransformSystem* sys);
void transform_system_free(TransformSystem* sys);
void transform_system_update(TransformSystem* sys, Ecs *world);
bool transform_system_type_init(SystemManager *mgr);

#endif