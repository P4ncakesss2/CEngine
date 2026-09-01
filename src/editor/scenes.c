#include "scenes.h"
#include "cglm/types.h"
#include "cglm/vec3.h"
#include "ecs/components.h"
#include <cglm/cglm.h>
#include "asset/mesh.h"
#include "ecs/ecs.h"

static Entity spawn_node(Ecs *w, const char *name, vec3 pos, vec3 rot, vec3 scale)
{
	Entity e;
	ecs_entity_create(w, &e);

	Transform t = {0};
	glm_vec3_copy(pos, t.position);
	glm_vec3_copy(rot, t.rotation);
	glm_vec3_copy(scale, t.scale);
	glm_mat4_identity(t.matrix);
	ECS_ADD(w, e, Transform, t);

	Name n = {.text = ecs_string_intern(w, name)};
	ECS_ADD(w, e, Name, n);

	return e;
}

static Entity spawn_dynamic_box(Ecs *w, const char *name, vec3 pos, vec3 rot, vec3 halfExtents,
                                 float density, vec3 initialVelocity)
{
	vec3 scale = {1.0f, 1.0f, 1.0f};
	Entity box = spawn_node(w, name, pos, rot, scale);

	Mesh boxMesh = {0};
	asset_ref_set(w, &boxMesh.meshRef, MESH_PROC_CUBE);
	Material boxMaterial = {0};
	asset_ref_set(w, &boxMaterial.albedoRef, "crate_pt_1.ctex");

	ECS_ADD(w, box, Material, boxMaterial);
	ECS_ADD(w, box, Mesh, boxMesh);

	RigidBody boxBody = {0};
	boxBody.type = RIGID_BODY_Dynamic;
	boxBody.gravityScale = 1.0f;
	glm_vec3_copy(initialVelocity, boxBody.linearVelocity);
	ECS_ADD(w, box, RigidBody, boxBody);

	Collider boxCollider = {0};
	boxCollider.type = COLLIDER_Box;
	glm_vec3_copy(halfExtents, boxCollider.box.halfExtents);
	boxCollider.density = density;
	boxCollider.friction = 0.6f;
	boxCollider.restitution = 0.1f;
	boxCollider.categoryBits = 1; 
    boxCollider.maskBits = 1;
	ECS_ADD(w, box, Collider, boxCollider);

	return box;
}

static void spawn_flying_camera(Ecs *ecs) {
    Entity e;
    ecs_entity_create(ecs, &e);

    Transform t = { .scale = {1, 1, 1}, .position = {0,10,0} };
    glm_mat4_identity(t.matrix);
    ECS_ADD(ecs, e, Transform, t);

	Entity cameraEntity;
	ecs_entity_create(ecs, &cameraEntity);
	Transform t2 = { .scale = {1, 1, 1}, .position = {0,0.7,0} };
    glm_mat4_identity(t2.matrix);
    ECS_ADD(ecs, cameraEntity, Transform, t2);

    Camera cam = { 
        .active = true, 
        .fov = 90.0f, 
        .nearPlane = 0.025f, 
        .farPlane = 1000.0f, 
        .type = CAMERA_TYPE_Perspective 
    };
    ECS_ADD(ecs, cameraEntity, Camera, cam);

	Parent parent = {.entity = e};
	ECS_ADD(ecs, cameraEntity, Parent, parent);

	Mesh playerMesh = {0};
	asset_ref_set(ecs, &playerMesh.meshRef, MESH_PROC_CAPSULE);
	ECS_ADD(ecs, e, Mesh, playerMesh);
	
    PlayerController player = {
        .lookSensitivity = 0.005f,
        .maxGroundSpeed  = 7.0f,
        .maxAirSpeed     = 2.0f,
        .groundAccel     = 10.0f,
        .airAccel        = 20.0f, 
        .groundFriction  = 6.0f,
        .jumpForce       = 4.5f,
		.cameraEntity = cameraEntity,
    };
    ECS_ADD(ecs, e, PlayerController, player);

	CharacterMover mover = {0};
	ECS_ADD(ecs, e, CharacterMover, mover);

    Collider col = { .type = COLLIDER_Capsule };
    col.capsule.radius = 0.3f;
    col.capsule.height = 1.8f;
	col.mass = 75.0f;
    col.friction = 0.8f;
	col.categoryBits = 1;
    col.maskBits = 1;
    ECS_ADD(ecs, e, Collider, col);
}


void scene_build_level1(Ecs *w)
{
	vec3 origin = {0, 0, 0};
	vec3 camPos = {0, 2, 10};

	spawn_flying_camera(w);

	Entity ground = spawn_node(w, "Ground", origin, GLM_VEC3_ZERO, (vec3){500, 0.25, 500});
	Mesh groundMesh = {0};
	asset_ref_set(w, &groundMesh.meshRef, MESH_PROC_PLANE);
	Material groundMaterial = {0};
	asset_ref_set(w, &groundMaterial.albedoRef, "asphalt_old_pt_1.ctex");

	ECS_ADD(w, ground, Material, groundMaterial);
	ECS_ADD(w, ground, Mesh, groundMesh);

	RigidBody groundBody = {0};
	groundBody.type = RIGID_BODY_Static;
	ECS_ADD(w, ground, RigidBody, groundBody);

	Collider groundCollider = {0};
	groundCollider.type = COLLIDER_Box;
	groundCollider.categoryBits = 1;
    groundCollider.maskBits = 1;
	glm_vec3_copy((vec3){500.0f, 1.0f, 500.0f}, groundCollider.box.halfExtents);
	groundCollider.offset[1] = -1.0f;
	groundCollider.friction = 0.8f;
	ECS_ADD(w, ground, Collider, groundCollider);

	vec3 boxHalfExtents = {0.5f, 0.5f, 0.5f};
	const float boxSize = boxHalfExtents[0] * 2.0f;
	const float boxSpacing = boxSize * 1.02f;

	const int baseWidth = 5;
	const float groundTopY = 0.25f; 

	char nameBuf[32];
	int boxIndex = 0;

	for (int row = 0; row < baseWidth; row++) {
		int boxesInRow = baseWidth - row;
		float py = groundTopY + boxHalfExtents[1] + row * boxSpacing;

		for (int col = 0; col < boxesInRow; col++) {
			float px = (col - (boxesInRow - 1) * 0.5f) * boxSpacing;
			vec3 pos = {px, py, 0.0f};

			snprintf(nameBuf, sizeof(nameBuf), "PyramidBox_%d", boxIndex);
			spawn_dynamic_box(w, nameBuf, pos, GLM_VEC3_ZERO, boxHalfExtents, 1.0f, GLM_VEC3_ZERO);

			boxIndex++;
		}
	}

	const int targetRow = 3;
	vec3 projectilePos = {
		0.0f,
		groundTopY + boxHalfExtents[1] + targetRow * boxSpacing,
		12.0f
	};
	vec3 projectileVelocity = {0.0f, 0.0f, -35.0f};
	spawn_dynamic_box(w, "Cannonball", projectilePos, GLM_VEC3_ZERO, boxHalfExtents, 15.0f, projectileVelocity);
}