#include "image.h"
#include "buffer.h"
#include <string.h>

uint32_t image_compute_mip_levels(VkExtent3D extent) {
    uint32_t maxDim = extent.width > extent.height ? extent.width : extent.height;
    uint32_t levels = 1;
    while (maxDim >> 1) {
        maxDim >>= 1;
        levels++;
    }
    return levels;
}

static GraphicsResult image_create_view(Context* ctx, Image* image, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->handle,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image->format,
        .subresourceRange = {
            .aspectMask = aspect,
            .levelCount = image->mipLevels,
            .layerCount = 1,
        },
    };

    VkResult vkRes = vkCreateImageView(ctx->device, &viewInfo, NULL, &image->view);
    if (vkRes != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_IMAGE_VIEW_CREATION_FAILED, .vk = vkRes };
    }
    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

GraphicsResult image_create(Context* ctx, const ImageCreateInfo* info, Image* outImage) {
    if (!ctx || !info || !outImage || info->extent.width == 0 || info->extent.height == 0) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_INVALID_ARGUMENT, .vk = VK_ERROR_INITIALIZATION_FAILED };
    }

    memset(outImage, 0, sizeof(*outImage));

    uint32_t mipLevels = info->mipLevels ? info->mipLevels : 1;
    VkSampleCountFlagBits samples = info->samples ? info->samples : VK_SAMPLE_COUNT_1_BIT;

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = info->format,
        .extent = info->extent,
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = info->usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VmaAllocationCreateInfo allocInfo = {
        .flags = info->dedicatedMemory ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : 0,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkResult vkRes = vmaCreateImage(ctx->allocator, &imageInfo, &allocInfo, &outImage->handle, &outImage->allocation, NULL);
    if (vkRes != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_IMAGE_CREATION_FAILED, .vk = vkRes };
    }

    outImage->extent = info->extent;
    outImage->format = info->format;
    outImage->mipLevels = mipLevels;

    GraphicsResult res = image_create_view(ctx, outImage, info->aspect);
    if (res.err != GRAPHICS_OK) {
        vmaDestroyImage(ctx->allocator, outImage->handle, outImage->allocation);
        memset(outImage, 0, sizeof(*outImage));
        return res;
    }

    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

void image_destroy(Context* ctx, Image* image) {
    if (!ctx || !image || image->handle == VK_NULL_HANDLE) return;
    if (image->view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx->device, image->view, NULL);
    }
    vmaDestroyImage(ctx->allocator, image->handle, image->allocation);
    memset(image, 0, sizeof(*image));
}

GraphicsResult image_create_staged(Context* ctx, const void* data, VkDeviceSize dataSize,
                                    const ImageCreateInfo* info, bool generateMips, Image* outImage) {
    if (!info) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_INVALID_ARGUMENT, .vk = VK_ERROR_INITIALIZATION_FAILED };
    }

    Buffer stagingBuffer;
    GraphicsResult res = buffer_create(ctx, dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                       &stagingBuffer);
    if (res.err != GRAPHICS_OK) return res;

    void* mapped = buffer_map(ctx, &stagingBuffer);
    if (!mapped) {
        buffer_destroy(ctx, &stagingBuffer);
        return (GraphicsResult){ .err = GRAPHICS_ERR_OUT_OF_MEMORY, .vk = VK_ERROR_MEMORY_MAP_FAILED };
    }
    memcpy(mapped, data, (size_t)dataSize);
    buffer_unmap(ctx, &stagingBuffer);

    VkExtent3D extent = info->extent;
    VkImageAspectFlags aspect = info->aspect;
    uint32_t mipLevels = generateMips ? image_compute_mip_levels(extent) : (info->mipLevels ? info->mipLevels : 1);

    ImageCreateInfo imageInfo = *info;
    imageInfo.mipLevels = mipLevels;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (mipLevels > 1) {
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    res = image_create(ctx, &imageInfo, outImage);
    if (res.err != GRAPHICS_OK) {
        buffer_destroy(ctx, &stagingBuffer);
        return res;
    }

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = ctx->graphicsCommandPool,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx->device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toDst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = outImage->handle,
        .subresourceRange = { .aspectMask = aspect, .levelCount = mipLevels, .layerCount = 1 },
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, 0, NULL, 0, NULL, 1, &toDst);

    VkBufferImageCopy region = {
        .imageSubresource = { .aspectMask = aspect, .mipLevel = 0, .layerCount = 1 },
        .imageExtent = extent,
    };
    vkCmdCopyBufferToImage(cmd, stagingBuffer.handle, outImage->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if (mipLevels > 1) {
        int32_t mipWidth = (int32_t)extent.width;
        int32_t mipHeight = (int32_t)extent.height;

        for (uint32_t level = 1; level < mipLevels; level++) {
            VkImageMemoryBarrier prevToSrc = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = outImage->handle,
                .subresourceRange = { .aspectMask = aspect, .baseMipLevel = level - 1, .levelCount = 1, .layerCount = 1 },
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  0, 0, NULL, 0, NULL, 1, &prevToSrc);

            int32_t nextWidth  = mipWidth  > 1 ? mipWidth  / 2 : 1;
            int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

            VkImageBlit blit = {
                .srcSubresource = { .aspectMask = aspect, .mipLevel = level - 1, .layerCount = 1 },
                .srcOffsets = { { 0, 0, 0 }, { mipWidth, mipHeight, 1 } },
                .dstSubresource = { .aspectMask = aspect, .mipLevel = level, .layerCount = 1 },
                .dstOffsets = { { 0, 0, 0 }, { nextWidth, nextHeight, 1 } },
            };
            vkCmdBlitImage(cmd,
                            outImage->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            outImage->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1, &blit, VK_FILTER_LINEAR);

            VkImageMemoryBarrier prevToShaderRead = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = outImage->handle,
                .subresourceRange = { .aspectMask = aspect, .baseMipLevel = level - 1, .levelCount = 1, .layerCount = 1 },
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  0, 0, NULL, 0, NULL, 1, &prevToShaderRead);

            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }
    }

    VkImageMemoryBarrier lastToShaderRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = outImage->handle,
        .subresourceRange = { .aspectMask = aspect, .baseMipLevel = mipLevels - 1, .levelCount = 1, .layerCount = 1 },
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          0, 0, NULL, 0, NULL, 1, &lastToShaderRead);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };

    vkQueueSubmit(ctx->queues.graphics, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->queues.graphics);

    vkFreeCommandBuffers(ctx->device, ctx->graphicsCommandPool, 1, &cmd);
    buffer_destroy(ctx, &stagingBuffer);

    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

