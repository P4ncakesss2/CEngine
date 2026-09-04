#ifndef RENDERER_H
#define RENDERER_H

#include <vulkan/vulkan.h>
#include <cglm/cglm.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../ecs/components.h"
#include "../asset/asset.h"
#include "context.h"
#include "buffer.h"
#include "image.h"
#include "window.h"

#define MAX_FRAME_RENDER_OBJECTS 4096
#define MSAA_LEVEL_COUNT 7

#define MAX_BINDLESS_TEXTURES 4096
#define BINDLESS_TEXTURE_SET  0

#define BINDLESS_SAMPLER_BINDING 0
#define BINDLESS_TEXTURE_BINDING 1

#define MAX_BINDLESS_SAMPLERS SAMPLER_Count
#define SAMPLER_DEFAULT_INDEX SAMPLER_Linear_repeat

#define BINDLESS_DEFAULT_INDEX 0
#define UI_WHITE_PIXEL_INDEX   1

typedef struct ObjectData {
    mat4 model;
    mat4 invmodel;
} ObjectData;

typedef struct MaterialData {
    uint32_t albedoIndex;
    uint32_t samplerIndex;
    uint32_t isTiled;
    uint32_t isStochasticTiled;
    vec2     tiling;
    vec2     _pad1;
} MaterialData;

typedef struct RenderObject {
    AssetHandle meshHandle;
    mat4        model;
    bool        transparent;
} RenderObject;

typedef struct MaterialObject {
    AssetHandle albedoHandle;
    SamplerKind samplerKind;
    bool        isTiled;
    bool        isStochasticTiled;
    vec2        tiling;
} MaterialObject;

typedef struct CameraData {
    mat4 viewproj;
} CameraData;

typedef struct RenderPushConstants {
    VkDeviceAddress cameraAdress;
    VkDeviceAddress objectAddress;
    VkDeviceAddress materialAddress;
} RenderPushConstants;

typedef struct FrameObjectBuffer {
    Buffer          buffer;
    VkDeviceAddress address;
    void*           mapped;
} FrameObjectBuffer;

typedef struct DrawItem {
    AssetHandle meshHandle;
    uint32_t    objectIndex;
} DrawItem;

typedef struct {
    AssetHandle handle;
    Buffer      vertexBuffer;
    Buffer      indexBuffer;
    uint32_t    indexCount;
    bool        occupied;
} MeshGpuSlot;

typedef struct {
    AssetHandle    handle;
    Image          image;
    uint32_t       bindlessIndex;
    bool           occupied;
} TextureGpuSlot;

typedef struct GpuSlotTable {
    void*    slots;
    uint32_t count;
    uint32_t capacity;
    uint32_t elemSize;
    uint32_t handleOffset;
    uint32_t occupiedOffset;
} GpuSlotTable;

typedef void (*UiDrawFn)(void* userdata);

typedef struct Renderer {
    Context*      ctx;
    Ecs* ecs;
    Window*       window;
    AssetManager* assets;

    GpuSlotTable meshTable;
    GpuSlotTable texTable;

    VkDescriptorSetLayout bindlessLayout;
    VkDescriptorPool      bindlessPool;
    VkDescriptorSet       bindlessSet;
    VkSampler             samplers[MAX_BINDLESS_SAMPLERS];

    Image defaultImage;
    Image uiWhiteImage;

    uint32_t  nextBindlessIndex;
    uint32_t *freeBindlessIndices;
    uint32_t  freeBindlessCount;
    uint32_t  freeBindlessCapacity;

    FrameObjectBuffer objectBuffers[MAX_FRAMES_IN_FLIGHT];
    FrameObjectBuffer cameraBuffers[MAX_FRAMES_IN_FLIGHT];
    FrameObjectBuffer materialBuffers[MAX_FRAMES_IN_FLIGHT];

    DrawItem drawItems[MAX_FRAME_RENDER_OBJECTS];
    uint32_t drawItemCount;

    DrawItem transparentDrawItems[MAX_FRAME_RENDER_OBJECTS];
    uint32_t transparentDrawItemCount;

    VkPipeline       depthPrepassPipelines[MSAA_LEVEL_COUNT];
    VkPipeline       geometryPipelines[MSAA_LEVEL_COUNT];
    VkPipeline       transparentPipelines[MSAA_LEVEL_COUNT];
    VkPipelineLayout pipelineLayout;
    VkPipelineCache  pipelineCache;

    UiDrawFn uiDrawFn;
    void*    uiDrawUserdata;
    uint32_t currentFrame;

    bool         uiActive;
} Renderer;


GraphicsResult renderer_init(Renderer* r, Context* ctx, Window* window, AssetManager* assets, Ecs* ecs,UiDrawFn uiDrawFn, void* uiDrawUserdata);
void renderer_free(Renderer* r);
void renderer_clear_gpu_cache(Renderer* r);

VkExtent2D renderer_get_extent(const Renderer* r);
void  renderer_draw_frame(Renderer* r, const RenderObject* objects, const MaterialObject* materials, uint32_t count, mat4 viewproj, vec3 camPos, bool camValid);

#endif