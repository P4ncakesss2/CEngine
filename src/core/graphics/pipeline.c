#include "pipeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "context.h"

void pipeline_layout_builder_init(PipelineLayoutBuilder* builder) {
    memset(builder, 0, sizeof(PipelineLayoutBuilder));
}

void pipeline_layout_builder_add_set_layout(PipelineLayoutBuilder* builder, VkDescriptorSetLayout setLayout) {
    if (builder->setLayoutCount >= PIPELINE_LAYOUT_MAX_SET_LAYOUTS) return;
    builder->setLayouts[builder->setLayoutCount++] = setLayout;
}

void pipeline_layout_builder_add_push_constant(PipelineLayoutBuilder* builder, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size) {
    if (builder->pushConstantCount >= PIPELINE_LAYOUT_MAX_PUSH_CONSTANTS) return;
    builder->pushConstants[builder->pushConstantCount++] = (VkPushConstantRange){
        .stageFlags = stageFlags,
        .offset = offset,
        .size = size,
    };
}

GraphicsResult pipeline_layout_builder_build(Context* ctx, const PipelineLayoutBuilder* builder, VkPipelineLayout* outLayout) {
    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = builder->setLayoutCount,
        .pSetLayouts = builder->setLayouts,
        .pushConstantRangeCount = builder->pushConstantCount,
        .pPushConstantRanges = builder->pushConstants,
    };

    VkResult vkRes = vkCreatePipelineLayout(ctx->device, &layoutInfo, NULL, outLayout);
    if (vkRes != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_PIPELINE_LAYOUT_CREATION_FAILED, .vk = vkRes };
    }
    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

void pipeline_layout_destroy(Context* ctx, VkPipelineLayout layout) {
    if (layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx->device, layout, NULL);
    }
}

GraphicsResult pipeline_create_shader_module(Context* ctx, const unsigned char* code, size_t size, VkShaderModule* outModule) {
    *outModule = VK_NULL_HANDLE;

    // SPIR-V is a stream of 32-bit words. The Vulkan spec requires pCode to
    // point at 4-byte-aligned memory; a size that isn't a multiple of 4 also
    // means the blob isn't valid SPIR-V at all. `code` here can come from an
    // arbitrary VFS backend, so neither can be assumed - a caller handing us
    // a buffer from a byte-oriented reader (e.g. one that returns pointers
    // into a memory-mapped file, or that reads into a `char*` with no
    // alignment guarantee) would otherwise hand this straight to the driver
    // as-is, which is UB and has previously produced garbage/crashes on
    // strict-alignment or SIMD-heavy validation layers.
    if (size == 0 || size % sizeof(uint32_t) != 0) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_SHADER_MODULE_CREATION_FAILED, .vk = VK_ERROR_UNKNOWN };
    }

    const uint32_t* pCode = (const uint32_t*)code;
    uint32_t* aligned = NULL;
    if (((uintptr_t)code) % _Alignof(uint32_t) != 0) {
        aligned = (uint32_t*)malloc(size);
        if (!aligned) {
            return (GraphicsResult){ .err = GRAPHICS_ERR_SHADER_MODULE_CREATION_FAILED, .vk = VK_ERROR_OUT_OF_HOST_MEMORY };
        }
        memcpy(aligned, code, size);
        pCode = aligned;
    }

    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = pCode,
    };

    VkShaderModule module;
    VkResult result = vkCreateShaderModule(ctx->device, &info, NULL, &module);
    free(aligned);
    if (result != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_SHADER_MODULE_CREATION_FAILED, .vk = result };
    }

    *outModule = module;
    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

GraphicsResult pipeline_load_shader_from_vfs(Context* ctx, Vfs* vfs, const char* vpath, VkShaderModule* outModule) {
    *outModule = VK_NULL_HANDLE;

    unsigned char* bytes = NULL;
    size_t size = 0;
    if (vfs_read_file(vfs, vpath, (void**)&bytes, &size) != VFS_OK) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_IO, .vk = VK_SUCCESS };
    }

    GraphicsResult res = pipeline_create_shader_module(ctx, bytes, size, outModule);
    free(bytes);
    return res;
}

