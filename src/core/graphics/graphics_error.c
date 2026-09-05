#include "graphics_error.h"

const char *graphics_err_str(GraphicsError err) {
    switch (err) {
        case GRAPHICS_OK: return "No error";
        case GRAPHICS_ERR_INVALID_ARGUMENT: return "Invalid argument provided";
        case GRAPHICS_ERR_OUT_OF_MEMORY: return "Out of host memory";

        case GRAPHICS_ERR_WINDOW_SYSTEM_INIT_FAILED: return "Window system initialization failed";
        case GRAPHICS_ERR_EXTENSION_FETCH_FAILED: return "Failed to enumerate Vulkan instance extensions";
        case GRAPHICS_ERR_EXTENSION_UNSUPPORTED: return "Required Vulkan extensions are unsupported";
        case GRAPHICS_ERR_PHYSICAL_DEVICE_FETCH_FAILED: return "Failed to enumerate Vulkan physical devices";
        case GRAPHICS_ERR_PHYSICAL_DEVICE_PICK_FAILED: return "No suitable Vulkan physical device found";
        case GRAPHICS_ERR_INSTANCE_CREATION_FAILED: return "Failed to create Vulkan instance";
        case GRAPHICS_ERR_DEBUG_MESSENGER_CREATION_FAILED: return "Failed to create debug utils messenger";
        case GRAPHICS_ERR_LOGICAL_DEVICE_CREATION_FAILED: return "Failed to create Vulkan logical device";
        case GRAPHICS_ERR_VMA_ALLOCATOR_CREATION_FAILED: return "Failed to create VMA memory allocator";
        case GRAPHICS_ERR_COMMAND_POOL_CREATION_FAILED: return "Failed to create command pool";

        case GRAPHICS_ERR_NO_CONTEXT_INIT: return "Window library not initialized";
        case GRAPHICS_ERR_WINDOW_CREATE_FAILED: return "Failed to create window";
        case GRAPHICS_ERR_SURFACE_CREATION_FAILED: return "Failed to create Vulkan window surface";
        case GRAPHICS_ERR_VULKAN_INIT_FAILED: return "Vulkan hardware context initialization failed";

        case GRAPHICS_ERR_SWAPCHAIN_CREATION_FAILED: return "Failed to create Vulkan swapchain";
        case GRAPHICS_ERR_SWAPCHAIN_IMAGE_VIEWS_CREATION_FAILED: return "Failed to create swapchain image views";

        case GRAPHICS_ERR_DEPTH_IMAGE_CREATION_FAILED: return "Failed to create depth buffer image";
        case GRAPHICS_ERR_DEPTH_IMAGE_VIEW_CREATION_FAILED: return "Failed to create depth buffer image view";

        case GRAPHICS_ERR_COLOR_IMAGE_CREATION_FAILED: return "Failed to create MSAA color target image";
        case GRAPHICS_ERR_COLOR_IMAGE_VIEW_CREATION_FAILED: return "Failed to create MSAA color target image view";

        case GRAPHICS_ERR_RESOLVE_IMAGE_CREATION_FAILED: return "Failed to create resolve target image";
        case GRAPHICS_ERR_RESOLVE_IMAGE_VIEW_CREATION_FAILED: return "Failed to create resolve target image view";

        case GRAPHICS_ERR_SEMAPHORE_CREATION_FAILED: return "Failed to create frame synchronization semaphores";
        case GRAPHICS_ERR_FENCE_CREATION_FAILED: return "Failed to create frame synchronization fences";

        case GRAPHICS_ERR_COMMAND_BUFFER_ALLOCATION_FAILED: return "Failed to allocate command buffers";
        case GRAPHICS_ERR_PIPELINE_CACHE_CREATION_FAILED: return "Failed to create pipeline cache";
        case GRAPHICS_ERR_PIPELINE_CREATION_FAILED: return "Failed to create pipeline";
        case GRAPHICS_ERR_PIPELINE_LAYOUT_CREATION_FAILED: return "Failed to create pipeline layout";
        case GRAPHICS_ERR_PIPELINE_CACHE_SAVE_FAILED: return "Failed to save pipeline cache";

        case GRAPHICS_ERR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED: return "Failed to create descriptor set layout";
        case GRAPHICS_ERR_DESCRIPTOR_POOL_CREATION_FAILED: return "Failed to create descriptor pool";
        case GRAPHICS_ERR_DESCRIPTOR_SET_ALLOCATION_FAILED: return "Failed to allocate descriptor set";
        case GRAPHICS_ERR_SAMPLER_CREATION_FAILED: return "Failed to create sampler";

        case GRAPHICS_ERR_IMAGE_CREATION_FAILED: return "Failed to create image";
        case GRAPHICS_ERR_IMAGE_VIEW_CREATION_FAILED: return "Failed to create image view";

        case GRAPHICS_ERR_IO: return "IO error";
        case GRAPHICS_ERR_SHADER_MODULE_CREATION_FAILED: return "Failed to create shader module";
        default: return "Unknown graphics error";
    }
}

const char *vk_result_str(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
        default: return "VK_ERROR_UNHANDLED_CODE";
    }
}
