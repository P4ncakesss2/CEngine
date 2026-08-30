#ifndef IMAGE_H
#define IMAGE_H

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <stdbool.h>
#include "context.h"
#include "graphics_error.h"

typedef struct Image {
    VkImage       handle;
    VkImageView   view;
    VmaAllocation allocation;
    VkExtent3D    extent;
    VkFormat      format;
    uint32_t      mipLevels;
} Image;

typedef struct ImageCreateInfo {
    VkExtent3D            extent;
    VkFormat              format;
    VkImageUsageFlags     usage;
    VkImageAspectFlags    aspect;
    uint32_t              mipLevels;
    VkSampleCountFlagBits samples;
    bool                  dedicatedMemory;
} ImageCreateInfo;

typedef struct ImageMipData {
    const void* data;
    VkDeviceSize dataSize;
    uint32_t    width;
    uint32_t    height;
} ImageMipData;

#define IMAGE_MAX_MIPS 16

uint32_t image_compute_mip_levels(VkExtent3D extent);

GraphicsResult image_create(Context* ctx, const ImageCreateInfo* info, Image* outImage);
GraphicsResult image_create_staged(Context* ctx, const void* data, VkDeviceSize dataSize, const ImageCreateInfo* info, bool generateMips, Image* outImage);
GraphicsResult image_create_staged_mips(Context* ctx, const ImageMipData* mips, uint32_t mipCount, const ImageCreateInfo* info, Image* outImage);

void image_destroy(Context* ctx, Image* image);

#endif // IMAGE_H