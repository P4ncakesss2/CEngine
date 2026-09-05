#ifndef WINDOW_H
#define WINDOW_H

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <stdbool.h>
#include "vk_mem_alloc.h"
#include "context.h"
#include "graphics_error.h"
#include "image.h"
#include "../asset/texture.h"

#define MAX_FRAMES_IN_FLIGHT 2

typedef enum CursorMode {
    CURSOR_MODE_NORMAL,
    CURSOR_MODE_HIDDEN,
    CURSOR_MODE_DISABLED
} CursorMode;

typedef enum {
    KEY_UNKNOWN = 0,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_SPACE, KEY_ENTER, KEY_ESCAPE, KEY_TAB, KEY_BACKSPACE,
    KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT,
    KEY_LEFT_CTRL,  KEY_RIGHT_CTRL,
    KEY_LEFT_ALT,   KEY_RIGHT_ALT,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_GRAVE_ACCENT, 
    KEY_COUNT
} Key;

typedef enum {
    MOUSE_BUTTON_UNKNOWN = 0,

    MOUSE_BUTTON_LEFT,
    MOUSE_BUTTON_RIGHT,
    MOUSE_BUTTON_MIDDLE,
    MOUSE_BUTTON_4,
    MOUSE_BUTTON_5,

    MOUSE_BUTTON_COUNT
} MouseButton;

typedef struct InputState {
    bool keys[GLFW_KEY_LAST + 1];
    bool keysPrev[GLFW_KEY_LAST + 1];
    bool mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1];
    bool mouseButtonsPrev[GLFW_MOUSE_BUTTON_LAST + 1];
    double mouseX, mouseY;
    double mouseDeltaX, mouseDeltaY;
    double scrollX, scrollY;
#define INPUT_TEXT_MAX 32
    uint32_t textInput[INPUT_TEXT_MAX];
    int      textInputCount;
} InputState;

typedef struct FrameData {
    VkSemaphore     acquireSemaphore;
    VkFence         renderFence;
    VkCommandPool   graphicsPool;
    VkCommandBuffer graphicsCmd;
    VkCommandPool   computePool;
    VkCommandBuffer computeCmd;
} FrameData;

typedef struct ImageData {
    VkSemaphore renderSemaphore;
} ImageData;

typedef struct Window {
    GLFWwindow* handle;
    int width, height;
    int posx, posy;
    char* title;
    bool vsync;
    bool framebufferResized;
    bool minimized;

    int windowedWidth, windowedHeight;
    int windowedPosX, windowedPosY;

    InputState input;

    VkSurfaceKHR surface;
    VkExtent2D swapchainExtent;
    VkSwapchainKHR swapchain;
    VkImage* swapchainImages;
    uint32_t swapchainImagesCount;
    VkImageView* swapchainImageViews;
    VkSurfaceFormatKHR swapchainSurfaceFormat;

    ImageData* imageData;
    FrameData frameData[MAX_FRAMES_IN_FLIGHT];

    Image depthImage;
    Image colorImage;
    Image resolveImage;
    VkExtent2D renderExtent;

    VkSampleCountFlagBits msaa;
    CursorMode cursorMode;
} Window;

typedef struct WindowCreateInfo {
    int width, height;
    const char* title;
    bool vsync;
    VkSampleCountFlagBits msaa; 
} WindowCreateInfo;

GraphicsResult window_create(Context* ctx, Window* window, WindowCreateInfo* info);
void window_free(Context* ctx, Window* window);
bool window_should_close(Window* window);
void window_poll_events(Window* window);
void window_close(Window* window);
double window_get_time(void);

GraphicsResult window_recreate_swapchain(Context* ctx, Window* window);
GraphicsResult window_recreate_render_targets(Context* ctx, Window* window);
GraphicsResult window_set_vsync(Context* ctx, Window* window, bool vsync);
GraphicsResult window_set_msaa(Context* ctx, Window* window, VkSampleCountFlagBits samples);

bool window_key_down(const Window* window, Key key);
bool window_key_pressed(const Window* window, Key key);
bool window_key_released(const Window* window, Key key);
bool window_mouse_down(const Window* window, MouseButton button);
bool window_mouse_pressed(const Window* window, MouseButton button);
bool window_mouse_released(const Window* window, MouseButton button);
void window_get_mouse_pos(const Window* window, double* x, double* y);
void window_get_mouse_delta(const Window* window, double* dx, double* dy);
void window_get_scroll_delta(const Window* window, double* sx, double* sy);

void window_set_title(Window* window, const char* title);
void window_set_icon(Window* window, const TextureAsset* icon);
void window_set_cursor_mode(Window* window, CursorMode mode);
void window_show(Window* window);
void window_hide(Window* window);

const char* window_get_clipboard(Window* window);
void window_set_clipboard(Window* window, const char* text);

void window_set_accepts_gameplay_input(Window* window, bool accepts);
bool window_accepts_gameplay_input(const Window* window);

int window_context_init(void);
void window_context_free(void);
const char** window_get_vulkan_extensions(uint32_t* count);

#endif