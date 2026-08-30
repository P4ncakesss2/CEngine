#include "ui_backend.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

static VkDescriptorPool g_UiDescriptorPool = VK_NULL_HANDLE;
static VkDevice g_UiDevice = VK_NULL_HANDLE;

extern "C" {

bool ui_init(
    GLFWwindow* window,
    VkInstance instance,
    VkPhysicalDevice physical_device,
    VkDevice device,
    VkQueue queue,
    uint32_t min_image_count,
    uint32_t image_count,
    VkFormat color_format
) {
    g_UiDevice = device;
    constexpr uint32_t kUiMaxDescriptorSets = 32;

    VkDescriptorPoolSize pool_sizes[] = {
        {
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            kUiMaxDescriptorSets
        },
        {
            VK_DESCRIPTOR_TYPE_SAMPLER,
            kUiMaxDescriptorSets
        }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = kUiMaxDescriptorSets;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;

    VkResult result = vkCreateDescriptorPool(
        device,
        &pool_info,
        nullptr,
        &g_UiDescriptorPool
    );

    if (result != VK_SUCCESS) {
        g_UiDescriptorPool = VK_NULL_HANDLE;
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};

    init_info.Instance = instance;
    init_info.PhysicalDevice = physical_device;
    init_info.Device = device;
    init_info.Queue = queue;

    init_info.DescriptorPool = g_UiDescriptorPool;

    init_info.MinImageCount = min_image_count;
    init_info.ImageCount = image_count;

    init_info.UseDynamicRendering = true;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;

    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats =
        &color_format;

    ImGui_ImplVulkan_Init(&init_info);

    return true;
}

void ui_new_frame(void)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();
}

void ui_render(VkCommandBuffer command_buffer)
{
    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();

    ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer);
}

void ui_shutdown(void) {
    if (g_UiDescriptorPool == VK_NULL_HANDLE) {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    ImGui::DestroyContext();

    vkDestroyDescriptorPool(
        g_UiDevice,
        g_UiDescriptorPool,
        nullptr
    );

    g_UiDescriptorPool = VK_NULL_HANDLE;
    g_UiDevice = VK_NULL_HANDLE;
}

void* ui_add_texture(VkSampler sampler, VkImageView view, VkImageLayout layout) {
    return (void*)ImGui_ImplVulkan_AddTexture(sampler, view, layout);
}

void ui_remove_texture(void* descriptorSet) {
    ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)descriptorSet);
}

}