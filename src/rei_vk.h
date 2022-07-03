#ifndef REI_VK_H
#define REI_VK_H

#include <stdlib.h>

#ifdef __linux__
#  define VK_USE_PLATFORM_XCB_KHR
#else
#  error "Unhandled platform..."
#endif

#include <volk/volk.h>

#include "rei_asset.h"
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

#define REI_VK_BIND_DESCRIPTORS(__cmd_buffer, __pipeline_layout, __count, __descriptors) \
  vkCmdBindDescriptorSets (__cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, __pipeline_layout, 0, __count, __descriptors, 0, NULL);

#define REI_FD_VULKAN_TYPE(name) typedef struct name##_T* name

// Forward declare Vulkan types.
REI_FD_VULKAN_TYPE (VmaAllocator);
REI_FD_VULKAN_TYPE (VmaAllocation);

typedef struct xcb_connection_t xcb_connection_t;

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

typedef struct rei_vk_render_pass_t {
  VkRenderPass handle;
  VkFramebuffer* framebuffers;
  u32 framebuffer_count;
  u32 clear_value_count;
  VkClearValue* clear_values;
} rei_vk_render_pass_t;

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

typedef struct rei_vk_dynamic_buffer_t {
  u64 current_offset;
  rei_vk_buffer_t buffer;
} rei_vk_dynamic_buffer_t;

typedef struct rei_vk_swapchain_t {
  VkFormat format;
  u32 image_count;
  VkImage* images;
  VkImageView* views;

  u32 width, height;

  VkSwapchainKHR handle;
  rei_vk_image_t depth_image;
} rei_vk_swapchain_t;

typedef struct rei_vk_frame_data_t {
  VkCommandBuffer cmd_buffer;
  VkFence submit_fence;
  VkSemaphore present_semaphore;
  VkSemaphore render_semaphore;
} rei_vk_frame_data_t;

// Context for creation and submition of immediate commands (ones that stall gpu until their completion).
typedef struct rei_vk_imm_ctxt_t {
  VkQueue queue;
  VkFence fence;
  VkCommandPool cmd_pool;
} rei_vk_imm_ctxt_t;

// Create infos for Vulkan objects.
typedef struct rei_vk_image_ci_t {
  u32 width;
  u32 height;
  u32 mip_levels;
  VkFormat format;
  VkImageUsageFlags usage;
  VkImageAspectFlags aspect_mask;
} rei_vk_image_ci_t;

typedef struct rei_vk_render_pass_ci_t {
  f32 r;
  f32 g;
  f32 b;
  f32 a;
  const rei_vk_swapchain_t* swapchain;
} rei_vk_render_pass_ci_t;

// Stringify VkResult for debugging purposes.
const char* rei_vk_show_error (VkResult);

VKAPI_ATTR VkBool32 VKAPI_CALL rei_vk_debug_callback (
  VkDebugUtilsMessageSeverityFlagBitsEXT severity,
  VkDebugUtilsMessageTypeFlagsEXT type,
  const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
  void* user_data
);

u32 rei_vk_check_extensions (const VkExtensionProperties* available, u32 available_count, const char* const* required, u32 required_count);

void rei_vk_create_instance (const char* const* required_ext, u32 required_ext_count, VkInstance* out);

b8 rei_vk_find_queue_indices (VkPhysicalDevice device, VkSurfaceKHR surface, rei_vk_queue_indices_t* out);

void rei_vk_choose_gpu (
  VkInstance instance,
  VkSurfaceKHR surface,
  const char* const* required_ext,
  u32 required_ext_count,
  VkPhysicalDevice* out
);

#ifdef __linux__
   void rei_vk_create_xcb_surface (VkInstance instance, u32 window_handle, xcb_connection_t* xcb_connection, VkSurfaceKHR* out);
#else
#  error "Unhandled platform..."
#endif

void rei_vk_create_device (
  VkPhysicalDevice physical_device,
  VkSurfaceKHR surface,
  const char* const* enabled_ext,
  u32 enabled_ext_count,
  rei_vk_device_t* out
);

void rei_vk_create_allocator (VkInstance instance, VkPhysicalDevice physical_device, const rei_vk_device_t* device, VmaAllocator* out);

void rei_vk_create_image (const rei_vk_device_t* device, VmaAllocator allocator, const rei_vk_image_ci_t* create_info, rei_vk_image_t* out);
void rei_vk_destroy_image (const rei_vk_device_t* device, VmaAllocator allocator, rei_vk_image_t* image);

