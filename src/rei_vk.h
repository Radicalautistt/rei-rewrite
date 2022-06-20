#ifndef REI_VK_H
#define REI_VK_H

#include <stdlib.h>

#include <volk/volk.h>

#include "rei_logger.h"

#define REI_VK_VERSION VK_API_VERSION_1_0
#define REI_VK_IMAGE_FORMAT VK_FORMAT_B8G8R8A8_SRGB
#define REI_VK_TEXTURE_FORMAT VK_FORMAT_R8G8B8A8_SRGB
#define REI_VK_DEPTH_FORMAT VK_FORMAT_X8_D24_UNORM_PACK32

#define REI_VK_DEBUG_MESSENGER_CI(name)                                                                                   \
    VkDebugUtilsMessengerCreateInfoEXT name = {                                                                           \
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,                                                   \
      .pNext = NULL,                                                                                                      \
      .pUserData = NULL,                                                                                                  \
      .flags = 0,                                                                                                         \
      .pfnUserCallback = rei_vk_debug_callback,                                                                           \
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,    \
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT, \
    }

#ifdef NDEBUG
#  define REI_VK_CHECK(call) call
#else
#  define REI_VK_CHECK(call) do {                                                                         \
     VkResult error = call;                                                                               \
     if (error) {                                                                                         \
       REI_LOG_ERROR (                                                                                    \
         "%s:%d REI Vulkan error " REI_ANSI_YELLOW "%s" REI_ANSI_RED " occured in " REI_ANSI_YELLOW "%s", \
         __FILE__,                                                                                        \
         __LINE__,                                                                                        \
         rei_vk_show_error (error),                                                                       \
         __FUNCTION__                                                                                     \
       );                                                                                                 \
                                                                                                          \
       exit (EXIT_FAILURE);                                                                               \
     }                                                                                                    \
   } while (0)
#endif

#define REI_FD_VULKAN_TYPE(name) typedef struct name##_T* name

// Forward declare Vulkan types.
REI_FD_VULKAN_TYPE (VmaAllocator);
REI_FD_VULKAN_TYPE (VmaAllocation);

// Create infos for Vulkan objects.
typedef struct rei_vk_image_ci_t {
  u32 width;
  u32 height;
  u32 mip_levels;
  VkFormat format;
  VkImageUsageFlags usage;
  VkImageAspectFlags aspect_mask;
} rei_vk_image_ci_t;

typedef struct rei_vk_gfx_pipeline_ci_t {
  VkPipelineCache cache;
  VkRenderPass render_pass;
  VkPipelineLayout layout;

  u32 subpass_index;
  u32 color_blend_attachment_count;

  const char* pixel_shader_path;
  const char* vertex_shader_path;

  VkPipelineDynamicStateCreateInfo* dynamic_state;
  VkPipelineViewportStateCreateInfo* viewport_state;
  VkPipelineVertexInputStateCreateInfo* vertex_input_state;
  VkPipelineDepthStencilStateCreateInfo* depth_stencil_state;
  VkPipelineColorBlendAttachmentState* color_blend_attachments;
  VkPipelineRasterizationStateCreateInfo* rasterization_state;
} rei_vk_gfx_pipeline_ci_t;

typedef struct rei_vk_image_trans_info_t {
  VkImageLayout old_layout;
  VkImageLayout new_layout;
  VkPipelineStageFlags src_stage;
  VkPipelineStageFlags dst_stage;
  VkImageSubresourceRange* subresource_range;
} rei_vk_image_trans_info_t;

typedef struct rei_vk_queue_indices_t {
  u32 gfx, present, transfer;
} rei_vk_queue_indices_t;

typedef struct rei_vk_device_t {
  VkDevice handle;
  VkQueue gfx_queue;
  VkQueue present_queue;
  u32 gfx_index;
  u32 present_index;
} rei_vk_device_t;

typedef struct rei_vk_image_t {
  VkImage handle;
  VkImageView view;
  VmaAllocation memory;
} rei_vk_image_t;

typedef struct rei_vk_buffer_t {
  void* mapped;
  VkBuffer handle;
  u64 size;
  VmaAllocation memory;
} rei_vk_buffer_t;

