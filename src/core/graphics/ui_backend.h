#pragma once
#include <vulkan/vulkan.h>

typedef struct GLFWwindow GLFWwindow;

#ifdef __cplusplus
extern "C" {
#endif

bool ui_init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VkQueue queue, uint32_t min_image_count, uint32_t image_count, VkFormat color_format);
void ui_new_frame(void);
void ui_render(VkCommandBuffer command_buffer);
void ui_shutdown(void);

void*  ui_add_texture(VkSampler sampler, VkImageView view, VkImageLayout layout);
void   ui_remove_texture(void* descriptorSet);

#ifdef __cplusplus
}
#endif