void rei_vk_create_buffer (
  VmaAllocator allocator,
  u64 size,
  VkBufferUsageFlags usage,
  u64 vma_memory_usage,
  VkMemoryPropertyFlags memory_flags,
  rei_vk_buffer_t* out
);

void rei_vk_map_buffer (VmaAllocator allocator, rei_vk_buffer_t* buffer);
void rei_vk_unmap_buffer (VmaAllocator allocator, rei_vk_buffer_t* buffer);

void rei_vk_destroy_buffer (VmaAllocator allocator, rei_vk_buffer_t* buffer);

void rei_vk_create_swapchain (
  const rei_vk_device_t* device,
  VmaAllocator allocator,
  VkSwapchainKHR old,
  VkSurfaceKHR surface,
  u32 width,
  u32 height,
  VkPhysicalDevice physical_device,
  rei_vk_swapchain_t* out
);

void rei_vk_destroy_swapchain (const rei_vk_device_t* device, VmaAllocator allocator, rei_vk_swapchain_t* swapchain);

void rei_vk_create_render_pass (const rei_vk_device_t* device, const rei_vk_render_pass_ci_t* create_info, rei_vk_render_pass_t* out);
void rei_vk_destroy_render_pass (const rei_vk_device_t* device, rei_vk_render_pass_t* render_pass);

void rei_vk_create_frame_data (const rei_vk_device_t* device, VkCommandPool cmd_pool, rei_vk_frame_data_t* out);

u32 rei_vk_begin_frame (
  const rei_vk_device_t* device,
  const rei_vk_render_pass_t* render_pass,
  const rei_vk_frame_data_t* current_frame,
  const rei_vk_swapchain_t* swapchain,
  VkCommandBuffer* out
);

void rei_vk_end_frame (
  const rei_vk_device_t* device,
  const rei_vk_frame_data_t* current_frame,
  const rei_vk_swapchain_t* swapchain,
  u32 image_index
);

void rei_vk_destroy_frame_data (const rei_vk_device_t* device, rei_vk_frame_data_t* frame_data);

void rei_vk_create_shader_module (const rei_vk_device_t* device, const char* relative_path, VkShaderModule* out);
void rei_vk_create_gfx_pipeline (const rei_vk_device_t* device, const rei_vk_gfx_pipeline_ci_t* create_info, VkPipeline* out);

void rei_vk_create_imm_ctxt (const rei_vk_device_t* device, u32 queue_index, rei_vk_imm_ctxt_t* out);
void rei_vk_destroy_imm_ctxt (const rei_vk_device_t* device, rei_vk_imm_ctxt_t* context);
void rei_vk_start_imm_cmd (const rei_vk_device_t* device, const rei_vk_imm_ctxt_t* context, VkCommandBuffer* out);

void rei_vk_transition_image_cmd (VkCommandBuffer cmd_buffer, const rei_vk_image_trans_info_t* trans_info, VkImage image);
void rei_vk_copy_buffer_cmd (VkCommandBuffer cmd_buffer, u64 size, u64 src_offset, const rei_vk_buffer_t* src, rei_vk_buffer_t* dst);

void rei_vk_end_imm_cmd (const rei_vk_device_t* device, const rei_vk_imm_ctxt_t* context, VkCommandBuffer cmd_buffer);

void rei_vk_create_sampler (const rei_vk_device_t* device, f32 min_lod, f32 max_lod, VkFilter filter, VkSampler* out);

void rei_vk_create_texture (
  const rei_vk_device_t* device,
  VmaAllocator allocator,
  const rei_vk_imm_ctxt_t* context,
  const rei_texture_t* src,
  rei_vk_image_t* out
);

void rei_vk_create_texture_mipmapped (
  const rei_vk_device_t* device,
  VmaAllocator allocator,
  const rei_vk_imm_ctxt_t* context,
  const rei_image_t* src,
  rei_vk_image_t* out
);

void rei_vk_allocate_descriptors (
  const rei_vk_device_t* device,
  VkDescriptorPool pool,
  VkDescriptorSetLayout layout,
  u32 count,
  VkDescriptorSet* out
);

void rei_vk_write_image_descriptors (
  const rei_vk_device_t* device,
  VkSampler sampler,
  const VkImageView* src_views,
  u32 count,
  VkDescriptorSet* descriptors
);

#endif /* REI_VK_H */
