#include "context.h"
#include <string.h>
#include "window.h"
#include <stdio.h>
#include <stdlib.h>

static const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};
static const uint32_t VALIDATION_LAYERS_SIZE = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);

static const VkSampleCountFlagBits MSAA_CANDIDATES[] = {
    VK_SAMPLE_COUNT_64_BIT, VK_SAMPLE_COUNT_32_BIT,
    VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_8_BIT,
    VK_SAMPLE_COUNT_4_BIT,  VK_SAMPLE_COUNT_2_BIT,
};
static const uint32_t MSAA_CANDIDATES_SIZE = sizeof(MSAA_CANDIDATES) / sizeof(MSAA_CANDIDATES[0]);

typedef struct DeviceFeatureChain {
    VkPhysicalDeviceVulkan11Features                vk11;
    VkPhysicalDeviceVulkan12Features                vk12;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynState;
    VkPhysicalDeviceVulkan13Features                vk13;
    VkPhysicalDeviceFeatures2                       features2;
} DeviceFeatureChain;

static void device_feature_chain_init(DeviceFeatureChain* c) {
    memset(c, 0, sizeof(*c));
    c->vk11.sType        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    c->vk12.sType        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    c->vk12.pNext        = &c->vk11;
    c->extDynState.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    c->extDynState.pNext = &c->vk12;
    c->vk13.sType        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    c->vk13.pNext        = &c->extDynState;
    c->features2.sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    c->features2.pNext   = &c->vk13;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    (void)severity; (void)type; (void)pUserData;
    printf("[Validation Layer]: %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}

static VkResult create_debug_utils_messenger_ext(VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != NULL)
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void destroy_debug_utils_messenger_ext(VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator)
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != NULL)
        func(instance, debugMessenger, pAllocator);
}

static bool check_validation_layer_support(void) {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties* availableLayers = malloc(sizeof(VkLayerProperties) * layerCount);
    if (!availableLayers) return false;
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

    for(uint32_t i=0; i < VALIDATION_LAYERS_SIZE; i++) {
        bool found = false;
        for(uint32_t j=0; j < layerCount; j++)
            if (strcmp(VALIDATION_LAYERS[i], availableLayers[j].layerName) == 0) { found = true; break; }
        if (!found) { free(availableLayers); return false; }
    }
    free(availableLayers);
    return true;
}

static const char** get_required_extensions(Context* ctx, uint32_t* out_count) {
    uint32_t base_count = 0;
    const char** extensions = malloc(sizeof(*extensions) * 2);
    if (!extensions) return NULL;
    if (ctx->validationEnabled) {
        extensions[base_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
#ifdef __APPLE__
    extensions[base_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif
    uint32_t window_count = 0;
    const char** window = window_get_vulkan_extensions(&window_count);
    if (window && window_count > 0) {
        uint32_t total_count = base_count + window_count;
        const char** temp = realloc(extensions, sizeof(*temp) * total_count);
        if (!temp) {
            free(extensions);
            return NULL;
        }
        extensions = temp;
        memcpy(extensions + base_count, window, window_count * sizeof(char*));
        base_count = total_count;
    }

    if (out_count) {
        *out_count = base_count;
    }
    return extensions;
}

static bool check_extension_support(const char** required, uint32_t requiredCount, VkExtensionProperties* available, uint32_t availableCount) {
    for(uint32_t i=0; i < requiredCount; i++) {
        bool found = false;
        for(uint32_t j=0; j < availableCount; j++)
            if (strcmp(required[i], available[j].extensionName) == 0) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

static bool check_device_extension_support(VkPhysicalDevice device, const char** required, uint32_t requiredCount) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    VkExtensionProperties* available = malloc(sizeof(VkExtensionProperties) * count);
    if (!available) return false;
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, available);

    bool ok = check_extension_support(required, requiredCount, available, count);
    free(available);
    return ok;
}

static GraphicsResult create_instance(Context* ctx, const char* appName) {
    bool enableValidation = ctx->validationEnabled;
    if (enableValidation && !check_validation_layer_support()) {
        printf("Validation layers requested but not available.\n");
        ctx->validationEnabled = false;
    }
    enableValidation = ctx->validationEnabled;
    VkApplicationInfo appInfo = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = (appName && appName[0]) ? appName : "No App",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "No Engine",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = VK_API_VERSION_1_3,
    };

    uint32_t requiredExtensionsCount = 0;
    const char** requiredExtensions = get_required_extensions(ctx, &requiredExtensionsCount);
    if (!requiredExtensions)
        return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };
        
    uint32_t extCount;
    VkResult enumResult = vkEnumerateInstanceExtensionProperties(NULL, &extCount, NULL);
    if (enumResult != VK_SUCCESS || extCount == 0) {
        free(requiredExtensions);
        return (GraphicsResult){ GRAPHICS_ERR_EXTENSION_FETCH_FAILED, enumResult };
    }
    VkExtensionProperties* extensions = malloc(sizeof(VkExtensionProperties) * extCount);
    if (!extensions) {
        free(requiredExtensions);
        return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };
    }
    enumResult = vkEnumerateInstanceExtensionProperties(NULL, &extCount, extensions);
    if (enumResult != VK_SUCCESS || extCount == 0) {
        free(extensions);
        free(requiredExtensions);
        return (GraphicsResult){ GRAPHICS_ERR_EXTENSION_FETCH_FAILED, enumResult };
    }
    if (!check_extension_support(requiredExtensions, requiredExtensionsCount, extensions, extCount)) {
        free(extensions);
        free(requiredExtensions);
        return (GraphicsResult){ GRAPHICS_ERR_EXTENSION_UNSUPPORTED, VK_SUCCESS };
    }

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
        .pUserData       = NULL,
    };

    VkInstanceCreateInfo createInfo = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = enableValidation ? (void*)&debugCreateInfo : NULL,
        .flags                   = 0,
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = enableValidation ? VALIDATION_LAYERS_SIZE : 0,
        .ppEnabledLayerNames     = enableValidation ? VALIDATION_LAYERS : NULL,
        .enabledExtensionCount   = requiredExtensionsCount,
        .ppEnabledExtensionNames = requiredExtensions,
    };
