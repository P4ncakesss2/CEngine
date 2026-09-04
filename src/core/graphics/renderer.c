#include "renderer.h"
#include "ecs/components.h"
#include "window.h"
#include "pipeline.h"
#include "image.h"
#include "../asset/mesh.h"
#include "../asset/texture.h"
#include "shaders.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ui_backend.h"

static inline void* gpu_slot_at(GpuSlotTable* t, uint32_t idx) {
    return (uint8_t*)t->slots + (size_t)idx * t->elemSize;
}
static inline AssetHandle* gpu_slot_handle(GpuSlotTable* t, void* slot) {
    return (AssetHandle*)((uint8_t*)slot + t->handleOffset);
}
static inline bool* gpu_slot_occupied(GpuSlotTable* t, void* slot) {
    return (bool*)((uint8_t*)slot + t->occupiedOffset);
}

static void* gpu_slot_table_find(GpuSlotTable* t, AssetHandle handle) {
    if (t->capacity == 0) return NULL;
    uint32_t mask = t->capacity - 1;
    uint32_t idx = (uint32_t)(handle & mask);
    for (uint32_t probe = 0; probe < t->capacity; probe++) {
        void* s = gpu_slot_at(t, idx);
        if (!*gpu_slot_occupied(t, s)) return NULL;
        if (*gpu_slot_handle(t, s) == handle) return s;
        idx = (idx + 1) & mask;
    }
    return NULL;
}

static bool gpu_slot_table_grow(GpuSlotTable* t) {
    uint32_t newCap = t->capacity ? t->capacity * 2 : 32;
    void* newSlots = calloc(newCap, t->elemSize);
    if (!newSlots) return false;

    GpuSlotTable newTable = *t;
    newTable.slots = newSlots;
    newTable.capacity = newCap;

    uint32_t mask = newCap - 1;
    for (uint32_t i = 0; i < t->capacity; i++) {
        void* s = gpu_slot_at(t, i);
        if (!*gpu_slot_occupied(t, s)) continue;
        uint32_t idx = (uint32_t)(*gpu_slot_handle(t, s) & mask);
        while (*gpu_slot_occupied(&newTable, gpu_slot_at(&newTable, idx))) idx = (idx + 1) & mask;
        memcpy(gpu_slot_at(&newTable, idx), s, t->elemSize);
    }

    free(t->slots);
    t->slots = newSlots;
    t->capacity = newCap;
    return true;
}

static void* gpu_slot_table_insert_new(GpuSlotTable* t, AssetHandle handle) {
    if (t->capacity == 0 || (t->count + 1) * 10 >= t->capacity * 7) {
        if (!gpu_slot_table_grow(t)) return NULL;
    }
    uint32_t mask = t->capacity - 1;
    uint32_t idx = (uint32_t)(handle & mask);
    while (*gpu_slot_occupied(t, gpu_slot_at(t, idx))) idx = (idx + 1) & mask;

    void* s = gpu_slot_at(t, idx);
    memset(s, 0, t->elemSize);
    *gpu_slot_handle(t, s) = handle;
    *gpu_slot_occupied(t, s) = true;
    t->count++;
    return s;
}

static void bindless_write_slot(Renderer* r, uint32_t index, VkImageView view) {
    VkDescriptorImageInfo imageInfo = {
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = r->bindlessSet,
        .dstBinding = BINDLESS_TEXTURE_BINDING,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &imageInfo,
    };
    vkUpdateDescriptorSets(r->ctx->device, 1, &write, 0, NULL);
}

static void bindless_write_sampler(Renderer* r, uint32_t index, VkSampler sampler) {
    VkDescriptorImageInfo samplerInfo = {
        .sampler = sampler,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = r->bindlessSet,
        .dstBinding = BINDLESS_SAMPLER_BINDING,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &samplerInfo,
    };
    vkUpdateDescriptorSets(r->ctx->device, 1, &write, 0, NULL);
}

static uint32_t bindless_alloc_index(Renderer* r) {
    if (r->freeBindlessCount > 0) {
        return r->freeBindlessIndices[--r->freeBindlessCount];
    }
    if (r->nextBindlessIndex == 0) r->nextBindlessIndex = 1;
    return r->nextBindlessIndex++;
}