GraphicsResult pipeline_cache_create(Context* ctx, const char* filepath, VkPipelineCache* outCache) {
    *outCache = VK_NULL_HANDLE;

    void* initialData = NULL;
    size_t initialSize = 0;

    if (filepath) {
        FILE* f = fopen(filepath, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0) {
                initialData = malloc((size_t)sz);
                if (initialData && fread(initialData, 1, (size_t)sz, f) == (size_t)sz) {
                    initialSize = (size_t)sz;
                } else {
                    free(initialData);
                    initialData = NULL;
                }
            }
            fclose(f);
        }
    }

    VkPipelineCacheCreateInfo cacheInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = initialSize,
        .pInitialData = initialData,
    };

    VkPipelineCache cache;
    VkResult vkRes = vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &cache);
    free(initialData);

    if (vkRes != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_PIPELINE_CACHE_CREATION_FAILED, .vk = vkRes };
    }

    *outCache = cache;
    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

GraphicsResult pipeline_cache_save(Context* ctx, VkPipelineCache cache, const char* filepath) {
    if (cache == VK_NULL_HANDLE || !filepath) {
        return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
    }

    size_t dataSize = 0;
    VkResult vkRes = vkGetPipelineCacheData(ctx->device, cache, &dataSize, NULL);
    if (vkRes != VK_SUCCESS || dataSize == 0) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_PIPELINE_CACHE_SAVE_FAILED, .vk = vkRes };
    }

    void* data = malloc(dataSize);
    if (!data) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_PIPELINE_CACHE_SAVE_FAILED, .vk = VK_ERROR_OUT_OF_HOST_MEMORY };
    }

    vkRes = vkGetPipelineCacheData(ctx->device, cache, &dataSize, data);
    if (vkRes != VK_SUCCESS) {
        free(data);
        return (GraphicsResult){ .err = GRAPHICS_ERR_PIPELINE_CACHE_SAVE_FAILED, .vk = vkRes };
    }

    FILE* f = fopen(filepath, "wb");
    if (!f) {
        free(data);
        return (GraphicsResult){ .err = GRAPHICS_ERR_IO, .vk = VK_SUCCESS };
    }
    size_t written = fwrite(data, 1, dataSize, f);
    fclose(f);
    free(data);

    if (written != dataSize) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_IO, .vk = VK_SUCCESS };
    }
    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}

void pipeline_cache_destroy(Context* ctx, VkPipelineCache cache) {
    if (cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(ctx->device, cache, NULL);
    }
}

