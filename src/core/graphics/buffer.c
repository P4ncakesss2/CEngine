#include "buffer.h"
#include <string.h>

GraphicsResult buffer_create(Context* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, Buffer* outBuffer) {
    if (!ctx || !outBuffer || size == 0) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_INVALID_ARGUMENT, .vk = VK_ERROR_INITIALIZATION_FAILED };
    }

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo allocInfo = {
        .usage = memoryUsage,
        .flags = allocFlags,
    };

    VkResult vkRes = vmaCreateBuffer(ctx->allocator, &bufferInfo, &allocInfo, &outBuffer->handle, &outBuffer->allocation, NULL);
    if (vkRes != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_OUT_OF_MEMORY, .vk = vkRes };
    }

    outBuffer->size = size;
    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

void buffer_destroy(Context* ctx, Buffer* buffer) {
    if (ctx && buffer && buffer->handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx->allocator, buffer->handle, buffer->allocation);
        buffer->handle = VK_NULL_HANDLE;
        buffer->allocation = VK_NULL_HANDLE;
        buffer->size = 0;
    }
}

void* buffer_map(Context* ctx, Buffer* buffer) {
    void* data = NULL;
    if (vmaMapMemory(ctx->allocator, buffer->allocation, &data) != VK_SUCCESS) {
        return NULL;
    }
    return data;
}

void buffer_unmap(Context* ctx, Buffer* buffer) {
    vmaUnmapMemory(ctx->allocator, buffer->allocation);
}

GraphicsResult buffer_create_staged(Context* ctx, const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer* outBuffer) {
    Buffer stagingBuffer;
    GraphicsResult res = buffer_create(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                       &stagingBuffer);
    if (res.err != GRAPHICS_OK) return res;

    void* mapped = buffer_map(ctx, &stagingBuffer);
    if (!mapped) {
        buffer_destroy(ctx, &stagingBuffer);
        return (GraphicsResult){ .err = GRAPHICS_ERR_OUT_OF_MEMORY, .vk = VK_ERROR_MEMORY_MAP_FAILED };
    }
    memcpy(mapped, data, (size_t)size);
    buffer_unmap(ctx, &stagingBuffer);

    res = buffer_create(ctx, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, outBuffer);
    if (res.err != GRAPHICS_OK) {
        buffer_destroy(ctx, &stagingBuffer);
        return res;
    }

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = ctx->transferCommandPool,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx->device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion = { .srcOffset = 0, .dstOffset = 0, .size = size };
    vkCmdCopyBuffer(cmd, stagingBuffer.handle, outBuffer->handle, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };

    vkQueueSubmit(ctx->queues.transfer, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->queues.transfer);

    vkFreeCommandBuffers(ctx->device, ctx->transferCommandPool, 1, &cmd);
    buffer_destroy(ctx, &stagingBuffer);

    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}