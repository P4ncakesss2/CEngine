#ifndef RENDERER_H
#define RENDERER_H

#include <vulkan/vulkan.h>
#include <cglm/cglm.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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

#define MAX_BINDLESS_SAMPLERS   8
#define SAMPLER_DEFAULT_INDEX   0

#define BINDLESS_DEFAULT_INDEX 0
#define UI_WHITE_PIXEL_INDEX   1

typedef struct ObjectData {
    mat4     model;
    mat4     invmodel;
    uint32_t albedoIndex;
    uint32_t samplerIndex;
    float    _pad[2];
} ObjectData;

typedef struct CameraData {
    mat4 viewproj;
} CameraData;

typedef struct RenderPushConstants {
    VkDeviceAddress cameraAdress;
    VkDeviceAddress objectAddress;
} RenderPushConstants;

typedef struct FrameObjectBuffer {
    Buffer          buffer;
    VkDeviceAddress address;
    ObjectData*     mapped;
} FrameObjectBuffer;

typedef struct DrawItem {
    AssetHandle meshHandle;
    uint32_t    albedoIndex;
    uint32_t    samplerIndex;
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

#define GPU_SLOT_TABLE(SlotType) \
    (GpuSlotTable){ \
        .elemSize       = sizeof(SlotType), \
        .handleOffset   = offsetof(SlotType, handle), \
        .occupiedOffset = offsetof(SlotType, occupied), \
    }

typedef struct RenderObject {
    AssetHandle meshHandle;
    AssetHandle albedoHandle;
    mat4        model;
    bool        transparent;
} RenderObject;

typedef void (*UiDrawFn)(void* userdata);

typedef enum RenderTarget {
    RENDER_TARGET_SWAPCHAIN = 0,
    RENDER_TARGET_OFFSCREEN,
} RenderTarget;

typedef struct Renderer Renderer;
typedef struct UiScrollState UiScrollState;

typedef struct SceneTarget {
    Image       color;
    Image       depth;
    VkSampler   sampler;
    void*       imguiTexId;
    VkExtent2D  extent;
    VkExtent2D  allocatedExtent;
} SceneTarget;

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

    DrawItem drawItems[MAX_FRAME_RENDER_OBJECTS];
    uint32_t drawItemCount;

    DrawItem transparentDrawItems[MAX_FRAME_RENDER_OBJECTS];
    uint32_t transparentDrawItemCount;

    VkPipeline       depthPrepassPipelines[MSAA_LEVEL_COUNT];
    VkPipeline       geometryPipelines[MSAA_LEVEL_COUNT];
    VkPipeline       transparentPipelines[MSAA_LEVEL_COUNT];
    VkPipelineLayout pipelineLayout;

    UiDrawFn uiDrawFn;
    void*    uiDrawUserdata;
    uint32_t currentFrame;

    bool         uiActive;
    RenderTarget renderTarget;
    SceneTarget  sceneTarget;
} Renderer;


GraphicsResult renderer_init(Renderer* r, Context* ctx, Window* window, AssetManager* assets, Ecs* ecs,
                              RenderTarget renderTarget, UiDrawFn uiDrawFn, void* uiDrawUserdata);
void renderer_free(Renderer* r);
void renderer_clear_gpu_cache(Renderer* r);
void renderer_resize_scene_target(Renderer* r, uint32_t width, uint32_t height);

VkExtent2D renderer_get_extent(const Renderer* r);
void  renderer_draw_frame(Renderer* r, const RenderObject* objects, uint32_t count, mat4 viewproj, vec3 camPos, bool camValid);

#endif