typedef struct rei_vk_swapchain_t {
  VkFormat format;
  u32 image_count;
  VkImage* images;
  VkImageView* views;

  u32 width, height;

  VkSwapchainKHR handle;
  rei_vk_image_t depth_image;
} rei_vk_swapchain_t;

// Context for creation and submition of immediate commands (ones that stall gpu until their completion).
typedef struct rei_vk_imm_ctxt_t {
  VkQueue queue;
  VkFence fence;
  VkCommandPool cmd_pool;
} rei_vk_imm_ctxt_t;

// Stringify VkResult for debugging purposes.
const char* rei_vk_show_error (VkResult);

VKAPI_ATTR VkBool32 VKAPI_CALL rei_vk_debug_callback (
  VkDebugUtilsMessageSeverityFlagBitsEXT severity,
  VkDebugUtilsMessageTypeFlagsEXT type,
  const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
  void* user_data
);

void rei_vk_create_instance (const char* const* required_ext, u32 required_ext_count, VkInstance* out);

b8 rei_vk_find_queue_indices (VkPhysicalDevice device, VkSurfaceKHR surface, rei_vk_queue_indices_t* out);

void rei_vk_choose_gpu (
  VkInstance instance,
  VkSurfaceKHR surface,
  const char* const* required_ext,
  u32 required_ext_count,
  VkPhysicalDevice* out
);

void rei_vk_create_device (
  VkPhysicalDevice physical_device,
  VkSurfaceKHR surface,
  const char* const* enabled_ext,
  u32 enabled_ext_count,
  rei_vk_device_t* out
);

void rei_vk_create_image (rei_vk_device_t* device, VmaAllocator allocator, const rei_vk_image_ci_t* create_info, rei_vk_image_t* out);
void rei_vk_destroy_image (rei_vk_device_t* device, VmaAllocator allocator, rei_vk_image_t* image);

void rei_vk_create_buffer (
  VmaAllocator allocator,
  u64 size,
  VkBufferUsageFlags usage,
  u64 vma_memory_usage,
  VkMemoryPropertyFlags memory_flags,
  rei_vk_buffer_t* out
);

void rei_vk_destroy_buffer (VmaAllocator allocator, rei_vk_buffer_t* buffer);

void rei_vk_create_swapchain (
  rei_vk_device_t* device,
  VmaAllocator allocator,
  VkSwapchainKHR old,
  VkSurfaceKHR surface,
  u32 width,
  u32 height,
  VkPhysicalDevice physical_device,
  rei_vk_swapchain_t* out
);

void rei_vk_destroy_swapchain (rei_vk_device_t* device, VmaAllocator allocator, rei_vk_swapchain_t* swapchain);

void rei_vk_create_shader_module (rei_vk_device_t* device, const char* relative_path, VkShaderModule* out);
void rei_vk_create_gfx_pipeline (rei_vk_device_t* device, const rei_vk_gfx_pipeline_ci_t* create_info, VkPipeline* out);

void rei_vk_create_imm_ctxt (rei_vk_device_t* device, u32 queue_index, rei_vk_imm_ctxt_t* out);
void rei_vk_destroy_imm_ctxt (rei_vk_device_t* device, rei_vk_imm_ctxt_t* context);
void rei_vk_start_imm_cmd (rei_vk_device_t* device, const rei_vk_imm_ctxt_t* context, VkCommandBuffer* out);
void rei_vk_end_imm_cmd (rei_vk_device_t* device, const rei_vk_imm_ctxt_t* context, VkCommandBuffer cmd_buffer);
void rei_vk_transition_image_cmd (VkCommandBuffer cmd_buffer, const rei_vk_image_trans_info_t* trans_info, VkImage image);

void rei_vk_create_sampler (rei_vk_device_t* device, f32 min_lod, f32 max_lod, VkFilter filter, VkSampler* out);

void rei_vk_create_texture (
  rei_vk_device_t* device,
  VmaAllocator allocator,
  const rei_vk_imm_ctxt_t* context,
  u8* pixels,
  u32 width,
  u32 height,
  rei_vk_image_t* out
);

void rei_vk_create_texture_mipmapped (
  rei_vk_device_t* device,
  VmaAllocator allocator,
  const rei_vk_imm_ctxt_t* context,
  u8* pixels,
  u32 width,
  u32 height,
  rei_vk_image_t* out
);

#endif /* REI_VK_H */
