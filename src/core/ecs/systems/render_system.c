#include "render_system.h"
#include "../components.h"
#include "camera_system.h"
#include <stdlib.h>

void render_system_init(RenderSystem* sys) { (void)sys; }
void render_system_free(RenderSystem* sys) { (void)sys; }

void render_system_render(RenderSystem* sys, Ecs* ecs, Renderer* renderer,
                           mat4 viewproj, vec3 camPos, bool camValid) {
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
        mat->isStochasticTiled = false;
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
                mat->isStochasticTiled = material->isStochasticTiled;
            }
        }

        count++;
    }

    renderer_draw_frame(renderer, objects, materials, count, viewproj, camPos, camValid);
}

void render_system_update(RenderSystem* sys, Ecs* ecs, CameraSystem* camera, Renderer* renderer) {
    mat4 viewproj;
    vec3 camPos;
    bool camValid = camera_system_get_view(camera, ecs, viewproj, camPos);
    render_system_render(sys, ecs, renderer, viewproj, camPos, camValid);
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