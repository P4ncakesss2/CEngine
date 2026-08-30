#include "app.h"
#include <stdio.h>

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

    WindowCreateInfo win_info = {
        .height = config->windowHeight,
        .width = config->windowWidth,
        .title = config->windowTitle,
        .vsync = config->windowVsync,
        .msaa = (VkSampleCountFlagBits)config->windowMSAA,
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

    GraphicsResult ren_result = renderer_init(&app->renderer, &app->context, &app->window, &app->assets, &app->ecs, config->renderTarget, config->imgui_draw_callback, config->imgui_userdata);
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

void app_update(EngineApp* app)
{
    window_poll_events(&app->window);
    app->currentTime = (float)window_get_time();
    app->deltaTime = app->currentTime - app->lastTime;
    app->lastTime = app->currentTime;

    app->fps = app->deltaTime > 0.0f ? 1.0f / app->deltaTime : app->fps;
    app->frameTimeMs = app->deltaTime * 1000.0f;

    EcsResult frame_scene_result = scene_manager_update(&app->scenes);
    if (frame_scene_result != ECS_OK) {
        fprintf(stderr, "scene_manager: base scene transition failed: %s\n", ecs_result_str(frame_scene_result));
    }

    system_manager_update(&app->systems, app->deltaTime);
}