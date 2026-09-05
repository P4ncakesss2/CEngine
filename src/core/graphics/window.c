#include "window.h"
#include "context.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

static bool glfwInitialized = false;

static void framebuffer_size_callback(GLFWwindow* handle, int width, int height) {
    Window* window = (Window*)glfwGetWindowUserPointer(handle);
    if (window) {
        window->width = width;
        window->height = height;
        window->minimized = (width == 0 || height == 0);
        window->framebufferResized = true;
    }
}

static void key_callback(GLFWwindow* handle, int key, int scancode, int action, int mods) {
    (void)scancode; (void)mods;
    Window* window = (Window*)glfwGetWindowUserPointer(handle);
    if (window && key >= 0 && key <= GLFW_KEY_LAST) {
        if (action == GLFW_PRESS)   window->input.keys[key] = true;
        if (action == GLFW_RELEASE) window->input.keys[key] = false;
    }
}

static void mouse_button_callback(GLFWwindow* handle, int button, int action, int mods) {
    (void)mods;
    Window* window = (Window*)glfwGetWindowUserPointer(handle);
    if (window && button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST) {
        if (action == GLFW_PRESS)   window->input.mouseButtons[button] = true;
        if (action == GLFW_RELEASE) window->input.mouseButtons[button] = false;
    }
}

static void cursor_position_callback(GLFWwindow* handle, double xpos, double ypos) {
    Window* window = (Window*)glfwGetWindowUserPointer(handle);
    if (window) {
        window->input.mouseDeltaX += (xpos - window->input.mouseX);
        window->input.mouseDeltaY += (ypos - window->input.mouseY);
        window->input.mouseX = xpos;
        window->input.mouseY = ypos;
    }
}

static void scroll_callback(GLFWwindow* handle, double xoffset, double yoffset) {
    Window* window = (Window*)glfwGetWindowUserPointer(handle);
    if (window) {
        window->input.scrollX += xoffset;
        window->input.scrollY += yoffset;
    }
}

static void char_callback(GLFWwindow* handle, unsigned int codepoint) {
    Window* window = (Window*)glfwGetWindowUserPointer(handle);
    if (window && window->input.textInputCount < INPUT_TEXT_MAX) {
        window->input.textInput[window->input.textInputCount++] = codepoint;
    }
}

static GraphicsResult create_surface(Context* ctx, Window* window) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = glfwCreateWindowSurface(ctx->instance, window->handle, NULL, &surface);
    if (result != VK_SUCCESS) {
        window_free(ctx, window);
        return (GraphicsResult){ GRAPHICS_ERR_SURFACE_CREATION_FAILED, result };
    }
    window->surface = surface;
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR* capabilities, GLFWwindow* window) {
    if (capabilities->currentExtent.width != UINT32_MAX)
        return capabilities->currentExtent;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = { (uint32_t)width, (uint32_t)height };
    if (actualExtent.width < capabilities->minImageExtent.width)
        actualExtent.width = capabilities->minImageExtent.width;
    else if (actualExtent.width > capabilities->maxImageExtent.width)
        actualExtent.width = capabilities->maxImageExtent.width;

    if (actualExtent.height < capabilities->minImageExtent.height)
        actualExtent.height = capabilities->minImageExtent.height;
    else if (actualExtent.height > capabilities->maxImageExtent.height)
        actualExtent.height = capabilities->maxImageExtent.height;

    return actualExtent;
}

static uint32_t choose_swap_min_image_count(const VkSurfaceCapabilitiesKHR* surfaceCapabilities) {
    uint32_t minImageCount = surfaceCapabilities->minImageCount;
    if ((surfaceCapabilities->maxImageCount > 0) && (surfaceCapabilities->maxImageCount < minImageCount))
        minImageCount = surfaceCapabilities->maxImageCount;
    return minImageCount;
}

static VkSurfaceFormatKHR choose_swap_surface_format(const VkSurfaceFormatKHR* availableFormats, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (availableFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormats[i];
        }
    }
    return availableFormats[0];
}

