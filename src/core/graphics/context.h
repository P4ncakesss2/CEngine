#ifndef CONTEXT_H
#define CONTEXT_H

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <stdbool.h>
#include <stdint.h>
#include "graphics_error.h"

typedef struct Window Window;

typedef struct Queues {
    VkQueue  graphics;
    VkQueue  compute;
    VkQueue  transfer;
    uint32_t graphicsFamilyIndex;
    uint32_t computeFamilyIndex;
    uint32_t transferFamilyIndex;
} Queues;

typedef struct Context {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDebugUtilsMessengerEXT debugMessenger;
    VmaAllocator allocator;
    Queues queues;
    VkCommandPool transferCommandPool;
    VkCommandPool graphicsCommandPool;
    VkSampleCountFlagBits maxMsaaSamples;
    bool validationEnabled;
    char gpuName[256];
} Context;

typedef struct ContextCreateInfo {
    const char* appName;
    bool validationEnabled;
} ContextCreateInfo;

GraphicsResult context_init(Context* ctx, ContextCreateInfo* info);
GraphicsResult context_init_hardware(Context* ctx, Window* window);
void context_wait_idle(Context* ctx);
void context_free(Context* ctx);

#endif