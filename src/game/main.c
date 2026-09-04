#include "app.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "ecs/ecs.h"
#include "ecs/components.h"
#include "cvar.h"

/* ---- console state ---- */

#define CONSOLE_INPUT_BUF_SIZE 256
#define CONSOLE_HISTORY_MAX    128

static bool s_consoleOpen = true;
static char s_inputBuf[CONSOLE_INPUT_BUF_SIZE] = {0};
static char s_history[CONSOLE_HISTORY_MAX][CONSOLE_INPUT_BUF_SIZE];
static int  s_historyCount = 0;

static void console_log(const char *fmt, ...) {
    if (s_historyCount >= CONSOLE_HISTORY_MAX) {
        memmove(s_history[0], s_history[1], sizeof(s_history) - sizeof(s_history[0]));
        s_historyCount = CONSOLE_HISTORY_MAX - 1;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_history[s_historyCount], CONSOLE_INPUT_BUF_SIZE, fmt, args);
    va_end(args);
    s_historyCount++;
}

static void console_print_cvar(Cvar *c) {
    switch (c->type) {
        case CVAR_INT:    console_log("%s = %d", c->name, c->value.i); break;
        case CVAR_FLOAT:  console_log("%s = %g", c->name, c->value.f); break;
        case CVAR_STRING: console_log("%s = \"%s\"", c->name, c->value.s); break;
    }
}

static void console_execute(const char *line) {
    if (line[0] == '\0') return;
    console_log("> %s", line);

    char name[64] = {0};
    char val[192] = {0};
    int n = sscanf(line, "%63s %191[^\n]", name, val);
    if (n <= 0) return;

    if (strcmp(name, "list") == 0) {
        for (Cvar *c = g_cvar_list; c; c = c->next)
            console_print_cvar(c);
        return;
    }

    Cvar *c = cvar_find(name);
    if (!c) {
        console_log("unknown cvar: %s", name);
        return;
    }

    if (n == 1) {
        console_print_cvar(c);
    } else if (cvar_set(name, val)) {
        console_print_cvar(c);
    } else {
        console_log("failed to set %s", name);
    }
}

static void console_draw(void) {
    if (!s_consoleOpen) return;

    igSetNextWindowSize((ImVec2){520, 400}, ImGuiCond_FirstUseEver);
    if (!igBegin("Console", &s_consoleOpen, ImGuiWindowFlags_None)) {
        igEnd();
        return;
    }

    ImVec2 footerSize = {0, -igGetFrameHeightWithSpacing()};
    igBeginChild_Str("ConsoleScroll", footerSize, true, ImGuiWindowFlags_None);
    for (int i = 0; i < s_historyCount; i++) {
        igTextUnformatted(s_history[i], NULL);
    }
    if (igGetScrollY() >= igGetScrollMaxY())
        igSetScrollHereY(1.0f);
    igEndChild();

    igSeparator();

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
    igSetNextItemWidth(-1);
    if (igInputText("##ConsoleInput", s_inputBuf, CONSOLE_INPUT_BUF_SIZE, flags, NULL, NULL)) {
        console_execute(s_inputBuf);
        s_inputBuf[0] = '\0';
        igSetKeyboardFocusHere(-1);
    }

    igEnd();
}

static void render_ui(void* user_data) {
    EngineApp* app = (EngineApp*)user_data;
    igBegin("Performance", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    igText("FPS: %.1f", app->fps);
    igText("Frame Time: %.2f ms", app->frameTimeMs);

    ECS_EACH(&app->ecs, ECS_MASK(COMPONENT_CharacterMover), e) {
        CharacterMover* player = ECS_GET(&app->ecs, e, CharacterMover);

        float horizontalSpeed = sqrtf(player->velocity[0] * player->velocity[0]
            + player->velocity[2] * player->velocity[2]);
        float totalSpeed = sqrtf(player->velocity[0] * player->velocity[0]
            + player->velocity[1] * player->velocity[1]
            + player->velocity[2] * player->velocity[2]);

        igSeparator();
        igText("Speed (horizontal): %.2f u/s", horizontalSpeed);
        igText("Speed (total): %.2f u/s", totalSpeed);
        igText("Velocity: (%.2f, %.2f, %.2f)",
            player->velocity[0], player->velocity[1], player->velocity[2]);

        break;
    }
    igEnd();

    console_draw();
}

int main(int argc, char **argv)
{
    EngineApp app;
    AppConfig config = {
        .appName = "CEngine Game",
        .windowWidth = 1280,
        .windowHeight = 720,
        .windowTitle = "CEngine Game",
        .windowVsync = true,
        .windowMSAA = MSAA_4X,
        .imgui_draw_callback = &render_ui,
        .imgui_userdata = &app,
        .validationEnabled = true,
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