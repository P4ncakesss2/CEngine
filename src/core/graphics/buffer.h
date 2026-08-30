#ifndef BUFFER_H
#define BUFFER_H

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "context.h"
#include "graphics_error.h"

typedef struct Buffer {
    VkBuffer handle;
    VmaAllocation allocation;
    VkDeviceSize size;
} Buffer;

GraphicsResult buffer_create(Context* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, Buffer* outBuffer);
GraphicsResult buffer_create_staged(Context* ctx, const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer* outBuffer);
void buffer_destroy(Context* ctx, Buffer* buffer);
void* buffer_map(Context* ctx, Buffer* buffer);
void buffer_unmap(Context* ctx, Buffer* buffer);

#endif // BUFFER_H