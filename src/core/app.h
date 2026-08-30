#ifndef CENGINE_APP_H
#define CENGINE_APP_H

#include <stdbool.h>

#include "graphics/window.h"
#include "graphics/context.h"
#include "graphics/renderer.h"

#include "vfs/vfs.h"
#include "ecs/ecs.h"

#include "asset/asset.h"
#include "ecs/system.h"
#include "ecs/scene.h"

typedef enum {
    MSAA_NONE = 1,
    MSAA_2X   = 2,
    MSAA_4X   = 4,
    MSAA_8X   = 8,
    MSAA_16X  = 16,
    MSAA_32X  = 32,
    MSAA_64X  = 64,
} MSAASamples;

typedef struct EngineApp {
    SystemManager systems;
    SceneManager scenes;
    AssetManager assets;

    Renderer renderer;
    Context context;
    Window window;

    Vfs vfs;
    Ecs ecs;

    float lastTime;
    float currentTime;
    float deltaTime;
    float fps;
    float frameTimeMs;
} EngineApp;

typedef struct AppConfig {
    const char* appName;
    uint32_t windowWidth;
    uint32_t windowHeight;
    const char* windowTitle;
    MSAASamples windowMSAA;
    bool windowVsync;

    RenderTarget renderTarget;
    bool validationEnabled;
    void (*imgui_draw_callback)(void*);
    void* imgui_userdata;
    const char* initialScenePath;
    const char* initialPakPath;
} AppConfig;

int app_init(EngineApp* app, const AppConfig* config);
void app_shutdown(EngineApp* app);

bool app_should_close(EngineApp* app);
void app_update(EngineApp* app);

#endif