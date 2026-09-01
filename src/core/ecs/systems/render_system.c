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
    uint32_t count = 0;

    ECS_EACH(ecs, ECS_MASK(COMPONENT_Mesh, COMPONENT_Transform), e) {
        if (count >= MAX_FRAME_RENDER_OBJECTS) break;

        Mesh* meshComp = ECS_GET(ecs, e, Mesh);
        Transform* wt = ECS_GET(ecs, e, Transform);

        RenderObject* obj = &objects[count];
        obj->meshHandle   = asset_ref_resolve(renderer->assets, renderer->ecs, ASSET_TYPE_Mesh, &meshComp->meshRef);
        obj->albedoHandle = ASSET_INVALID_HANDLE;
        obj->transparent  = false;
        glm_mat4_copy(wt->matrix, obj->model);

        Material* material = ECS_GET(ecs, e, Material);
        if (material) {
            obj->albedoHandle = asset_ref_resolve(renderer->assets, renderer->ecs, ASSET_TYPE_Texture, &material->albedoRef);
            obj->transparent  = material->isTransparent;
        }

        count++;
    }

    renderer_draw_frame(renderer, objects, count, viewproj, camPos, camValid);
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