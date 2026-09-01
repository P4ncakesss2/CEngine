#ifndef ECS_COMPONENTS_H
#define ECS_COMPONENTS_H

#include <cglm/cglm.h>
#include "stdbool.h"
#include "asset/asset.h"
#include <box3d/box3d.h>

typedef uint32_t Entity;

typedef struct {
    vec3 position;
    vec3 rotation;
    vec3 scale;
    mat4 matrix;

    bool worldOverrideActive;
    mat4 worldOverride;
} Transform;

typedef struct {
    uint32_t entity;
} Parent;

typedef struct {
    StringId text;
} Name;

typedef struct {
    AssetRef meshRef;
} Mesh;

typedef struct {
    AssetRef albedoRef;
    bool isTransparent;
} Material;

typedef enum {
    CAMERA_TYPE_Orthogonal,
    CAMERA_TYPE_Perspective,
} CamType;

typedef struct {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    CamType type;

    float fov;
    float aspect;
    float nearPlane;
    float farPlane;
    float orthoSize;
    bool active;
} Camera;

typedef struct {
    AssetRef sceneRef;
} Scene;

typedef struct {
    AssetRef faces[6];
} Skybox;

typedef enum {
    RIGID_BODY_Static,
    RIGID_BODY_Kinematic,
    RIGID_BODY_Dynamic,
} RigidBodyType;

typedef struct {
    bool x,y,z;
} MotionLocks;

typedef struct {
    RigidBodyType type;
    float gravityScale;
    float linearDamping;
    float angularDamping;

    MotionLocks angularMotionLocks;
    MotionLocks linearMotionLocks;

    vec3  linearVelocity;
    vec3  angularVelocity;

    b3BodyId bodyId;
    bool     created;
} RigidBody;

typedef enum {
    COLLIDER_Box,
    COLLIDER_Sphere,
    COLLIDER_Capsule,
} ColliderType;

typedef struct {
    ColliderType type;

    vec3 offset;

    float density;
    float mass;
    float friction;
    float restitution;

    bool isSensor;
    bool enableContactEvents;
    bool enableHitEvents;

    uint64_t categoryBits;
    uint64_t maskBits;
    int32_t  groupIndex;

    b3ShapeId shapeId;
    bool created;

    union {
        struct {
            vec3 halfExtents;
        } box;
        struct {
            float radius;
        } sphere;
        struct {
            float radius;
            float height;
        } capsule;
    };
} Collider;

typedef struct {
    float lookSensitivity;
    float pitch;
    float yaw;
    vec3 currentVelocity;
    float maxGroundSpeed;
    float maxAirSpeed;
    float groundAccel;
    float airAccel;
    float groundFriction;
    float jumpForce; 
    bool  wasGrounded;
} PlayerController;

#endif