#ifdef __APPLE__
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VkResult result = vkCreateInstance(&createInfo, NULL, &ctx->instance);
    free(extensions);
    free(requiredExtensions);
    if (result != VK_SUCCESS) {
        return (GraphicsResult){ GRAPHICS_ERR_INSTANCE_CREATION_FAILED, result };
    }
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static GraphicsResult create_debug_messenger(Context* ctx) {
    if (!ctx->validationEnabled)
        return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
        .pUserData       = NULL,
    };
    VkResult result = create_debug_utils_messenger_ext(ctx->instance, &createInfo, NULL, &ctx->debugMessenger);
    if (result != VK_SUCCESS)
        return (GraphicsResult){ GRAPHICS_ERR_DEBUG_MESSENGER_CREATION_FAILED, result };
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static const char** get_device_extensions(Context* ctx, uint32_t* count) {
    const char** exts = malloc(sizeof(char*) * 2);
    if (!exts) {
        if (count) *count = 0;
        return NULL;
    }
    uint32_t c = 0;
    exts[c++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
#ifdef __APPLE__
    exts[c++] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
#endif
    if (count) {
        *count = c;
    }
    return exts;
}

static bool select_queue_families(Context* ctx, const VkSurfaceKHR* surface) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    if (!queueFamilies) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalDevice, &queueFamilyCount, queueFamilies);

    ctx->queues.graphicsFamilyIndex = UINT32_MAX;
    ctx->queues.computeFamilyIndex  = UINT32_MAX;
    ctx->queues.transferFamilyIndex = UINT32_MAX;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkQueueFlags flags = queueFamilies[i].queueFlags;
        if (ctx->queues.graphicsFamilyIndex == UINT32_MAX && (flags & VK_QUEUE_GRAPHICS_BIT)) {
            if (surface && *surface != VK_NULL_HANDLE) {
                VkBool32 presentSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(ctx->physicalDevice, i, *surface, &presentSupport);
                if (presentSupport) ctx->queues.graphicsFamilyIndex = i;
            } else {
                ctx->queues.graphicsFamilyIndex = i;
            }
        }
        if (flags & VK_QUEUE_COMPUTE_BIT) {
            if (ctx->queues.computeFamilyIndex == UINT32_MAX || (flags & VK_QUEUE_GRAPHICS_BIT) == 0)
                ctx->queues.computeFamilyIndex = i;
        }
        if (flags & VK_QUEUE_TRANSFER_BIT) {
            if (ctx->queues.transferFamilyIndex == UINT32_MAX ||
                ((flags & VK_QUEUE_GRAPHICS_BIT) == 0 && (flags & VK_QUEUE_COMPUTE_BIT) == 0))
                ctx->queues.transferFamilyIndex = i;
        }
    }
    free(queueFamilies);

    return ctx->queues.graphicsFamilyIndex != UINT32_MAX &&
           ctx->queues.computeFamilyIndex  != UINT32_MAX &&
           ctx->queues.transferFamilyIndex != UINT32_MAX;
}

