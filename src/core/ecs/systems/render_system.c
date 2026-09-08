#include "render_system.h"
#include "../components.h"
#include "camera_system.h"
#include "asset/mesh.h"
#include <stdlib.h>
#include <math.h>

static void direction_from_rotation(const vec3 rotation, vec3 outDir) {
    float pitch = rotation[0];
    float yaw   = rotation[1];
    outDir[0] = -sinf(yaw) * cosf(pitch);
    outDir[1] = -sinf(pitch);
    outDir[2] = -cosf(yaw) * cosf(pitch);
    glm_vec3_normalize(outDir);
}

void render_system_init(RenderSystem* sys) { (void)sys; }
void render_system_free(RenderSystem* sys) { (void)sys; }
#define MAX_SHADOW_CASTER_CANDIDATES 512

typedef struct {
    ObjectShadowCasterParams params;
    float distSq;
} ShadowCasterCandidate;

static uint32_t gather_object_shadow_casters(Ecs* ecs, Renderer* renderer, const CameraView* camera,
                                              ObjectShadowCasterParams* outCasters) {
    static ShadowCasterCandidate candidates[MAX_SHADOW_CASTER_CANDIDATES];
    uint32_t candidateCount = 0;

    vec3 camPos = { 0.0f, 0.0f, 0.0f };
    if (camera->valid) glm_vec3_copy(camera->position, camPos);

    ECS_EACH(ecs, ECS_MASK(COMPONENT_Mesh, COMPONENT_Transform, COMPONENT_ShadowCaster), e) {
        if (candidateCount >= MAX_SHADOW_CASTER_CANDIDATES) break;

        ShadowCaster* caster = ECS_GET(ecs, e, ShadowCaster);
        Mesh* meshComp = ECS_GET(ecs, e, Mesh);
        Transform* wt  = ECS_GET(ecs, e, Transform);

        ShadowCasterCandidate* cand = &candidates[candidateCount];
        cand->params.meshHandle = asset_ref_resolve(renderer->assets, renderer->ecs, ASSET_TYPE_Mesh, &meshComp->meshRef);
        glm_mat4_copy(wt->matrix, cand->params.model);
        cand->params.worldPos[0] = wt->matrix[3][0];
        cand->params.worldPos[1] = wt->matrix[3][1];
        cand->params.worldPos[2] = wt->matrix[3][2];

        MeshAsset* meshAsset = ASSET_GET(renderer->assets, cand->params.meshHandle, Mesh);
        if (meshAsset) {
            glm_vec3_copy(meshAsset->aabbMin, cand->params.aabbMin);
            glm_vec3_copy(meshAsset->aabbMax, cand->params.aabbMax);
        } else {
            glm_vec3_zero(cand->params.aabbMin);
            glm_vec3_zero(cand->params.aabbMax);
        }

        vec3 diff;
        glm_vec3_sub(cand->params.worldPos, camPos, diff);
        cand->distSq = glm_vec3_norm2(diff);

        candidateCount++;
    }

    for (uint32_t i = 1; i < candidateCount; i++) {
        ShadowCasterCandidate key = candidates[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && candidates[j].distSq > key.distSq) {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = key;
    }

    uint32_t outCount = candidateCount < MAX_OBJECT_SHADOW_CASTERS ? candidateCount : MAX_OBJECT_SHADOW_CASTERS;
    for (uint32_t i = 0; i < outCount; i++) {
        outCasters[i] = candidates[i].params;
    }
    return outCount;
}

void render_system_render(RenderSystem* sys, Ecs* ecs, Renderer* renderer, const CameraView* camera) {
    (void)sys;
    static RenderObject objects[MAX_FRAME_RENDER_OBJECTS];
    static MaterialObject materials[MAX_FRAME_RENDER_OBJECTS];
    uint32_t count = 0;

    ECS_EACH(ecs, ECS_MASK(COMPONENT_Mesh, COMPONENT_Transform), e) {
        if (count >= MAX_FRAME_RENDER_OBJECTS) break;

        Mesh* meshComp = ECS_GET(ecs, e, Mesh);
        Transform* wt = ECS_GET(ecs, e, Transform);

        RenderObject* obj = &objects[count];
        MaterialObject* mat = &materials[count];

        obj->meshHandle  = asset_ref_resolve(renderer->assets, renderer->ecs, ASSET_TYPE_Mesh, &meshComp->meshRef);
        obj->transparent = false;
        glm_mat4_copy(wt->matrix, obj->model);

        mat->albedoHandle = ASSET_INVALID_HANDLE;
        mat->samplerKind  = SAMPLER_Linear_repeat;
        mat->isTiled      = false;
        mat->tiling[0]    = 1.0f;
        mat->tiling[1]    = 1.0f;

        Material* material = ECS_GET(ecs, e, Material);
        if (material) {
            mat->albedoHandle = asset_ref_resolve(renderer->assets, renderer->ecs, ASSET_TYPE_Texture, &material->albedoRef);
            mat->samplerKind = material->sampler;
            obj->transparent  = material->isTransparent;
            mat->isTiled      = material->isTiled;
            if (material->isTiled) {
                mat->tiling[0] = material->tiling[0];
                mat->tiling[1] = material->tiling[1];
            }
        }

        count++;
    }

    SkyboxDrawParams skybox = {0};
    ECS_EACH(ecs, ECS_MASK(COMPONENT_Skybox), e) {
        Skybox* skyboxComp = ECS_GET(ecs, e, Skybox);
        skybox.hdriHandle = asset_ref_resolve(renderer->assets, renderer->ecs, ASSET_TYPE_Texture, &skyboxComp->hdriRef);
        skybox.samplerKind = SAMPLER_Linear_clamp;
        skybox.enabled = (skybox.hdriHandle != ASSET_INVALID_HANDLE);
        break;
    }

    DirectionalLightParams light = {0};
    ECS_EACH(ecs, ECS_MASK(COMPONENT_DirectionalLight, COMPONENT_Transform), e) {
        DirectionalLight* lightComp = ECS_GET(ecs, e, DirectionalLight);
        Transform* lt = ECS_GET(ecs, e, Transform);

        direction_from_rotation(lt->rotation, light.direction);
        glm_vec3_copy(lightComp->color, light.color);
        light.intensity = lightComp->intensity;
        light.enabled   = true;
        break;
    }

    static ObjectShadowCasterParams shadowCasters[MAX_OBJECT_SHADOW_CASTERS];
    uint32_t shadowCasterCount = gather_object_shadow_casters(ecs, renderer, camera, shadowCasters);

    renderer_draw_frame(renderer, objects, materials, count, camera, &skybox, &light, shadowCasters, shadowCasterCount);
}

void render_system_update(RenderSystem* sys, Ecs* ecs, CameraSystem* camera, Renderer* renderer) {
    CameraView camView;
    camera_system_get_view(camera, ecs, &camView);
    render_system_render(sys, ecs, renderer, &camView);
}
static void render_system_type_free(void *data) {
    render_system_free(data);
    free(data);
}

static void render_system_type_update(void *data, SystemManager *mgr, float dt, float alpha) {
    (void)dt;
    CameraSystem *camera = SYSTEM_GET(mgr, Camera);
    render_system_update(data, mgr->ecs, camera, mgr->renderer);
}

bool render_system_type_init(SystemManager *mgr) {
    RenderSystem *sys = calloc(1, sizeof(RenderSystem));
    if (!sys) return false;
    render_system_init(sys);
    system_type_register(mgr, SYSTEM_TYPE_Render, sys, render_system_type_free, render_system_type_update, NULL);
    return true;
}