static GraphicsResult create_bindless_resources(Renderer* r, Context* ctx) {
    VkDescriptorSetLayoutBinding bindings[2] = {
        [BINDLESS_SAMPLER_BINDING] = {
            .binding = BINDLESS_SAMPLER_BINDING,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = MAX_BINDLESS_SAMPLERS,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        [BINDLESS_TEXTURE_BINDING] = {
            .binding = BINDLESS_TEXTURE_BINDING,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = MAX_BINDLESS_TEXTURES,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorBindingFlags bindingFlags[2] = {
        [BINDLESS_SAMPLER_BINDING] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        [BINDLESS_TEXTURE_BINDING] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
    };
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 2,
        .pBindingFlags = bindingFlags,
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindingFlagsInfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 2,
        .pBindings = bindings,
    };

    VkResult vkRes = vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &r->bindlessLayout);
    if (vkRes != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED, .vk = vkRes };
    }

    VkDescriptorPoolSize poolSizes[2] = {
        { .type = VK_DESCRIPTOR_TYPE_SAMPLER,       .descriptorCount = MAX_BINDLESS_SAMPLERS },
        { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = MAX_BINDLESS_TEXTURES },
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes,
    };
    vkRes = vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &r->bindlessPool);
    if (vkRes != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_DESCRIPTOR_POOL_CREATION_FAILED, .vk = vkRes };
    }

    uint32_t variableCount = MAX_BINDLESS_TEXTURES;
    VkDescriptorSetVariableDescriptorCountAllocateInfo variableInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &variableCount,
    };
    VkDescriptorSetAllocateInfo setAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = &variableInfo,
        .descriptorPool = r->bindlessPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &r->bindlessLayout,
    };
    vkRes = vkAllocateDescriptorSets(ctx->device, &setAllocInfo, &r->bindlessSet);
    if (vkRes != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_DESCRIPTOR_SET_ALLOCATION_FAILED, .vk = vkRes };
    }

    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(ctx->physicalDevice, &deviceProps);

    static const struct {
        VkFilter            filter;
        VkSamplerMipmapMode mipmapMode;
        VkSamplerAddressMode addressMode;
        bool                anisotropy;
    } samplerDescs[SAMPLER_Count] = {
        [SAMPLER_Linear_repeat]  = { VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_LINEAR,  VK_SAMPLER_ADDRESS_MODE_REPEAT,        true  },
        [SAMPLER_Nearest_repeat] = { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT,        false },
        [SAMPLER_Linear_clamp]   = { VK_FILTER_LINEAR,  VK_SAMPLER_MIPMAP_MODE_LINEAR,  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, true  },
        [SAMPLER_Nearest_clamp]  = { VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false },
    };

    for (uint32_t kind = 0; kind < SAMPLER_Count; kind++) {
        VkSamplerCreateInfo samplerInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = samplerDescs[kind].filter,
            .minFilter = samplerDescs[kind].filter,
            .mipmapMode = samplerDescs[kind].mipmapMode,
            .addressModeU = samplerDescs[kind].addressMode,
            .addressModeV = samplerDescs[kind].addressMode,
            .addressModeW = samplerDescs[kind].addressMode,
            .anisotropyEnable = samplerDescs[kind].anisotropy,
            .maxAnisotropy = samplerDescs[kind].anisotropy ? deviceProps.limits.maxSamplerAnisotropy : 1.0f,
            .minLod = 0.0f,
            .maxLod = VK_LOD_CLAMP_NONE,
            .mipLodBias = 0.0f,
        };
        vkRes = vkCreateSampler(ctx->device, &samplerInfo, NULL, &r->samplers[kind]);
        if (vkRes != VK_SUCCESS) {
            return (GraphicsResult){ .err = GRAPHICS_ERR_SAMPLER_CREATION_FAILED, .vk = vkRes };
        }
        bindless_write_sampler(r, kind, r->samplers[kind]);
    }

    static const uint8_t missingPixels[4 * 4] = {
        255,   0, 255, 255,
        0,   0,   0, 255,
        0,   0,   0, 255,
        255,   0, 255, 255,
    };
    ImageCreateInfo whiteImageInfo = {
        .extent = { 2, 2, 1 },
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage  = VK_IMAGE_USAGE_SAMPLED_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    GraphicsResult res = image_create_staged(ctx, missingPixels, sizeof(missingPixels), &whiteImageInfo, false, &r->defaultImage);
    if (res.err != GRAPHICS_OK) return res;
    r->nextBindlessIndex = 1;

    for (uint32_t i = 0; i < MAX_BINDLESS_TEXTURES; i++) {
        bindless_write_slot(r, i, r->defaultImage.view);
    }

    static const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    ImageCreateInfo whitePixelInfo = {
        .extent = { 1, 1, 1 },
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage  = VK_IMAGE_USAGE_SAMPLED_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    GraphicsResult whiteRes = image_create_staged(ctx, whitePixel, sizeof(whitePixel), &whitePixelInfo, false, &r->uiWhiteImage);
    if (whiteRes.err != GRAPHICS_OK) return whiteRes;

    bindless_write_slot(r, UI_WHITE_PIXEL_INDEX, r->uiWhiteImage.view);
    r->nextBindlessIndex = UI_WHITE_PIXEL_INDEX + 1;

    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

static void destroy_bindless_resources(Renderer* r, Context* ctx) {
    for (uint32_t i = 0; i < r->texTable.capacity; i++) {
        TextureGpuSlot* s = (TextureGpuSlot*)gpu_slot_at(&r->texTable, i);
        if (!s->occupied) continue;
        image_destroy(ctx, &s->image);
    }
    free(r->texTable.slots);

    free(r->freeBindlessIndices);

    image_destroy(ctx, &r->defaultImage);
    image_destroy(ctx, &r->uiWhiteImage);
    for (uint32_t i = 0; i < MAX_BINDLESS_SAMPLERS; i++) {
        if (r->samplers[i]) vkDestroySampler(ctx->device, r->samplers[i], NULL);
    }
    if (r->bindlessPool) vkDestroyDescriptorPool(ctx->device, r->bindlessPool, NULL);
    if (r->bindlessLayout) vkDestroyDescriptorSetLayout(ctx->device, r->bindlessLayout, NULL);
}

static TextureGpuSlot *tex_slot_table_find(Renderer *r, AssetHandle handle) {
    return (TextureGpuSlot*)gpu_slot_table_find(&r->texTable, handle);
}

static TextureGpuSlot *tex_slot_table_insert_new(Renderer *r, AssetHandle handle) {
    return (TextureGpuSlot*)gpu_slot_table_insert_new(&r->texTable, handle);
}

static bool tex_slot_has_gpu_data(const TextureGpuSlot *slot) {
    return slot->image.handle != VK_NULL_HANDLE;
}

static VkFormat vk_format_for_ctex(CtexFormat fmt) {
    switch (fmt) {
        case CTEX_FORMAT_BC1: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case CTEX_FORMAT_BC3: return VK_FORMAT_BC3_SRGB_BLOCK;
        case CTEX_FORMAT_BC5: return VK_FORMAT_BC5_UNORM_BLOCK; 
        case CTEX_FORMAT_RGBA8:
        default: return VK_FORMAT_R8G8B8A8_SRGB;
    }
}
 
static TextureGpuSlot *texture_gpu_cache_get_or_upload(Renderer *r, AssetHandle handle, TextureAsset *tex) {
    if (handle == ASSET_INVALID_HANDLE || !tex) return NULL;
 
    TextureGpuSlot *slot = tex_slot_table_find(r, handle);
    if (slot && tex_slot_has_gpu_data(slot)) return slot;
 
    if (!slot) {
        slot = tex_slot_table_insert_new(r, handle);
        if (!slot) return NULL;
    }
 
    VkExtent3D extent = { tex->width, tex->height, 1 };
    ImageCreateInfo texImageInfo = {
        .extent = extent,
        .format = vk_format_for_ctex(tex->format),
        .usage  = VK_IMAGE_USAGE_SAMPLED_BIT,
        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    };
 
    GraphicsResult res;
 
    if (tex->mipCount > 1) {
        ImageMipData mips[CTEX_MAX_MIPS];
        for (uint32_t i = 0; i < tex->mipCount; i++) {
            mips[i] = (ImageMipData){
                .data     = tex->mips[i].data,
                .dataSize = tex->mips[i].byteSize,
                .width    = tex->mips[i].width,
                .height   = tex->mips[i].height,
            };
        }
        res = image_create_staged_mips(r->ctx, mips, tex->mipCount, &texImageInfo, &slot->image);
    } else {
        res = image_create_staged(r->ctx, tex->mips[0].data, tex->mips[0].byteSize,
                                   &texImageInfo, false, &slot->image);
    }
 
    if (res.err != GRAPHICS_OK) {
        return NULL;
    }
 
    slot->bindlessIndex = bindless_alloc_index(r);
    bindless_write_slot(r, slot->bindlessIndex, slot->image.view);
    return slot;
}

static const VkSampleCountFlagBits ALL_MSAA_LEVELS[MSAA_LEVEL_COUNT] = {
    VK_SAMPLE_COUNT_1_BIT,  VK_SAMPLE_COUNT_2_BIT,  VK_SAMPLE_COUNT_4_BIT,
    VK_SAMPLE_COUNT_8_BIT,  VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT,
    VK_SAMPLE_COUNT_64_BIT,
};

static uint32_t msaa_level_index(VkSampleCountFlagBits samples) {
    switch (samples) {
        case VK_SAMPLE_COUNT_1_BIT:  return 0;
        case VK_SAMPLE_COUNT_2_BIT:  return 1;
        case VK_SAMPLE_COUNT_4_BIT:  return 2;
        case VK_SAMPLE_COUNT_8_BIT:  return 3;
        case VK_SAMPLE_COUNT_16_BIT: return 4;
        case VK_SAMPLE_COUNT_32_BIT: return 5;
        case VK_SAMPLE_COUNT_64_BIT: return 6;
        default:                     return 0;
    }
}

#define PIPELINE_CACHE_FILEPATH "pipeline_cache.bin"

static GraphicsResult create_pipeline_set(Renderer* r, Context* ctx, Window* window,
                                           VkShaderModule shaderModule,
                                           VkSampleCountFlagBits samples, uint32_t levelIndex) {
    PipelineBuilder prepassBuilder;
    pipeline_builder_init(&prepassBuilder);
    pipeline_builder_add_shader_stage(&prepassBuilder, VK_SHADER_STAGE_VERTEX_BIT, shaderModule, "vertexMain");
    pipeline_builder_add_vertex_binding(&prepassBuilder, 0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX);
    pipeline_builder_add_vertex_attribute(&prepassBuilder, 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position));
    pipeline_builder_add_vertex_attribute(&prepassBuilder, 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal));
    pipeline_builder_add_vertex_attribute(&prepassBuilder, 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv1));
    pipeline_builder_enable_depth_test(&prepassBuilder, true, VK_COMPARE_OP_LESS);
    pipeline_builder_set_depth_format(&prepassBuilder, VK_FORMAT_D32_SFLOAT);
    pipeline_builder_set_layout(&prepassBuilder, r->pipelineLayout);
    pipeline_builder_set_multisampling(&prepassBuilder, samples);
    pipeline_builder_set_cull_mode(&prepassBuilder, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    prepassBuilder.renderingInfo.colorAttachmentCount = 0;
    prepassBuilder.renderingInfo.pColorAttachmentFormats = NULL;

    GraphicsResult prepassRes = pipeline_builder_build(ctx, &prepassBuilder, r->pipelineCache, &r->depthPrepassPipelines[levelIndex]);
    if (prepassRes.err != GRAPHICS_OK) return prepassRes;

    PipelineBuilder geometryBuilder;
    pipeline_builder_init(&geometryBuilder);
    pipeline_builder_add_shader_stage(&geometryBuilder, VK_SHADER_STAGE_VERTEX_BIT, shaderModule, "vertexMain");
    pipeline_builder_add_shader_stage(&geometryBuilder, VK_SHADER_STAGE_FRAGMENT_BIT, shaderModule, "fragmentMain");
    pipeline_builder_add_vertex_binding(&geometryBuilder, 0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX);
    pipeline_builder_add_vertex_attribute(&geometryBuilder, 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position));
    pipeline_builder_add_vertex_attribute(&geometryBuilder, 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal));
    pipeline_builder_add_vertex_attribute(&geometryBuilder, 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv1));
    pipeline_builder_enable_depth_test(&geometryBuilder, false, VK_COMPARE_OP_EQUAL);
    pipeline_builder_set_depth_format(&geometryBuilder, VK_FORMAT_D32_SFLOAT);
    pipeline_builder_set_color_attachment_format(&geometryBuilder, window->swapchainSurfaceFormat.format);
    pipeline_builder_set_layout(&geometryBuilder, r->pipelineLayout);
    pipeline_builder_set_multisampling(&geometryBuilder, samples);
    pipeline_builder_set_cull_mode(&geometryBuilder, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

    GraphicsResult geometryRes = pipeline_builder_build(ctx, &geometryBuilder, r->pipelineCache, &r->geometryPipelines[levelIndex]);
    if (geometryRes.err != GRAPHICS_OK) return geometryRes;

    PipelineBuilder transparentBuilder;
    pipeline_builder_init(&transparentBuilder);
    pipeline_builder_add_shader_stage(&transparentBuilder, VK_SHADER_STAGE_VERTEX_BIT, shaderModule, "vertexMain");
    pipeline_builder_add_shader_stage(&transparentBuilder, VK_SHADER_STAGE_FRAGMENT_BIT, shaderModule, "fragmentMain");
    pipeline_builder_add_vertex_binding(&transparentBuilder, 0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX);
    pipeline_builder_add_vertex_attribute(&transparentBuilder, 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position));
    pipeline_builder_add_vertex_attribute(&transparentBuilder, 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal));
    pipeline_builder_add_vertex_attribute(&transparentBuilder, 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv1));
    pipeline_builder_enable_depth_test(&transparentBuilder, false, VK_COMPARE_OP_LESS);
    pipeline_builder_set_depth_format(&transparentBuilder, VK_FORMAT_D32_SFLOAT);
    pipeline_builder_set_color_attachment_format(&transparentBuilder, window->swapchainSurfaceFormat.format);
    pipeline_builder_set_layout(&transparentBuilder, r->pipelineLayout);
    pipeline_builder_set_multisampling(&transparentBuilder, samples);
    pipeline_builder_enable_alpha_blend(&transparentBuilder, true);
    pipeline_builder_set_cull_mode(&transparentBuilder, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

    GraphicsResult transparentRes = pipeline_builder_build(ctx, &transparentBuilder, r->pipelineCache, &r->transparentPipelines[levelIndex]);
    if (transparentRes.err != GRAPHICS_OK) return transparentRes;

    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

static GraphicsResult create_pipelines(Renderer* r, Context* ctx, Window* window, AssetManager* assets) {
    VkShaderModule shaderModule;
    GraphicsResult shaderRes = pipeline_create_shader_module(ctx, (const unsigned char*)geometry_spirv, geometry_spirv_size, &shaderModule);
    if (shaderRes.err != GRAPHICS_OK) return shaderRes;

    PipelineLayoutBuilder layoutBuilder;
    pipeline_layout_builder_init(&layoutBuilder);
    pipeline_layout_builder_add_set_layout(&layoutBuilder, r->bindlessLayout);
    pipeline_layout_builder_add_push_constant(&layoutBuilder, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(RenderPushConstants));

    GraphicsResult pipelineRes = pipeline_layout_builder_build(ctx, &layoutBuilder, &r->pipelineLayout);
    if (pipelineRes.err != GRAPHICS_OK) {
        vkDestroyShaderModule(ctx->device, shaderModule, NULL);
        return pipelineRes;
    }

    GraphicsResult cacheRes = pipeline_cache_create(ctx, PIPELINE_CACHE_FILEPATH, &r->pipelineCache);
    if (cacheRes.err != GRAPHICS_OK) {
        r->pipelineCache = VK_NULL_HANDLE;
    }

    uint32_t maxLevelIndex = msaa_level_index(ctx->maxMsaaSamples);
    for (uint32_t levelIndex = 0; levelIndex <= maxLevelIndex; levelIndex++) {
        GraphicsResult res = create_pipeline_set(r, ctx, window, shaderModule, ALL_MSAA_LEVELS[levelIndex], levelIndex);
        if (res.err != GRAPHICS_OK) {
            vkDestroyShaderModule(ctx->device, shaderModule, NULL);
            return res;
        }
    }

    vkDestroyShaderModule(ctx->device, shaderModule, NULL);
    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

static void destroy_pipelines(Renderer* r, Context* ctx) {
    for (uint32_t i = 0; i < MSAA_LEVEL_COUNT; i++) {
        if (r->depthPrepassPipelines[i]) vkDestroyPipeline(ctx->device, r->depthPrepassPipelines[i], NULL);
        if (r->geometryPipelines[i])     vkDestroyPipeline(ctx->device, r->geometryPipelines[i], NULL);
        if (r->transparentPipelines[i])  vkDestroyPipeline(ctx->device, r->transparentPipelines[i], NULL);
    }
    vkDestroyPipelineLayout(ctx->device, r->pipelineLayout, NULL);

    pipeline_cache_save(ctx, r->pipelineCache, PIPELINE_CACHE_FILEPATH);
    pipeline_cache_destroy(ctx, r->pipelineCache);
    r->pipelineCache = VK_NULL_HANDLE;
}

static GraphicsResult create_frame_buffer(Context* ctx, VkDeviceSize size, FrameObjectBuffer* out) {
    GraphicsResult res = buffer_create(ctx, size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        &out->buffer);
    if (res.err != GRAPHICS_OK) return res;

    out->mapped = buffer_map(ctx, &out->buffer);

    VkBufferDeviceAddressInfo addrInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = out->buffer.handle,
    };
    out->address = vkGetBufferDeviceAddress(ctx->device, &addrInfo);

    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

static void destroy_frame_buffers(Renderer* r, Context* ctx, FrameObjectBuffer* buffers) {
    (void)r;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        FrameObjectBuffer* ob = &buffers[i];
        if (ob->buffer.handle == VK_NULL_HANDLE) continue;
        buffer_unmap(ctx, &ob->buffer);
        buffer_destroy(ctx, &ob->buffer);
    }
}

#define GPU_SLOT_TABLE(SlotType) \
    (GpuSlotTable){ \
        .elemSize       = sizeof(SlotType), \
        .handleOffset   = offsetof(SlotType, handle), \
        .occupiedOffset = offsetof(SlotType, occupied), \
    }

GraphicsResult renderer_init(Renderer* r, Context* ctx, Window* window, AssetManager* assets, Ecs* ecs, UiDrawFn uiDrawFn, void* uiDrawUserdata) {
    memset(r, 0, sizeof(Renderer));
    r->ctx = ctx;
    r->window = window;
    r->ecs = ecs;
    r->assets = assets;
    r->uiDrawFn = uiDrawFn;
    r->uiDrawUserdata = uiDrawUserdata;

    r->meshTable = GPU_SLOT_TABLE(MeshGpuSlot);
    r->texTable  = GPU_SLOT_TABLE(TextureGpuSlot);

    GraphicsResult res = create_bindless_resources(r, ctx);
    if (res.err != GRAPHICS_OK) return res;

    res = create_pipelines(r, ctx, window, assets);
    if (res.err != GRAPHICS_OK) {
        destroy_bindless_resources(r, ctx);
        return res;
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        res = create_frame_buffer(ctx, (VkDeviceSize)MAX_FRAME_RENDER_OBJECTS * sizeof(ObjectData), &r->objectBuffers[i]);
        if (res.err != GRAPHICS_OK) {
            destroy_pipelines(r, ctx);
            destroy_bindless_resources(r, ctx);
            destroy_frame_buffers(r, ctx, r->objectBuffers);
            destroy_frame_buffers(r, ctx, r->cameraBuffers);
            return res;
        }
        res = create_frame_buffer(ctx, sizeof(CameraData), &r->cameraBuffers[i]);
        if (res.err != GRAPHICS_OK) {
            destroy_pipelines(r, ctx);
            destroy_bindless_resources(r, ctx);
            destroy_frame_buffers(r, ctx, r->objectBuffers);
            destroy_frame_buffers(r, ctx, r->cameraBuffers);
            return res;
        }
        res = create_frame_buffer(ctx, (VkDeviceSize)MAX_FRAME_RENDER_OBJECTS * sizeof(MaterialData), &r->materialBuffers[i]);
        if (res.err != GRAPHICS_OK) {
            destroy_pipelines(r, ctx);
            destroy_bindless_resources(r, ctx);
            destroy_frame_buffers(r, ctx, r->objectBuffers);
            destroy_frame_buffers(r, ctx, r->cameraBuffers);
            destroy_frame_buffers(r, ctx, r->materialBuffers);
            return res;
        }
    }

    r->uiActive = uiDrawFn
        ? ui_init(window->handle, ctx->instance, ctx->physicalDevice, ctx->device, ctx->queues.graphics, MAX_FRAMES_IN_FLIGHT, MAX_FRAMES_IN_FLIGHT, window->swapchainSurfaceFormat.format)
        : false;
    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

#define SCENE_TARGET_ALLOC_GRANULARITY 64

static uint32_t round_up_to_granularity(uint32_t v) {
    return ((v + SCENE_TARGET_ALLOC_GRANULARITY - 1) / SCENE_TARGET_ALLOC_GRANULARITY)
           * SCENE_TARGET_ALLOC_GRANULARITY;
}

VkExtent2D renderer_get_extent(const Renderer* r) {
    return r->window->renderExtent;
}

static bool slot_has_gpu_data(const MeshGpuSlot *slot) {
    return slot->vertexBuffer.handle != VK_NULL_HANDLE;
}

void renderer_free(Renderer* r) {
    Context* ctx = r->ctx;
    context_wait_idle(ctx);
    ui_shutdown();
    destroy_pipelines(r, ctx);
    destroy_frame_buffers(r, ctx, r->objectBuffers);
    destroy_frame_buffers(r, ctx, r->cameraBuffers);
    destroy_frame_buffers(r, ctx, r->materialBuffers);
    for (uint32_t i = 0; i < r->meshTable.capacity; i++) {
        MeshGpuSlot *s = (MeshGpuSlot*)gpu_slot_at(&r->meshTable, i);
        if (!s->occupied || !slot_has_gpu_data(s)) continue;
        buffer_destroy(ctx, &s->vertexBuffer);
        buffer_destroy(ctx, &s->indexBuffer);
    }
    free(r->meshTable.slots);
    destroy_bindless_resources(r, ctx);
    memset(r, 0, sizeof(*r));
}

static MeshGpuSlot *mesh_slot_table_find(Renderer *r, AssetHandle handle) {
    return (MeshGpuSlot*)gpu_slot_table_find(&r->meshTable, handle);
}

static MeshGpuSlot *mesh_slot_table_insert_new(Renderer *r, AssetHandle handle) {
    return (MeshGpuSlot*)gpu_slot_table_insert_new(&r->meshTable, handle);
}

void renderer_clear_gpu_cache(Renderer* r) {
    Context* ctx = r->ctx;

    context_wait_idle(ctx);

    for (uint32_t i = 0; i < r->meshTable.capacity; i++) {
        MeshGpuSlot *s = (MeshGpuSlot*)gpu_slot_at(&r->meshTable, i);
        if (!s->occupied || !slot_has_gpu_data(s)) continue;
        buffer_destroy(ctx, &s->vertexBuffer);
        buffer_destroy(ctx, &s->indexBuffer);
    }
    free(r->meshTable.slots);
    r->meshTable.slots    = NULL;
    r->meshTable.capacity = 0;
    r->meshTable.count    = 0;

    for (uint32_t i = 0; i < r->texTable.capacity; i++) {
        TextureGpuSlot *s = (TextureGpuSlot*)gpu_slot_at(&r->texTable, i);
        if (!s->occupied || !tex_slot_has_gpu_data(s)) continue;
        image_destroy(ctx, &s->image);
    }
    free(r->texTable.slots);
    r->texTable.slots    = NULL;
    r->texTable.capacity = 0;
    r->texTable.count    = 0;

    free(r->freeBindlessIndices);
    r->freeBindlessIndices  = NULL;
    r->freeBindlessCount    = 0;
    r->freeBindlessCapacity = 0;
    r->nextBindlessIndex    = UI_WHITE_PIXEL_INDEX + 1;

    for (uint32_t i = 0; i < MAX_BINDLESS_TEXTURES; i++) {
        bindless_write_slot(r, i, r->defaultImage.view);
    }
    bindless_write_slot(r, UI_WHITE_PIXEL_INDEX, r->uiWhiteImage.view);

    r->drawItemCount                        = 0;
    r->transparentDrawItemCount              = 0;
}

static MeshGpuSlot *mesh_gpu_cache_get_or_upload(Renderer *r, AssetHandle handle, MeshAsset *mesh) {
    if (handle == ASSET_INVALID_HANDLE || !mesh) return NULL;

    MeshGpuSlot *slot = mesh_slot_table_find(r, handle);
    if (slot && slot_has_gpu_data(slot)) return slot;

    if (!slot) {
        slot = mesh_slot_table_insert_new(r, handle);
        if (!slot) return NULL;
    }

    VkDeviceSize vertexSize = (VkDeviceSize)mesh->vertex_count * sizeof(*mesh->vertices);
    VkDeviceSize indexSize  = (VkDeviceSize)mesh->index_count  * sizeof(*mesh->indices);

    if (buffer_create_staged(r->ctx, mesh->vertices, vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &slot->vertexBuffer).err != GRAPHICS_OK) {
        return NULL;
    }
    if (buffer_create_staged(r->ctx, mesh->indices, indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &slot->indexBuffer).err != GRAPHICS_OK) {
        buffer_destroy(r->ctx, &slot->vertexBuffer);
        memset(&slot->vertexBuffer, 0, sizeof(slot->vertexBuffer));
        return NULL;
    }

    slot->indexCount = mesh->index_count;
    return slot;
}

static void sort_draw_items_back_to_front(Renderer* r, DrawItem* items, uint32_t n, vec3 camPos) {
    FrameObjectBuffer* ob = &r->objectBuffers[r->currentFrame];
    ObjectData* objects = (ObjectData*)ob->mapped;
    if (n < 2) return;

    static float distSq[MAX_FRAME_RENDER_OBJECTS];
    for (uint32_t i = 0; i < n; i++) {
        ObjectData* obj = &objects[items[i].objectIndex];
        vec3 pos = { obj->model[3][0], obj->model[3][1], obj->model[3][2] };
        vec3 diff;
        glm_vec3_sub(pos, camPos, diff);
        distSq[i] = glm_vec3_norm2(diff);
    }

    for (uint32_t i = 1; i < n; i++) {
        DrawItem keyItem = items[i];
        float keyDist = distSq[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && distSq[j] < keyDist) {
            items[j + 1] = items[j];
            distSq[j + 1] = distSq[j];
            j--;
        }
        items[j + 1] = keyItem;
        distSq[j + 1] = keyDist;
    }
}

static void build_draw_list(Renderer* r, const RenderObject* objects, const MaterialObject* materials, uint32_t count) {
    FrameObjectBuffer* ob = &r->objectBuffers[r->currentFrame];
    FrameObjectBuffer* mb = &r->materialBuffers[r->currentFrame];
    ObjectData* objectData = (ObjectData*)ob->mapped;
    MaterialData* materialData = (MaterialData*)mb->mapped;

    r->drawItemCount = 0;
    r->transparentDrawItemCount = 0;

    if (count > MAX_FRAME_RENDER_OBJECTS) count = MAX_FRAME_RENDER_OBJECTS;

    for (uint32_t objectIndex = 0; objectIndex < count; objectIndex++) {
        const RenderObject* src = &objects[objectIndex];

        MeshGpuSlot *gpu = mesh_slot_table_find(r, src->meshHandle);
        if (!gpu || !slot_has_gpu_data(gpu)) {
            MeshAsset *mesh = asset_get(r->assets, src->meshHandle, ASSET_TYPE_Mesh);
            gpu = mesh_gpu_cache_get_or_upload(r, src->meshHandle, mesh);
        }
        if (!gpu || gpu->indexCount == 0) continue;

        const MaterialObject* mat = &materials[objectIndex];
        uint32_t albedoIndex = BINDLESS_DEFAULT_INDEX;
        TextureGpuSlot *texGpu = tex_slot_table_find(r, mat->albedoHandle);
        if (!texGpu || !tex_slot_has_gpu_data(texGpu)) {
            TextureAsset *texAsset = asset_get(r->assets, mat->albedoHandle, ASSET_TYPE_Texture);
            texGpu = texture_gpu_cache_get_or_upload(r, mat->albedoHandle, texAsset);
        }
        if (texGpu && tex_slot_has_gpu_data(texGpu)) {
            albedoIndex = texGpu->bindlessIndex;
        }

        glm_mat4_copy(src->model, objectData[objectIndex].model);
        glm_mat4_inv((vec4*)src->model, objectData[objectIndex].invmodel);

        materialData[objectIndex].albedoIndex = albedoIndex;
        materialData[objectIndex].samplerIndex = (uint32_t)mat->samplerKind;
        materialData[objectIndex].isTiled = mat->isTiled ? 1u : 0u;
        materialData[objectIndex].isStochasticTiled = false;
        if (mat->isTiled) {
            materialData[objectIndex].tiling[0] = mat->tiling[0];
            materialData[objectIndex].tiling[1] = mat->tiling[1];
            materialData[objectIndex].isStochasticTiled = mat->isStochasticTiled;
        } else {
            materialData[objectIndex].tiling[0] = 1.0f;
            materialData[objectIndex].tiling[1] = 1.0f;
        }


        DrawItem *item;
        if (src->transparent) {
            item = &r->transparentDrawItems[r->transparentDrawItemCount++];
        } else {
            item = &r->drawItems[r->drawItemCount++];
        }
        item->meshHandle = src->meshHandle;
        item->objectIndex = objectIndex;
    }
}

static void draw_items(Renderer* r, VkCommandBuffer cmd, VkDeviceAddress cameraAddress,
                        VkDeviceAddress baseObjectAddress, VkDeviceAddress baseMaterialAddress,
                        const DrawItem* items, uint32_t itemCount) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout,
                             BINDLESS_TEXTURE_SET, 1, &r->bindlessSet, 0, NULL);

    for (uint32_t i = 0; i < itemCount; i++) {
        const DrawItem *item = &items[i];
        MeshGpuSlot *gpu = mesh_slot_table_find(r, item->meshHandle);
        if (!gpu || !slot_has_gpu_data(gpu)) continue;

        RenderPushConstants pc = {
            .cameraAdress = cameraAddress,
            .objectAddress = baseObjectAddress + (item->objectIndex * sizeof(ObjectData)),
            .materialAddress = baseMaterialAddress + (item->objectIndex * sizeof(MaterialData)),
        };
        vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(pc), &pc);

        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &gpu->vertexBuffer.handle, offsets);
        vkCmdBindIndexBuffer(cmd, gpu->indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, gpu->indexCount, 1, 0, 0, 1);
    }
}

