#include "app.h"
#include <stdbool.h>
#include <stdio.h>
#include <math.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "ecs/ecs.h"
#include "ecs/components.h"

static void render_ui(void* user_data) {
    EngineApp* app = (EngineApp*)user_data;
    igBegin("Performance", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    igText("FPS: %.1f", app->fps);
    igText("Frame Time: %.2f ms", app->frameTimeMs);

    ECS_EACH(&app->ecs, ECS_MASK(COMPONENT_PlayerController), e) {
        PlayerController* player = ECS_GET(&app->ecs, e, PlayerController);

        float horizontalSpeed = sqrtf(player->currentVelocity[0] * player->currentVelocity[0]
                                     + player->currentVelocity[2] * player->currentVelocity[2]);
        float totalSpeed = sqrtf(player->currentVelocity[0] * player->currentVelocity[0]
                                + player->currentVelocity[1] * player->currentVelocity[1]
                                + player->currentVelocity[2] * player->currentVelocity[2]);

        igSeparator();
        igText("Speed (horizontal): %.2f u/s", horizontalSpeed);
        igText("Speed (total): %.2f u/s", totalSpeed);
        igText("Velocity: (%.2f, %.2f, %.2f)",
               player->currentVelocity[0], player->currentVelocity[1], player->currentVelocity[2]);

        break;
    }

    igEnd();
}

int main(int argc, char **argv)
{
    EngineApp app;
    AppConfig config = {
        .appName = "CEngine Game",
        .windowWidth = 1280,
        .windowHeight = 720,
        .windowTitle = "CEngine Game",
        .windowVsync = false,
        .windowMSAA = MSAA_4X,
        .renderTarget = RENDER_TARGET_SWAPCHAIN,
        .imgui_draw_callback = &render_ui,
        .imgui_userdata = &app,
        .validationEnabled = false,
        .initialPakPath = "game.pak",
        .initialScenePath = "scenes/level1.scn",
    };
    if (app_init(&app, &config) != 0) {
        return 1;
    }
    while (!app_should_close(&app)) {
        app_update(&app);
    }
    app_shutdown(&app);
    return 0;
}