static uint32_t rate_device_suitability(VkPhysicalDevice device, const VkSurfaceKHR* surface,
                                         const char** requiredExts, uint32_t requiredExtsCount)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);
    if (props.apiVersion < VK_API_VERSION_1_3)
        return 0;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    if (!queueFamilies) return 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

    bool hasGraphics = false, hasCompute = false, hasTransfer = false;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        VkQueueFlags flags = queueFamilies[i].queueFlags;
        if (!hasGraphics && (flags & VK_QUEUE_GRAPHICS_BIT)) {
            if (surface && *surface != VK_NULL_HANDLE) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, *surface, &present);
                if (present) hasGraphics = true;
            } else {
                hasGraphics = true;
            }
        }
        if (!hasCompute  && (flags & VK_QUEUE_COMPUTE_BIT))  hasCompute  = true;
        if (!hasTransfer && (flags & VK_QUEUE_TRANSFER_BIT)) hasTransfer = true;
    }
    free(queueFamilies);
    if (!hasGraphics || !hasCompute || !hasTransfer) return 0;

    if (!check_device_extension_support(device, requiredExts, requiredExtsCount)) return 0;

    if (surface && *surface != VK_NULL_HANDLE) {
        uint32_t formatCount = 0, presentModeCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, *surface, &formatCount, NULL);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, *surface, &presentModeCount, NULL);
        if (formatCount == 0 || presentModeCount == 0) return 0;
    }

    DeviceFeatureChain c;
    device_feature_chain_init(&c);
    vkGetPhysicalDeviceFeatures2(device, &c.features2);
    if (!c.vk13.dynamicRendering || !c.extDynState.extendedDynamicState ||
        !c.features2.features.samplerAnisotropy || !c.vk12.bufferDeviceAddress)
        return 0;

    uint32_t score = 1;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;
    score += props.limits.maxImageDimension2D;
    return score;
}

static VkSampleCountFlagBits get_max_usable_sample_count(VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    VkSampleCountFlags counts =
        props.limits.framebufferColorSampleCounts &
        props.limits.framebufferDepthSampleCounts;

    for (uint32_t i = 0; i < MSAA_CANDIDATES_SIZE; i++) {
        if (counts & (VkSampleCountFlags)MSAA_CANDIDATES[i]) return MSAA_CANDIDATES[i];
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

static void resolve_msaa_samples(Context* ctx) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ctx->physicalDevice, &props);
    ctx->maxMsaaSamples = get_max_usable_sample_count(ctx->physicalDevice);
}