void pipeline_builder_init(PipelineBuilder* builder) {
    memset(builder, 0, sizeof(PipelineBuilder));

    builder->vertexInputInfo = (VkPipelineVertexInputStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pVertexBindingDescriptions = builder->vertexBindings,
        .pVertexAttributeDescriptions = builder->vertexAttributes,
    };

    builder->inputAssembly = (VkPipelineInputAssemblyStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    builder->viewportState = (VkPipelineViewportStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    builder->rasterizer = (VkPipelineRasterizationStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    builder->multisampling = (VkPipelineMultisampleStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    builder->depthStencil = (VkPipelineDepthStencilStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
        .stencilTestEnable = VK_FALSE,
    };

    builder->colorBlendAttachments[0] = (VkPipelineColorBlendAttachmentState){
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_FALSE,
    };

    builder->colorBlending = (VkPipelineColorBlendStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = builder->colorBlendAttachments,
    };

    builder->dynamicStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
    builder->dynamicStates[1] = VK_DYNAMIC_STATE_SCISSOR;
    builder->dynamicStateCount = 2;

    builder->dynamicStateInfo = (VkPipelineDynamicStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = builder->dynamicStateCount,
        .pDynamicStates = builder->dynamicStates,
    };

    builder->renderingInfo = (VkPipelineRenderingCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = builder->colorAttachmentFormats,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
    };
}

void pipeline_builder_add_shader_stage(PipelineBuilder* builder, VkShaderStageFlagBits stage, VkShaderModule module, const char* entryPoint) {
    if (builder->shaderStageCount >= PIPELINE_MAX_SHADER_STAGES) return;
    builder->shaderStages[builder->shaderStageCount++] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = module,
        .pName = entryPoint,
    };
}

void pipeline_builder_set_topology(PipelineBuilder* builder, VkPrimitiveTopology topology) {
    builder->inputAssembly.topology = topology;
}

void pipeline_builder_set_polygon_mode(PipelineBuilder* builder, VkPolygonMode mode) {
    builder->rasterizer.polygonMode = mode;
}

void pipeline_builder_set_cull_mode(PipelineBuilder* builder, VkCullModeFlags cullMode, VkFrontFace frontFace) {
    builder->rasterizer.cullMode = cullMode;
    builder->rasterizer.frontFace = frontFace;
}

void pipeline_builder_set_multisampling(PipelineBuilder* builder, VkSampleCountFlagBits samples) {
    builder->multisampling.rasterizationSamples = samples;
}

void pipeline_builder_enable_depth_test(PipelineBuilder* builder, bool depthWriteEnable, VkCompareOp compareOp) {
    builder->depthStencil.depthTestEnable = VK_TRUE;
    builder->depthStencil.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
    builder->depthStencil.depthCompareOp = compareOp;
}

void pipeline_builder_disable_depth_test(PipelineBuilder* builder) {
    builder->depthStencil.depthTestEnable = VK_FALSE;
    builder->depthStencil.depthWriteEnable = VK_FALSE;
}

void pipeline_builder_set_depth_format(PipelineBuilder* builder, VkFormat format) {
    builder->renderingInfo.depthAttachmentFormat = format;
}

void pipeline_builder_add_vertex_binding(PipelineBuilder* builder, uint32_t binding, uint32_t stride, VkVertexInputRate inputRate) {
    if (builder->vertexBindingCount >= PIPELINE_MAX_VERTEX_BINDINGS) return;
    builder->vertexBindings[builder->vertexBindingCount++] = (VkVertexInputBindingDescription){
        .binding = binding,
        .stride = stride,
        .inputRate = inputRate,
    };
    builder->vertexInputInfo.vertexBindingDescriptionCount = builder->vertexBindingCount;
}

void pipeline_builder_add_vertex_attribute(PipelineBuilder* builder, uint32_t location, uint32_t binding, VkFormat format, uint32_t offset) {
    if (builder->vertexAttributeCount >= PIPELINE_MAX_VERTEX_ATTRIBUTES) return;
    builder->vertexAttributes[builder->vertexAttributeCount++] = (VkVertexInputAttributeDescription){
        .location = location,
        .binding = binding,
        .format = format,
        .offset = offset,
    };
    builder->vertexInputInfo.vertexAttributeDescriptionCount = builder->vertexAttributeCount;
}

void pipeline_builder_set_color_attachment_format(PipelineBuilder* builder, VkFormat format) {
    builder->colorAttachmentFormats[0] = format;
    builder->renderingInfo.colorAttachmentCount = 1;
    builder->renderingInfo.pColorAttachmentFormats = builder->colorAttachmentFormats;
}

void pipeline_builder_enable_alpha_blend(PipelineBuilder* builder, bool enabled) {
    builder->colorBlendAttachments[0].blendEnable = enabled ? VK_TRUE : VK_FALSE;
    builder->colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    builder->colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    builder->colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    builder->colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    builder->colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    builder->colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
}

void pipeline_builder_set_layout(PipelineBuilder* builder, VkPipelineLayout layout) {
    builder->layout = layout;
}

GraphicsResult pipeline_builder_build(Context* ctx, const PipelineBuilder* builder, VkPipelineCache cache, VkPipeline* outPipeline) {
    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &builder->renderingInfo,
        .stageCount = builder->shaderStageCount,
        .pStages = builder->shaderStages,
        .pVertexInputState = &builder->vertexInputInfo,
        .pInputAssemblyState = &builder->inputAssembly,
        .pViewportState = &builder->viewportState,
        .pRasterizationState = &builder->rasterizer,
        .pMultisampleState = &builder->multisampling,
        .pDepthStencilState = &builder->depthStencil,
        .pColorBlendState = &builder->colorBlending,
        .pDynamicState = &builder->dynamicStateInfo,
        .layout = builder->layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    VkResult vkRes = vkCreateGraphicsPipelines(ctx->device, cache, 1, &pipelineInfo, NULL, outPipeline);
    if (vkRes != VK_SUCCESS) {
        return (GraphicsResult){ .err = GRAPHICS_ERR_PIPELINE_CREATION_FAILED, .vk = vkRes };
    }
    return (GraphicsResult){ .err = GRAPHICS_OK, .vk = VK_SUCCESS };
}