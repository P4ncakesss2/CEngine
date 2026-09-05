#include "app.h"
#include <stdio.h>
#include "cvar.h"

DEFINE_CVAR_INT(sim_maxFixedStepsPerFrame, "sim_maxFixedStepsPerFrame", 5)
DEFINE_CVAR_FLOAT(sim_fixedUpdateRate, "sim_fixedUpdateRate", 60.0f);
DEFINE_CVAR_INT(r_vsync, "r_vsync", 1);
DEFINE_CVAR_INT(r_msaa,  "r_msaa",  4);
DEFINE_CVAR_STRING(r_windowTitle, "r_windowTitle", "CEngine Game");

int app_init(EngineApp* app, const AppConfig* config)
{
    ContextCreateInfo ctx_info = {
        .appName = config->appName,
        .validationEnabled = config->validationEnabled,
    };
    
    GraphicsResult ctx_result = context_init(&app->context, &ctx_info);
    if (ctx_result.err != GRAPHICS_OK) {
        fprintf(stderr, "Failed to initialize Vulkan context: %s (VkResult: %s)\n", graphics_err_str(ctx_result.err), vk_result_str(ctx_result.vk));
        return 1;
    }
    
    cvar_set("r_vsync", config->windowVsync ? "1" : "0");
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)config->windowMSAA);
        cvar_set("r_msaa", buf);
    }
    if (config->windowTitle) cvar_set("r_windowTitle", config->windowTitle);

    WindowCreateInfo win_info = {
        .height = config->windowHeight,
        .width = config->windowWidth,
        .title = r_windowTitle.value.s,
        .vsync = r_vsync.value.i != 0,
        .msaa = (VkSampleCountFlagBits)r_msaa.value.i,
    };
    
    GraphicsResult win_result = window_create(&app->context, &app->window, &win_info);
    if (win_result.err != GRAPHICS_OK) {
        fprintf(stderr, "Failed to create window: %s (VkResult: %s)\n", graphics_err_str(win_result.err), vk_result_str(win_result.vk));
        goto err_context;
    }

    VfsResult vfs_result = vfs_init(&app->vfs);
    if (vfs_result != VFS_OK) {
        fprintf(stderr, "Failed to initialize vfs: %s\n", vfs_result_str(vfs_result));
        goto err_window;
    }
    
    vfs_result = vfs_mount_pak(&app->vfs, config->initialPakPath, NULL);
    if (vfs_result != VFS_OK) {
        fprintf(stderr, "Failed to mount game pak (%s): %s\n", config->initialPakPath, vfs_result_str(vfs_result));
        goto err_vfs;
    }

    AssetResult asset_result = asset_manager_init(&app->assets, &app->vfs);
    if (asset_result != ASSET_OK) {
        fprintf(stderr, "Failed to initialize asset manager: %s\n", asset_result_str(asset_result));
        goto err_vfs;
    }

    GraphicsResult ren_result = renderer_init(&app->renderer, &app->context, &app->window, &app->assets, &app->ecs, config->imgui_draw_callback, config->imgui_userdata);
    if (ren_result.err != GRAPHICS_OK) {
        fprintf(stderr, "Failed to create renderer: %s (VkResult: %s)\n", graphics_err_str(ren_result.err), vk_result_str(ren_result.vk));
        goto err_assets;
    }

    EcsResult ecs_result = ecs_init(&app->ecs);
    if (ecs_result != ECS_OK) {
        fprintf(stderr, "Failed to initialize ecs: %s\n", ecs_result_str(ecs_result));
        goto err_renderer;
    }

    EcsResult scene_result = scene_manager_init(&app->scenes, &app->ecs, &app->vfs, &app->assets, &app->renderer, config->initialScenePath);
    if (scene_result != ECS_OK) {
        fprintf(stderr, "Failed to load initial base scene (%s): %s\n", config->initialScenePath, ecs_result_str(scene_result));
        goto err_ecs;
    }
    
    if (!system_manager_init(&app->systems, &app->ecs, &app->renderer, &app->assets, &app->window)) {
        fprintf(stderr, "Failed to initialize systems\n");
        goto err_scene;
    }

    app->lastTime = (float)window_get_time();
    app->fixedAccumulator = 0.0f;
    app->fixedDeltaTime = sim_fixedUpdateRate.value.f > 0.0f ? 1.0f / sim_fixedUpdateRate.value.f  : 1.0f / 60.0f;

    return 0; 