static VkPresentModeKHR choose_swap_present_mode(const VkPresentModeKHR* availablePresentModes, uint32_t count, bool vsync) {
    if (vsync)
        return VK_PRESENT_MODE_FIFO_KHR;

    for (uint32_t i = 0; i < count; i++) {
        if (availablePresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (availablePresentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static GraphicsResult create_swapchain(Context* ctx, Window* window, VkSwapchainKHR oldSwapchain) {
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physicalDevice, window->surface, &surfaceCapabilities);

    window->swapchainExtent = choose_swap_extent(&surfaceCapabilities, window->handle);
    uint32_t minImageCount = choose_swap_min_image_count(&surfaceCapabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physicalDevice, window->surface, &formatCount, NULL);
    VkSurfaceFormatKHR* availableFormats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
    if (!availableFormats) {
        return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physicalDevice, window->surface, &formatCount, availableFormats);
    window->swapchainSurfaceFormat = choose_swap_surface_format(availableFormats, formatCount);
    free(availableFormats);

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physicalDevice, window->surface, &presentModeCount, NULL);
    VkPresentModeKHR* availablePresentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
    if (!availablePresentModes) {
        return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physicalDevice, window->surface, &presentModeCount, availablePresentModes);
    VkPresentModeKHR presentMode = choose_swap_present_mode(availablePresentModes, presentModeCount, window->vsync);
    free(availablePresentModes);

    VkSwapchainCreateInfoKHR swapChainCreateInfo = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext            = NULL,
        .flags            = 0,
        .surface          = window->surface,
        .minImageCount    = minImageCount,
        .imageFormat      = window->swapchainSurfaceFormat.format,
        .imageColorSpace  = window->swapchainSurfaceFormat.colorSpace,
        .imageExtent      = window->swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = surfaceCapabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = presentMode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = oldSwapchain,
    };

    VkSwapchainKHR newSwapchain;
    VkResult create_result = vkCreateSwapchainKHR(ctx->device, &swapChainCreateInfo, NULL, &newSwapchain);
    if (create_result != VK_SUCCESS)
        return (GraphicsResult){ GRAPHICS_ERR_SWAPCHAIN_CREATION_FAILED, create_result };
    if (oldSwapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(ctx->device, oldSwapchain, NULL);
    window->swapchain = newSwapchain;
    window->renderExtent = (VkExtent2D){
        (uint32_t)fmaxf(1.0f, (float)window->swapchainExtent.width),
        (uint32_t)fmaxf(1.0f, (float)window->swapchainExtent.height)
    };
    uint32_t swapchainImageCount;
    vkGetSwapchainImagesKHR(ctx->device, window->swapchain, &swapchainImageCount, NULL);
    window->swapchainImagesCount = swapchainImageCount;
    window->swapchainImages = malloc(sizeof(VkImage) * swapchainImageCount);
    if (!window->swapchainImages) {
        return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };
    }
    vkGetSwapchainImagesKHR(ctx->device, window->swapchain, &swapchainImageCount, window->swapchainImages);

    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static GraphicsResult create_image_views(Context* ctx, Window* window) {
    window->swapchainImageViews = malloc(sizeof(VkImageView) * window->swapchainImagesCount);
    if (!window->swapchainImageViews) {
        return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };
    }
    for(uint32_t i=0; i < window->swapchainImagesCount; i++) {
        VkImageViewCreateInfo createInfo = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = window->swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = window->swapchainSurfaceFormat.format,
            .components = {
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        VkResult result = vkCreateImageView(ctx->device, &createInfo, NULL, &window->swapchainImageViews[i]);
        if (result != VK_SUCCESS)
            return (GraphicsResult){ GRAPHICS_ERR_SWAPCHAIN_IMAGE_VIEWS_CREATION_FAILED, result };
    }
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static GraphicsResult create_depth_image(Context* ctx, Window* window) {
    ImageCreateInfo info = {
        .extent = { window->renderExtent.width, window->renderExtent.height, 1 },
        .format = VK_FORMAT_D32_SFLOAT,
        .usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
        .samples = window->msaa,
        .dedicatedMemory = true,
    };
    return image_create(ctx, &info, &window->depthImage);
}

static GraphicsResult create_color_image(Context* ctx, Window* window) {
    ImageCreateInfo info = {
        .extent = { window->renderExtent.width, window->renderExtent.height, 1 },
        .format = window->swapchainSurfaceFormat.format,
        .usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .samples = window->msaa,
        .dedicatedMemory = true,
    };
    return image_create(ctx, &info, &window->colorImage);
}

static GraphicsResult create_resolve_image(Context* ctx, Window* window) {
    if (window->msaa == VK_SAMPLE_COUNT_1_BIT) {
        memset(&window->resolveImage, 0, sizeof(window->resolveImage));
        return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
    }
    ImageCreateInfo info = {
        .extent = { window->renderExtent.width, window->renderExtent.height, 1 },
        .format = window->swapchainSurfaceFormat.format,
        .usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .dedicatedMemory = true,
    };
    return image_create(ctx, &info, &window->resolveImage);
}

static GraphicsResult create_frame_data(Context* ctx, Window* window) {
    VkSemaphoreCreateInfo semInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for(int i=0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult res = vkCreateSemaphore(ctx->device, &semInfo, NULL, &window->frameData[i].acquireSemaphore);
        if (res != VK_SUCCESS) return (GraphicsResult){ GRAPHICS_ERR_SEMAPHORE_CREATION_FAILED, res };
        res = vkCreateFence(ctx->device, &fenceInfo, NULL, &window->frameData[i].renderFence);
        if (res != VK_SUCCESS) return (GraphicsResult){ GRAPHICS_ERR_FENCE_CREATION_FAILED, res };
        VkCommandPoolCreateInfo graphicsPoolInfo = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = ctx->queues.graphicsFamilyIndex,
        };
        res = vkCreateCommandPool(ctx->device, &graphicsPoolInfo, NULL, &window->frameData[i].graphicsPool);
        if (res != VK_SUCCESS) return (GraphicsResult){ GRAPHICS_ERR_COMMAND_POOL_CREATION_FAILED, res };

        VkCommandPoolCreateInfo computePoolInfo = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = ctx->queues.computeFamilyIndex,
        };
        res = vkCreateCommandPool(ctx->device, &computePoolInfo, NULL, &window->frameData[i].computePool);
        if (res != VK_SUCCESS) return (GraphicsResult){ GRAPHICS_ERR_COMMAND_POOL_CREATION_FAILED, res };

        VkCommandBufferAllocateInfo cmdInfo = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        cmdInfo.commandPool = window->frameData[i].graphicsPool;
        res = vkAllocateCommandBuffers(ctx->device, &cmdInfo, &window->frameData[i].graphicsCmd);
        if (res != VK_SUCCESS) return (GraphicsResult){ GRAPHICS_ERR_COMMAND_BUFFER_ALLOCATION_FAILED, res };

        cmdInfo.commandPool = window->frameData[i].computePool;
        res = vkAllocateCommandBuffers(ctx->device, &cmdInfo, &window->frameData[i].computeCmd);
        if (res != VK_SUCCESS) return (GraphicsResult){ GRAPHICS_ERR_COMMAND_BUFFER_ALLOCATION_FAILED, res };
    }
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static GraphicsResult create_image_data(Context* ctx, Window* window) {
    window->imageData = malloc(sizeof(ImageData) * window->swapchainImagesCount);
    for(uint32_t i=0; i < window->swapchainImagesCount; i++) {
        VkSemaphoreCreateInfo semInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkResult res = vkCreateSemaphore(ctx->device, &semInfo, NULL, &window->imageData[i].renderSemaphore);
        if (res != VK_SUCCESS) return (GraphicsResult){ GRAPHICS_ERR_SEMAPHORE_CREATION_FAILED, res };
    }
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static void destroy_render_targets(Context* ctx, Window* window) {
    image_destroy(ctx, &window->resolveImage);
    image_destroy(ctx, &window->colorImage);
    image_destroy(ctx, &window->depthImage);
}

static void destroy_swapchain_resources(Context* ctx, Window* window) {
    destroy_render_targets(ctx, window);

    if (window->imageData) {
        for (uint32_t i = 0; i < window->swapchainImagesCount; i++) {
            vkDestroySemaphore(ctx->device, window->imageData[i].renderSemaphore, NULL);
        }
        free(window->imageData);
        window->imageData = NULL;
    }

    if (window->swapchainImageViews) {
        for (uint32_t i = 0; i < window->swapchainImagesCount; i++) {
            vkDestroyImageView(ctx->device, window->swapchainImageViews[i], NULL);
        }
        free(window->swapchainImageViews);
        window->swapchainImageViews = NULL;
    }
    if (window->swapchainImages) {
        free(window->swapchainImages);
        window->swapchainImages = NULL;
    }
}

GraphicsResult window_recreate_render_targets(Context* ctx, Window* window) {
    if (!ctx || !window) return (GraphicsResult){ GRAPHICS_ERR_INVALID_ARGUMENT, VK_SUCCESS };

    vkDeviceWaitIdle(ctx->device);

    destroy_render_targets(ctx, window);

    window->renderExtent = (VkExtent2D){
        (uint32_t)fmaxf(1.0f, (float)window->swapchainExtent.width),
        (uint32_t)fmaxf(1.0f, (float)window->swapchainExtent.height)
    };

    GraphicsResult res = create_depth_image(ctx, window);
    if (res.err != GRAPHICS_OK) { destroy_render_targets(ctx, window); return res; }
    res = create_color_image(ctx, window);
    if (res.err != GRAPHICS_OK) { destroy_render_targets(ctx, window); return res; }
    res = create_resolve_image(ctx, window);
    if (res.err != GRAPHICS_OK) { destroy_render_targets(ctx, window); return res; }

    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

GraphicsResult window_recreate_swapchain(Context* ctx, Window* window) {
    if (!ctx || !window) return (GraphicsResult){ GRAPHICS_ERR_INVALID_ARGUMENT, VK_SUCCESS };

    int width = 0, height = 0;
    glfwGetFramebufferSize(window->handle, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window->handle, &width, &height);
        glfwWaitEvents();
    }
    window->minimized = false;

    vkDeviceWaitIdle(ctx->device);

    destroy_swapchain_resources(ctx, window);

    VkSwapchainKHR oldSwapchain = window->swapchain;
    GraphicsResult res = create_swapchain(ctx, window, oldSwapchain);
    if (res.err != GRAPHICS_OK) { destroy_swapchain_resources(ctx, window); return res; }
    res = create_image_views(ctx, window);
    if (res.err != GRAPHICS_OK) { destroy_swapchain_resources(ctx, window); return res; }
    res = create_depth_image(ctx, window);
    if (res.err != GRAPHICS_OK) { destroy_swapchain_resources(ctx, window); return res; }
    res = create_color_image(ctx, window);
    if (res.err != GRAPHICS_OK) { destroy_swapchain_resources(ctx, window); return res; }
    res = create_resolve_image(ctx, window);
    if (res.err != GRAPHICS_OK) { destroy_swapchain_resources(ctx, window); return res; }
    res = create_image_data(ctx, window);
    if (res.err != GRAPHICS_OK) { destroy_swapchain_resources(ctx, window); return res; }

    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

GraphicsResult window_set_vsync(Context* ctx, Window* window, bool vsync) {
    if (!ctx || !window) return (GraphicsResult){ GRAPHICS_ERR_INVALID_ARGUMENT, VK_SUCCESS };
    if (window->vsync == vsync) return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
    window->vsync = vsync;
    return window_recreate_swapchain(ctx, window);
}

GraphicsResult window_set_msaa(Context* ctx, Window* window, VkSampleCountFlagBits samples) {
    if (!ctx || !window) return (GraphicsResult){ GRAPHICS_ERR_INVALID_ARGUMENT, VK_SUCCESS };
    if (samples > ctx->maxMsaaSamples) samples = ctx->maxMsaaSamples;
    if (samples < VK_SAMPLE_COUNT_1_BIT) samples = VK_SAMPLE_COUNT_1_BIT;
    if (window->msaa == samples) return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
    window->msaa = samples;
    return window_recreate_render_targets(ctx, window);
}

GraphicsResult window_create(Context* ctx, Window* window, WindowCreateInfo* info) {
    if (!window || !info->title || !ctx || !ctx->instance) return (GraphicsResult){ GRAPHICS_ERR_INVALID_ARGUMENT, VK_SUCCESS };
    if (!glfwInitialized) return (GraphicsResult){ GRAPHICS_ERR_NO_CONTEXT_INIT, VK_SUCCESS };

    memset(window, 0, sizeof(Window));
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    window->title = strdup(info->title);
    if (!window->title) {
        window_free(ctx, window);
        return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };
    }

    int width  = info->width;
    int height = info->height;

    GLFWwindow* handle = glfwCreateWindow(width, height, window->title, NULL, NULL);
    if (!handle) {
        free(window->title);
        window->title = NULL;
        window_free(ctx, window);
        return (GraphicsResult){ GRAPHICS_ERR_WINDOW_CREATE_FAILED, VK_SUCCESS };
    }

    window->handle = handle;
    window->windowedWidth = info->width;
    window->windowedHeight = info->height;
    glfwGetWindowSize(window->handle, &window->width, &window->height);
    glfwGetWindowPos(window->handle, &window->posx, &window->posy);
    window->windowedPosX = window->posx;
    window->windowedPosY = window->posy;

    window->vsync = info->vsync;
    window->framebufferResized = false;
    window->minimized = false;
    window->cursorMode = CURSOR_MODE_NORMAL;

    glfwSetWindowUserPointer(window->handle, window);
    glfwSetFramebufferSizeCallback(window->handle, framebuffer_size_callback);
    glfwSetKeyCallback(window->handle, key_callback);
    glfwSetMouseButtonCallback(window->handle, mouse_button_callback);
    glfwSetCursorPosCallback(window->handle, cursor_position_callback);
    glfwSetScrollCallback(window->handle, scroll_callback);
    glfwSetCharCallback(window->handle, char_callback);

    glfwGetCursorPos(window->handle, &window->input.mouseX, &window->input.mouseY);

    GraphicsResult res = create_surface(ctx, window);
    if (res.err != GRAPHICS_OK) return res;

    GraphicsResult ctxRes = context_init_hardware(ctx, window);
    if (ctxRes.err != GRAPHICS_OK) {
        window_free(ctx, window);
        return ctxRes;
    }

    window->msaa = info->msaa;
    if (window->msaa > ctx->maxMsaaSamples) window->msaa = ctx->maxMsaaSamples;
    if (window->msaa < VK_SAMPLE_COUNT_1_BIT) window->msaa = VK_SAMPLE_COUNT_1_BIT;

    res = create_swapchain(ctx, window, NULL);
    if (res.err != GRAPHICS_OK) { window_free(ctx, window); return res; }
    res = create_image_views(ctx, window);
    if (res.err != GRAPHICS_OK) { window_free(ctx, window); return res; }
    res = create_depth_image(ctx, window);
    if (res.err != GRAPHICS_OK) { window_free(ctx, window); return res; }
    res = create_color_image(ctx, window);
    if (res.err != GRAPHICS_OK) { window_free(ctx, window); return res; }
    res = create_resolve_image(ctx, window);
    if (res.err != GRAPHICS_OK) { window_free(ctx, window); return res; }
    res = create_frame_data(ctx, window);
    if (res.err != GRAPHICS_OK) { window_free(ctx, window); return res; }
    res = create_image_data(ctx, window);
    if (res.err != GRAPHICS_OK) { window_free(ctx, window); return res; }

    glfwShowWindow(window->handle);
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

void window_free(Context* ctx, Window* window) {
    if (!window || !ctx) return;

    if (ctx->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->device);

        destroy_swapchain_resources(ctx, window);

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(ctx->device, window->frameData[i].acquireSemaphore, NULL);
            vkDestroyFence(ctx->device, window->frameData[i].renderFence, NULL);
            vkDestroyCommandPool(ctx->device, window->frameData[i].graphicsPool, NULL);
            vkDestroyCommandPool(ctx->device, window->frameData[i].computePool, NULL);
        }

        if (window->swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(ctx->device, window->swapchain, NULL);
            window->swapchain = VK_NULL_HANDLE;
        }
    }
    if (window->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx->instance, window->surface, NULL);
        window->surface = VK_NULL_HANDLE;
    }
    if (window->handle) {
        glfwDestroyWindow(window->handle);
        window->handle = NULL;
    }
    if (window->title) {
        free(window->title);
        window->title = NULL;
    }
    memset(window, 0, sizeof(Window));
}

bool window_should_close(Window* out_window) {
    if (!out_window || !out_window->handle) return false;
    return glfwWindowShouldClose(out_window->handle);
}

void window_poll_events(Window* window) {
    if (!window) return;
    
    memcpy(window->input.keysPrev, window->input.keys, sizeof(window->input.keys));
    memcpy(window->input.mouseButtonsPrev, window->input.mouseButtons, sizeof(window->input.mouseButtons));
    window->input.mouseDeltaX = 0.0;
    window->input.mouseDeltaY = 0.0;
    window->input.scrollX = 0.0;
    window->input.scrollY = 0.0;
    window->input.textInputCount = 0;

    glfwPollEvents();
}

static const int s_keyToGlfw[KEY_COUNT] = {
    [KEY_UNKNOWN]      = GLFW_KEY_UNKNOWN,

    [KEY_A] = GLFW_KEY_A, [KEY_B] = GLFW_KEY_B, [KEY_C] = GLFW_KEY_C,
    [KEY_D] = GLFW_KEY_D, [KEY_E] = GLFW_KEY_E, [KEY_F] = GLFW_KEY_F,
    [KEY_G] = GLFW_KEY_G, [KEY_H] = GLFW_KEY_H, [KEY_I] = GLFW_KEY_I,
    [KEY_J] = GLFW_KEY_J, [KEY_K] = GLFW_KEY_K, [KEY_L] = GLFW_KEY_L,
    [KEY_M] = GLFW_KEY_M, [KEY_N] = GLFW_KEY_N, [KEY_O] = GLFW_KEY_O,
    [KEY_P] = GLFW_KEY_P, [KEY_Q] = GLFW_KEY_Q, [KEY_R] = GLFW_KEY_R,
    [KEY_S] = GLFW_KEY_S, [KEY_T] = GLFW_KEY_T, [KEY_U] = GLFW_KEY_U,
    [KEY_V] = GLFW_KEY_V, [KEY_W] = GLFW_KEY_W, [KEY_X] = GLFW_KEY_X,
    [KEY_Y] = GLFW_KEY_Y, [KEY_Z] = GLFW_KEY_Z,

    [KEY_0] = GLFW_KEY_0, [KEY_1] = GLFW_KEY_1, [KEY_2] = GLFW_KEY_2,
    [KEY_3] = GLFW_KEY_3, [KEY_4] = GLFW_KEY_4, [KEY_5] = GLFW_KEY_5,
    [KEY_6] = GLFW_KEY_6, [KEY_7] = GLFW_KEY_7, [KEY_8] = GLFW_KEY_8,
    [KEY_9] = GLFW_KEY_9,

    [KEY_SPACE]     = GLFW_KEY_SPACE,
    [KEY_ENTER]     = GLFW_KEY_ENTER,
    [KEY_ESCAPE]    = GLFW_KEY_ESCAPE,
    [KEY_TAB]       = GLFW_KEY_TAB,
    [KEY_BACKSPACE] = GLFW_KEY_BACKSPACE,

    [KEY_LEFT_SHIFT]  = GLFW_KEY_LEFT_SHIFT,
    [KEY_RIGHT_SHIFT] = GLFW_KEY_RIGHT_SHIFT,
    [KEY_LEFT_CTRL]   = GLFW_KEY_LEFT_CONTROL,
    [KEY_RIGHT_CTRL]  = GLFW_KEY_RIGHT_CONTROL,
    [KEY_LEFT_ALT]    = GLFW_KEY_LEFT_ALT,
    [KEY_RIGHT_ALT]   = GLFW_KEY_RIGHT_ALT,

    [KEY_UP]    = GLFW_KEY_UP,
    [KEY_DOWN]  = GLFW_KEY_DOWN,
    [KEY_LEFT]  = GLFW_KEY_LEFT,
    [KEY_RIGHT] = GLFW_KEY_RIGHT,

    [KEY_F1] = GLFW_KEY_F1,   [KEY_F2] = GLFW_KEY_F2,   [KEY_F3] = GLFW_KEY_F3,
    [KEY_F4] = GLFW_KEY_F4,   [KEY_F5] = GLFW_KEY_F5,   [KEY_F6] = GLFW_KEY_F6,
    [KEY_F7] = GLFW_KEY_F7,   [KEY_F8] = GLFW_KEY_F8,   [KEY_F9] = GLFW_KEY_F9,
    [KEY_F10] = GLFW_KEY_F10, [KEY_F11] = GLFW_KEY_F11, [KEY_F12] = GLFW_KEY_F12,

    [KEY_GRAVE_ACCENT] = GLFW_KEY_GRAVE_ACCENT,
};

static inline int key_to_glfw(Key key) {
    if (key < 0 || key >= KEY_COUNT) return GLFW_KEY_UNKNOWN;
    return s_keyToGlfw[key];
}

bool window_key_down(const Window* window, Key key) {
    int gk = key_to_glfw(key);
    if (gk < 0 || gk > GLFW_KEY_LAST) return false;
    return window->input.keys[gk];
}

bool window_key_pressed(const Window* window, Key key) {
    int gk = key_to_glfw(key);
    if (gk < 0 || gk > GLFW_KEY_LAST) return false;
    return window->input.keys[gk] && !window->input.keysPrev[gk];
}

bool window_key_released(const Window* window, Key key) {
    int gk = key_to_glfw(key);
    if (gk < 0 || gk > GLFW_KEY_LAST) return false;
    return !window->input.keys[gk] && window->input.keysPrev[gk];
}

static const int s_mouseButtonToGlfw[MOUSE_BUTTON_COUNT] = {
    [MOUSE_BUTTON_UNKNOWN] = -1,
    [MOUSE_BUTTON_LEFT]    = GLFW_MOUSE_BUTTON_LEFT,
    [MOUSE_BUTTON_RIGHT]   = GLFW_MOUSE_BUTTON_RIGHT,
    [MOUSE_BUTTON_MIDDLE]  = GLFW_MOUSE_BUTTON_MIDDLE,
    [MOUSE_BUTTON_4]       = GLFW_MOUSE_BUTTON_4,
    [MOUSE_BUTTON_5]       = GLFW_MOUSE_BUTTON_5,
};

static inline int mouse_button_to_glfw(MouseButton button) {
    if (button < 0 || button >= MOUSE_BUTTON_COUNT) return -1;
    return s_mouseButtonToGlfw[button];
}

bool window_mouse_down(const Window* window, MouseButton button) {
    int gb = mouse_button_to_glfw(button);
    if (gb < 0 || gb > GLFW_MOUSE_BUTTON_LAST) return false;
    return window->input.mouseButtons[gb];
}

bool window_mouse_pressed(const Window* window, MouseButton button) {
    int gb = mouse_button_to_glfw(button);
    if (gb < 0 || gb > GLFW_MOUSE_BUTTON_LAST) return false;
    return window->input.mouseButtons[gb] && !window->input.mouseButtonsPrev[gb];
}

bool window_mouse_released(const Window* window, MouseButton button) {
    int gb = mouse_button_to_glfw(button);
    if (gb < 0 || gb > GLFW_MOUSE_BUTTON_LAST) return false;
    return !window->input.mouseButtons[gb] && window->input.mouseButtonsPrev[gb];
}

void window_get_mouse_pos(const Window* window, double* x, double* y) {
    if (!window) return;
    if (x) *x = window->input.mouseX;
    if (y) *y = window->input.mouseY;
}

void window_get_mouse_delta(const Window* window, double* dx, double* dy) {
    if (!window) return;
    if (dx) *dx = window->input.mouseDeltaX;
    if (dy) *dy = window->input.mouseDeltaY;
}

void window_get_scroll_delta(const Window* window, double* sx, double* sy) {
    if (!window) return;
    if (sx) *sx = window->input.scrollX;
    if (sy) *sy = window->input.scrollY;
}

void window_set_title(Window* window, const char* title) {
    if (!window || !window->handle || !title) return;
    char* newTitle = strdup(title);
    if (!newTitle) return;
    free(window->title);
    window->title = newTitle;
    glfwSetWindowTitle(window->handle, window->title);
}

void window_set_icon(Window* window, const TextureAsset* icon) {
    if (!window || !window->handle || !icon) return;
    if (icon->mipCount == 0 || icon->width == 0 || icon->height == 0) return;
 
    if (icon->format != CTEX_FORMAT_RGBA8) {
        return;
    }
 
    const TextureMip *mip0 = &icon->mips[0];
    if (mip0->width == 0 || mip0->height == 0 || !mip0->data) return;
 
    GLFWimage image;
    image.width  = (int)mip0->width;
    image.height = (int)mip0->height;
    image.pixels = (unsigned char *)mip0->data;
    glfwSetWindowIcon(window->handle, 1, &image);
}

void window_set_cursor_mode(Window* window, CursorMode mode) {
    if (!window || !window->handle) return;
    int glfwMode = GLFW_CURSOR_NORMAL;
    if (mode == CURSOR_MODE_HIDDEN) glfwMode = GLFW_CURSOR_HIDDEN;
    else if (mode == CURSOR_MODE_DISABLED) glfwMode = GLFW_CURSOR_DISABLED;
    glfwSetInputMode(window->handle, GLFW_CURSOR, glfwMode);
    window->cursorMode = mode;
}

void window_show(Window* window) {
    if (!window || !window->handle) return;
    glfwShowWindow(window->handle);
}

void window_hide(Window* window) {
    if (!window || !window->handle) return;
    glfwHideWindow(window->handle);
}

const char* window_get_clipboard(Window* window) {
    if (!window || !window->handle) return NULL;
    return glfwGetClipboardString(window->handle);
}

void window_set_clipboard(Window* window, const char* text) {
    if (!window || !window->handle || !text) return;
    glfwSetClipboardString(window->handle, text);
}

int window_context_init(void) {
    glfwInitialized = glfwInit();
    return glfwInitialized;
}

void window_context_free(void) {
    glfwInitialized = false;
    glfwTerminate();
}

const char** window_get_vulkan_extensions(uint32_t* count) {
    return glfwGetRequiredInstanceExtensions(count);
}

void window_close(Window* window) {
    if (!window || !window->handle) return;
    return glfwSetWindowShouldClose(window->handle, 1);
}

double window_get_time(void) {
    return glfwGetTime();
}