static GraphicsResult pick_physical_device(Context* ctx, const VkSurfaceKHR* surface) {
    uint32_t requiredExtensionsCount = 0;
    const char** requiredExtensions = get_device_extensions(ctx, &requiredExtensionsCount);
    if (!requiredExtensions) {
        return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };
    }

    uint32_t deviceCount = 0;
    VkResult res = vkEnumeratePhysicalDevices(ctx->instance, &deviceCount, NULL);
    if (res != VK_SUCCESS) {
        free(requiredExtensions);
        return (GraphicsResult){ GRAPHICS_ERR_PHYSICAL_DEVICE_FETCH_FAILED, res };
    }
    VkPhysicalDevice* devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    if (!devices) {
        free(requiredExtensions);
        return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };
    }
    res = vkEnumeratePhysicalDevices(ctx->instance, &deviceCount, devices);
    if (res != VK_SUCCESS) {
        free(devices);
        free(requiredExtensions);
        return (GraphicsResult){ GRAPHICS_ERR_PHYSICAL_DEVICE_FETCH_FAILED, res };
    }

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    uint32_t highestScore = 0;
    for (uint32_t i = 0; i < deviceCount; i++) {
        uint32_t score = rate_device_suitability(devices[i], surface, requiredExtensions, requiredExtensionsCount);
        if (score > highestScore) {
            highestScore = score;
            chosen = devices[i];
        }
    }
    free(devices);
    free(requiredExtensions);

    if (chosen == VK_NULL_HANDLE) {
        return (GraphicsResult){ GRAPHICS_ERR_PHYSICAL_DEVICE_PICK_FAILED, VK_SUCCESS };
    }
    ctx->physicalDevice = chosen;
    if (!select_queue_families(ctx, surface)) {
        ctx->physicalDevice = VK_NULL_HANDLE;
        return (GraphicsResult){ GRAPHICS_ERR_PHYSICAL_DEVICE_PICK_FAILED, VK_SUCCESS };
    }
    resolve_msaa_samples(ctx);
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static GraphicsResult create_logical_device(Context* ctx) {
    uint32_t indicesToCheck[3] = {
        ctx->queues.graphicsFamilyIndex,
        ctx->queues.computeFamilyIndex,
        ctx->queues.transferFamilyIndex,
    };
    uint32_t uniqueIndices[3];
    uint32_t uniqueCount = 0;
    for (uint32_t i = 0; i < 3; i++) {
        bool exists = false;
        for (uint32_t j = 0; j < uniqueCount; j++)
            if (uniqueIndices[j] == indicesToCheck[i]) { exists = true; break; }
        if (!exists) uniqueIndices[uniqueCount++] = indicesToCheck[i];
    }

    VkDeviceQueueCreateInfo queueCreateInfos[3];
    float queuePriority = 1.0f;
    for (uint32_t i = 0; i < uniqueCount; i++) {
        queueCreateInfos[i] = (VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = uniqueIndices[i],
            .queueCount       = 1,
            .pQueuePriorities = &queuePriority,
        };
    }

    DeviceFeatureChain c;
    device_feature_chain_init(&c);
    c.vk11.shaderDrawParameters                          = VK_TRUE;
    c.vk12.drawIndirectCount                             = VK_TRUE;
    c.vk12.descriptorIndexing                            = VK_TRUE;
    c.vk12.descriptorBindingPartiallyBound                = VK_TRUE;
    c.vk12.descriptorBindingUpdateUnusedWhilePending      = VK_TRUE;
    c.vk12.runtimeDescriptorArray                        = VK_TRUE;
    c.vk12.descriptorBindingSampledImageUpdateAfterBind   = VK_TRUE;
    c.vk12.descriptorBindingStorageImageUpdateAfterBind   = VK_TRUE;
    c.vk12.shaderSampledImageArrayNonUniformIndexing      = VK_TRUE;
    c.vk12.bufferDeviceAddress                           = VK_TRUE;
    c.extDynState.extendedDynamicState                   = VK_TRUE;
    c.vk13.dynamicRendering                              = VK_TRUE;
    c.vk13.synchronization2                              = VK_TRUE;
    c.features2.features.samplerAnisotropy               = VK_TRUE;
    c.features2.features.fillModeNonSolid                = VK_TRUE;
    c.features2.features.multiDrawIndirect               = VK_TRUE;
    c.features2.features.sampleRateShading                = VK_TRUE;

    uint32_t deviceExtCount = 0;
    const char** deviceExts = get_device_extensions(ctx, &deviceExtCount);
    if (!deviceExts) return (GraphicsResult){ GRAPHICS_ERR_OUT_OF_MEMORY, VK_SUCCESS };

    VkDeviceCreateInfo createInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &c.features2,
        .queueCreateInfoCount    = uniqueCount,
        .pQueueCreateInfos       = queueCreateInfos,
        .enabledExtensionCount   = deviceExtCount,
        .ppEnabledExtensionNames = deviceExts,
        .pEnabledFeatures        = NULL,
    };

    VkResult result = vkCreateDevice(ctx->physicalDevice, &createInfo, NULL, &ctx->device);
    free(deviceExts);
    if (result != VK_SUCCESS) {
        return (GraphicsResult){ GRAPHICS_ERR_LOGICAL_DEVICE_CREATION_FAILED, result };
    }

    vkGetDeviceQueue(ctx->device, ctx->queues.graphicsFamilyIndex, 0, &ctx->queues.graphics);
    vkGetDeviceQueue(ctx->device, ctx->queues.computeFamilyIndex,  0, &ctx->queues.compute);
    vkGetDeviceQueue(ctx->device, ctx->queues.transferFamilyIndex, 0, &ctx->queues.transfer);

    VmaAllocatorCreateInfo allocatorInfo = {
        .flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT,
        .physicalDevice   = ctx->physicalDevice,
        .device           = ctx->device,
        .instance         = ctx->instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    result = vmaCreateAllocator(&allocatorInfo, &ctx->allocator);
    if (result != VK_SUCCESS) {
        vkDestroyDevice(ctx->device, NULL);
        ctx->device = VK_NULL_HANDLE;
        return (GraphicsResult){ GRAPHICS_ERR_VMA_ALLOCATOR_CREATION_FAILED, result };
    }

    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static GraphicsResult create_transfer_pool(Context* ctx) {
    VkCommandPoolCreateInfo poolInfo = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = ctx->queues.transferFamilyIndex,
    };
    VkResult res = vkCreateCommandPool(ctx->device, &poolInfo, NULL, &ctx->transferCommandPool);
    if (res != VK_SUCCESS) {
        return (GraphicsResult){ GRAPHICS_ERR_COMMAND_POOL_CREATION_FAILED, res };
    }
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static GraphicsResult create_graphics_pool(Context* ctx) {
    VkCommandPoolCreateInfo poolInfo = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->queues.graphicsFamilyIndex,
    };
    VkResult res = vkCreateCommandPool(ctx->device, &poolInfo, NULL, &ctx->graphicsCommandPool);
    if (res != VK_SUCCESS) {
        return (GraphicsResult){ GRAPHICS_ERR_COMMAND_POOL_CREATION_FAILED, res };
    }
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

static void teardown_hardware(Context* ctx) {
    if (ctx->graphicsCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx->device, ctx->graphicsCommandPool, NULL);
        ctx->graphicsCommandPool = VK_NULL_HANDLE;
    }
    if (ctx->transferCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx->device, ctx->transferCommandPool, NULL);
        ctx->transferCommandPool = VK_NULL_HANDLE;
    }
    if (ctx->allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(ctx->allocator);
        ctx->allocator = VK_NULL_HANDLE;
    }
    if (ctx->device != VK_NULL_HANDLE) {
        vkDestroyDevice(ctx->device, NULL);
        ctx->device = VK_NULL_HANDLE;
    }
    ctx->physicalDevice = VK_NULL_HANDLE;
}