err_scene:
    scene_manager_free(&app->scenes);
err_ecs:
    ecs_free(&app->ecs);
err_renderer:
    renderer_free(&app->renderer);
err_assets:
    asset_manager_free(&app->assets);
err_vfs:
    vfs_free(&app->vfs);
err_window:
    window_free(&app->context, &app->window);
err_context:
    context_free(&app->context);

    return 1;
}

void app_shutdown(EngineApp* app)
{
    system_manager_free(&app->systems);
    scene_manager_free(&app->scenes);
    ecs_free(&app->ecs);
    renderer_free(&app->renderer);
    asset_manager_free(&app->assets);
    vfs_free(&app->vfs);
    window_free(&app->context, &app->window);
    context_free(&app->context);
}

bool app_should_close(EngineApp* app)
{
    return window_should_close(&app->window);
}

static void app_sync_render_cvars(EngineApp *app)
{
    bool wantVsync = r_vsync.value.i != 0;
    if (wantVsync != app->window.vsync) {
        GraphicsResult r = window_set_vsync(&app->context, &app->window, wantVsync);
        if (r.err != GRAPHICS_OK) {
            fprintf(stderr, "r_vsync: failed to apply: %s\n", graphics_err_str(r.err));
        }
    }

    VkSampleCountFlagBits wantMsaa = (VkSampleCountFlagBits)r_msaa.value.i;
    if (wantMsaa != app->window.msaa) {
        GraphicsResult r = window_set_msaa(&app->context, &app->window, wantMsaa);
        if (r.err != GRAPHICS_OK) {
            fprintf(stderr, "r_msaa: failed to apply: %s\n", graphics_err_str(r.err));
        }
    }

    if (strcmp(r_windowTitle.value.s, app->window.title) != 0) {
        window_set_title(&app->window, r_windowTitle.value.s);
    }
}

void app_update(EngineApp* app)
{
    window_poll_events(&app->window);

    app->currentTime = (float)window_get_time();
    app->deltaTime = app->currentTime - app->lastTime;
    app->lastTime = app->currentTime;
    app->fixedDeltaTime = sim_fixedUpdateRate.value.f > 0.0f ? 1.0f / sim_fixedUpdateRate.value.f  : 1.0f / 60.0f;

    if (app->deltaTime > 0.25f)
        app->deltaTime = 0.25f;

    app->fps = app->deltaTime > 0.0f ? 1.0f / app->deltaTime : app->fps;
    app->frameTimeMs = app->deltaTime * 1000.0f;

    app_sync_render_cvars(app);

    EcsResult frame_scene_result = scene_manager_update(&app->scenes);
    if (frame_scene_result != ECS_OK) {
        fprintf(stderr, "scene_manager: base scene transition failed: %s\n", ecs_result_str(frame_scene_result));
    }

    app->fixedAccumulator += app->deltaTime;

    int steps = 0;
    while (app->fixedAccumulator >= app->fixedDeltaTime && steps < sim_maxFixedStepsPerFrame.value.i) {
        system_manager_fixed_update(&app->systems, app->fixedDeltaTime);
        app->fixedAccumulator -= app->fixedDeltaTime;
        steps++;
    }
    if (steps == sim_maxFixedStepsPerFrame.value.i) {
        app->fixedAccumulator = 0.0f;
    }

    float alpha = app->fixedAccumulator / app->fixedDeltaTime;
    system_manager_update(&app->systems, app->deltaTime, alpha);
}