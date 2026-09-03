#ifndef PIPELINE_H
#define PIPELINE_H

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>
#include "../vfs/vfs.h"
#include "graphics_error.h"
#include "context.h"

#define PIPELINE_LAYOUT_MAX_SET_LAYOUTS 4
#define PIPELINE_LAYOUT_MAX_PUSH_CONSTANTS 4

typedef struct PipelineLayoutBuilder {
    VkDescriptorSetLayout setLayouts[PIPELINE_LAYOUT_MAX_SET_LAYOUTS];
    uint32_t setLayoutCount;
    VkPushConstantRange pushConstants[PIPELINE_LAYOUT_MAX_PUSH_CONSTANTS];
    uint32_t pushConstantCount;
} PipelineLayoutBuilder;

void pipeline_layout_builder_init(PipelineLayoutBuilder* builder);
void pipeline_layout_builder_add_set_layout(PipelineLayoutBuilder* builder, VkDescriptorSetLayout setLayout);
void pipeline_layout_builder_add_push_constant(PipelineLayoutBuilder* builder, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size);
GraphicsResult pipeline_layout_builder_build(Context* ctx, const PipelineLayoutBuilder* builder, VkPipelineLayout* outLayout);
void pipeline_layout_destroy(Context* ctx, VkPipelineLayout layout);

GraphicsResult pipeline_create_shader_module(Context* ctx, const unsigned char* code, size_t size, VkShaderModule* outModule);
GraphicsResult pipeline_load_shader_from_vfs(Context* ctx, Vfs* vfs, const char* vpath, VkShaderModule* outModule);

GraphicsResult pipeline_cache_create(Context* ctx, const char* filepath, VkPipelineCache* outCache);
GraphicsResult pipeline_cache_save(Context* ctx, VkPipelineCache cache, const char* filepath);
void pipeline_cache_destroy(Context* ctx, VkPipelineCache cache);

#define PIPELINE_MAX_SHADER_STAGES 4
#define PIPELINE_MAX_VERTEX_BINDINGS 8
#define PIPELINE_MAX_VERTEX_ATTRIBUTES 8
#define PIPELINE_MAX_COLOR_ATTACHMENTS 8
#define PIPELINE_MAX_DYNAMIC_STATES 8

typedef struct PipelineBuilder {
    VkPipelineShaderStageCreateInfo shaderStages[PIPELINE_MAX_SHADER_STAGES];
    uint32_t shaderStageCount;

    VkVertexInputBindingDescription vertexBindings[PIPELINE_MAX_VERTEX_BINDINGS];
    VkVertexInputAttributeDescription vertexAttributes[PIPELINE_MAX_VERTEX_ATTRIBUTES];
    uint32_t vertexBindingCount;
    uint32_t vertexAttributeCount;
    VkPipelineVertexInputStateCreateInfo vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly;
    VkPipelineViewportStateCreateInfo viewportState;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkPipelineColorBlendAttachmentState colorBlendAttachments[PIPELINE_MAX_COLOR_ATTACHMENTS];
    VkPipelineColorBlendStateCreateInfo colorBlending;

    VkDynamicState dynamicStates[PIPELINE_MAX_DYNAMIC_STATES];
    VkPipelineDynamicStateCreateInfo dynamicStateInfo;
    uint32_t dynamicStateCount;

    VkFormat colorAttachmentFormats[PIPELINE_MAX_COLOR_ATTACHMENTS];
    VkPipelineRenderingCreateInfo renderingInfo;

    VkPipelineLayout layout;
} PipelineBuilder;

void pipeline_builder_init(PipelineBuilder* builder);
void pipeline_builder_add_shader_stage(PipelineBuilder* builder, VkShaderStageFlagBits stage, VkShaderModule module, const char* entryPoint);
void pipeline_builder_set_topology(PipelineBuilder* builder, VkPrimitiveTopology topology);
void pipeline_builder_set_polygon_mode(PipelineBuilder* builder, VkPolygonMode mode);
void pipeline_builder_set_cull_mode(PipelineBuilder* builder, VkCullModeFlags cullMode, VkFrontFace frontFace);
void pipeline_builder_set_multisampling(PipelineBuilder* builder, VkSampleCountFlagBits samples);

void pipeline_builder_enable_depth_test(PipelineBuilder* builder, bool depthWriteEnable, VkCompareOp compareOp);
void pipeline_builder_disable_depth_test(PipelineBuilder* builder);
void pipeline_builder_set_depth_format(PipelineBuilder* builder, VkFormat format);

void pipeline_builder_add_vertex_binding(PipelineBuilder* builder, uint32_t binding, uint32_t stride, VkVertexInputRate inputRate);
void pipeline_builder_add_vertex_attribute(PipelineBuilder* builder, uint32_t location, uint32_t binding, VkFormat format, uint32_t offset);

void pipeline_builder_set_color_attachment_format(PipelineBuilder* builder, VkFormat format);
void pipeline_builder_set_layout(PipelineBuilder* builder, VkPipelineLayout layout);
void pipeline_builder_enable_alpha_blend(PipelineBuilder* builder, bool enabled);

GraphicsResult pipeline_builder_build(Context* ctx, const PipelineBuilder* builder, VkPipelineCache cache, VkPipeline* outPipeline);

#endif