GraphicsResult context_init_hardware(Context* ctx, Window* window) {
    const VkSurfaceKHR* surface = window ? &window->surface : NULL;

    GraphicsResult res = pick_physical_device(ctx, surface);
    if (res.err != GRAPHICS_OK) return res;

    res = create_logical_device(ctx);
    if (res.err != GRAPHICS_OK) { teardown_hardware(ctx); return res; }

    res = create_transfer_pool(ctx);
    if (res.err != GRAPHICS_OK) { teardown_hardware(ctx); return res; }

    res = create_graphics_pool(ctx);
    if (res.err != GRAPHICS_OK) { teardown_hardware(ctx); return res; }

    VkPhysicalDeviceProperties gpuProps;
    vkGetPhysicalDeviceProperties(ctx->physicalDevice, &gpuProps);
    snprintf(ctx->gpuName, sizeof(ctx->gpuName), "%s", gpuProps.deviceName);

    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

GraphicsResult context_init(Context* ctx, ContextCreateInfo* info) {
    if (!ctx || !info) return (GraphicsResult){ GRAPHICS_ERR_INVALID_ARGUMENT, VK_SUCCESS };
    memset(ctx, 0, sizeof(Context));
    ctx->validationEnabled = info->validationEnabled;

    if (!window_context_init()) {
        return (GraphicsResult){ GRAPHICS_ERR_WINDOW_SYSTEM_INIT_FAILED, VK_SUCCESS };
    }
    GraphicsResult res = create_instance(ctx, info->appName);
    if (res.err != GRAPHICS_OK) { context_free(ctx); return res; }
    res = create_debug_messenger(ctx);
    if (res.err != GRAPHICS_OK) { context_free(ctx); return res; }
    return (GraphicsResult){ GRAPHICS_OK, VK_SUCCESS };
}

void context_free(Context* ctx) {
    if (!ctx) return;
    teardown_hardware(ctx);
    if (ctx->debugMessenger) {
        destroy_debug_utils_messenger_ext(ctx->instance, ctx->debugMessenger, NULL);
        ctx->debugMessenger = NULL;
    }
    if (ctx->instance) {
        vkDestroyInstance(ctx->instance, NULL);
        ctx->instance = NULL;
    }
    window_context_free();
    memset(ctx, 0, sizeof(Context));
}

void context_wait_idle(Context* ctx) {
    if (!ctx || !ctx->device) return;
    vkDeviceWaitIdle(ctx->device);
}