GraphicsResult image_create_staged_mips(Context* ctx, const ImageMipData* mips, uint32_t mipCount,
                                         const ImageCreateInfo* info, Image* outImage) {
    if (!info || !mips || mipCount == 0 || mipCount > IMAGE_MAX_MIPS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_INVALID_ARGUMENT, .vk = VK_ERROR_INITIALIZATION_FAILED };
    }

    VkDeviceSize totalSize = 0;
    for (uint32_t i = 0; i < mipCount; i++) totalSize += mips[i].dataSize;

    Buffer stagingBuffer;
    GraphicsResult res = buffer_create(ctx, totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                       &stagingBuffer);
    if (res.err != GRAPHICS_OK) return res;

    uint8_t* mapped = (uint8_t*)buffer_map(ctx, &stagingBuffer);
    if (!mapped) {
        buffer_destroy(ctx, &stagingBuffer);
        return (GraphicsResult){ .err = GRAPHICS_ERR_OUT_OF_MEMORY, .vk = VK_ERROR_MEMORY_MAP_FAILED };
    }

    VkDeviceSize offsets[IMAGE_MAX_MIPS];
    VkDeviceSize cursor = 0;
    for (uint32_t i = 0; i < mipCount; i++) {
        offsets[i] = cursor;
        memcpy(mapped + cursor, mips[i].data, (size_t)mips[i].dataSize);
        cursor += mips[i].dataSize;
    }
    buffer_unmap(ctx, &stagingBuffer);

    VkImageAspectFlags aspect = info->aspect;

    ImageCreateInfo imageInfo = *info;
    imageInfo.mipLevels = mipCount;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    res = image_create(ctx, &imageInfo, outImage);
    if (res.err != GRAPHICS_OK) {
        buffer_destroy(ctx, &stagingBuffer);
        return res;
    }

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = ctx->graphicsCommandPool,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx->device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toDst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = outImage->handle,
        .subresourceRange = { .aspectMask = aspect, .levelCount = mipCount, .layerCount = 1 },
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, 0, NULL, 0, NULL, 1, &toDst);

    VkBufferImageCopy regions[IMAGE_MAX_MIPS];
    for (uint32_t i = 0; i < mipCount; i++) {
        regions[i] = (VkBufferImageCopy){
            .bufferOffset = offsets[i],
            .imageSubresource = { .aspectMask = aspect, .mipLevel = i, .layerCount = 1 },
            .imageExtent = { mips[i].width, mips[i].height, 1 },
        };
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer.handle, outImage->handle,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipCount, regions);

    VkImageMemoryBarrier toShaderRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = outImage->handle,
        .subresourceRange = { .aspectMask = aspect, .levelCount = mipCount, .layerCount = 1 },
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          0, 0, NULL, 0, NULL, 1, &toShaderRead);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };

    vkQueueSubmit(ctx->queues.graphics, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->queues.graphics);

    vkFreeCommandBuffers(ctx->device, ctx->graphicsCommandPool, 1, &cmd);
    buffer_destroy(ctx, &stagingBuffer);

    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}