static void barrier_image(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                           VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspect,
            .levelCount = 1,
            .layerCount = 1,
        },
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
    };
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

void renderer_draw_frame(Renderer* r, const RenderObject* objects, const MaterialObject* materials, uint32_t count,
                          mat4 viewproj, vec3 camPos, bool camValid) {
    Context* ctx = r->ctx;
    Window* window = r->window;

    uint32_t msaaIdx = msaa_level_index(window->msaa);

    VkDevice device = ctx->device;
    FrameData* frame = &window->frameData[r->currentFrame];

    vkWaitForFences(device, 1, &frame->renderFence, VK_TRUE, UINT64_MAX);

    if (window->framebufferResized) {
        window->framebufferResized = false;
        window_recreate_swapchain(ctx, window);
        return;
    }

    uint32_t imageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(device, window->swapchain, UINT64_MAX,
                          frame->acquireSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        window_recreate_swapchain(ctx, window);
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        return;
    }

    vkResetFences(device, 1, &frame->renderFence);

    if (r->uiActive) {
        ui_new_frame();
        r->uiDrawFn(r->uiDrawUserdata);
    }

    bool msaaEnabled = window->msaa != VK_SAMPLE_COUNT_1_BIT;
    VkImage blitSrcImage = msaaEnabled ? window->resolveImage.handle : window->colorImage.handle;

    VkCommandBuffer cmd = frame->graphicsCmd;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    barrier_image(cmd, window->colorImage.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

    if (msaaEnabled) {
        barrier_image(cmd, window->resolveImage.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }

    barrier_image(cmd, window->depthImage.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                  0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    FrameObjectBuffer* ob = &r->objectBuffers[r->currentFrame];
    FrameObjectBuffer* cb = &r->cameraBuffers[r->currentFrame];
    FrameObjectBuffer* mb = &r->materialBuffers[r->currentFrame];

    CameraData camData = {0};
    if (camValid) {
        glm_mat4_copy(viewproj, camData.viewproj);
    } else {
        glm_mat4_identity(camData.viewproj);
    }
    memcpy(cb->mapped, &camData, sizeof(CameraData));

    build_draw_list(r, objects, materials, count);

    if (camValid) {
        sort_draw_items_back_to_front(r, r->transparentDrawItems, r->transparentDrawItemCount, camPos);
    }
    
    VkViewport viewport = {
        .x = 0.0f, .y = 0.0f,
        .width = (float)window->renderExtent.width,
        .height = (float)window->renderExtent.height,
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    VkRect2D scissor = { .offset = { 0, 0 }, .extent = window->renderExtent };

    VkRenderingAttachmentInfo prepassDepthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = window->depthImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .depthStencil = { 1.0f, 0 } },
    };

    VkRenderingInfo prepassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = window->renderExtent },
        .layerCount = 1,
        .colorAttachmentCount = 0,
        .pColorAttachments = NULL,
        .pDepthAttachment = &prepassDepthAttachment,
    };

    vkCmdBeginRendering(cmd, &prepassInfo);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->depthPrepassPipelines[msaaIdx]);
    draw_items(r, cmd, cb->address, ob->address, mb->address, r->drawItems, r->drawItemCount);
    vkCmdEndRendering(cmd);

    barrier_image(cmd, window->depthImage.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);

    VkRenderingAttachmentInfo colorAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = window->colorImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = msaaEnabled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { { 0.05f, 0.05f, 0.05f, 1.0f } } },
    };
    if (msaaEnabled) {
        colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttachment.resolveImageView = window->resolveImage.view;
        colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo geometryDepthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = window->depthImage.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    };

    VkRenderingInfo geometryInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = window->renderExtent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &geometryDepthAttachment,
    };

    vkCmdBeginRendering(cmd, &geometryInfo);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->geometryPipelines[msaaIdx]);
    draw_items(r, cmd, cb->address, ob->address, mb->address, r->drawItems, r->drawItemCount);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->transparentPipelines[msaaIdx]);
    draw_items(r, cmd, cb->address, ob->address, mb->address, r->transparentDrawItems, r->transparentDrawItemCount);

    vkCmdEndRendering(cmd);

    if (r->uiActive) {
        VkRenderingAttachmentInfo uiAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = msaaEnabled ? window->resolveImage.view : window->colorImage.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };

        VkRenderingInfo uiInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { .offset = { 0, 0 }, .extent = window->renderExtent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &uiAttachment,
        };

        vkCmdBeginRendering(cmd, &uiInfo);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        ui_render(cmd);
        vkCmdEndRendering(cmd);
    }

    barrier_image(cmd, blitSrcImage, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

    VkImage swapchainImage = window->swapchainImages[imageIndex];
    barrier_image(cmd, swapchainImage, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT);

    VkImageBlit blitRegion = {
        .srcSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
        .srcOffsets = {
            { 0, 0, 0 },
            { (int32_t)window->renderExtent.width, (int32_t)window->renderExtent.height, 1 },
        },
        .dstSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
        .dstOffsets = {
            { 0, 0, 0 },
            { (int32_t)window->swapchainExtent.width, (int32_t)window->swapchainExtent.height, 1 },
        },
    };
    vkCmdBlitImage(cmd,
                   blitSrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blitRegion, VK_FILTER_LINEAR);

    barrier_image(cmd, swapchainImage, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT, 0);

    vkEndCommandBuffer(cmd);

    VkSemaphore renderSemaphore = window->imageData[imageIndex].renderSemaphore;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->acquireSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderSemaphore,
    };
    vkQueueSubmit(ctx->queues.graphics, 1, &submitInfo, frame->renderFence);

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &window->swapchain,
        .pImageIndices = &imageIndex,
    };
    VkResult presentResult = vkQueuePresentKHR(ctx->queues.graphics, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || window->framebufferResized) {
        window->framebufferResized = false;
        window_recreate_swapchain(ctx, window);
    }
    else if (presentResult != VK_SUCCESS) {
        
    }

    r->currentFrame = (r->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}