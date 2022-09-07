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

#ifdef NDEBUG
#  define REI_VK_CHECK(__call) __call
#else
#  define REI_VK_CHECK(__call) do {                                                                       \
     VkResult vk_error = __call;                                                                          \
     if (vk_error) {                                                                                      \
       REI_LOG_ERROR (                                                                                    \
         "%s:%d REI Vulkan error " REI_ANSI_YELLOW "%s" REI_ANSI_RED " occured in " REI_ANSI_YELLOW "%s", \
         __FILE__,                                                                                        \
         __LINE__,                                                                                        \
         rei_vk_show_error (vk_error),                                                                    \
         __FUNCTION__                                                                                     \
       );                                                                                                 \
                                                                                                          \
       exit (EXIT_FAILURE);                                                                               \
     }                                                                                                    \
   } while (0)
#endif

#define REI_VK_BIND_DESCRIPTORS(__cmd_buffer, __pipeline_layout, __count, __descriptors) \
  vkCmdBindDescriptorSets (__cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, __pipeline_layout, 0, __count, __descriptors, 0, NULL);

#define REI_FD_VULKAN_TYPE(__name) typedef struct __name##_T* __name

// Forward declare Vulkan types.
REI_FD_VULKAN_TYPE (VmaAllocator);
REI_FD_VULKAN_TYPE (VmaAllocation);

typedef struct xcb_connection_t xcb_connection_t;

typedef enum rei_vk_buffer_type_e {
  REI_VK_BUFFER_TYPE_STAGING,
  REI_VK_BUFFER_TYPE_IDX_CONST,
  REI_VK_BUFFER_TYPE_VTX_CONST,
  REI_VK_BUFFER_TYPE_IDX_DYNAMIC,
  REI_VK_BUFFER_TYPE_VTX_DYNAMIC,
} rei_vk_buffer_type_e;

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

typedef struct rei_vk_instance_t {
  VkInstance handle;
  #ifndef NDEBUG
  VkDebugUtilsMessengerEXT dbg_messenger;
  #endif
  VkSurfaceKHR surface;
  VkPhysicalDevice gpu;
} rei_vk_instance_t;

typedef struct rei_vk_device_t {
  VkDevice handle;
  VkQueue gfx_queue;
  VkQueue present_queue;
  u32 gfx_index;
  u32 present_index;
} rei_vk_device_t;

typedef struct rei_vk_allocator_t {
  VmaAllocator vma_handle;
  pthread_mutex_t mutex;
} rei_vk_allocator_t;

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

typedef struct rei_vk_swapchain_t {
  VkFormat format;
  u32 image_count;

  struct {
    VkImage* handles;
    VkImageView* views;
    rei_vk_image_t depth_image;
  }* images;

  u32 width, height;

  VkSwapchainKHR handle;
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

// Stringify VkResult for debugging purposes.
const char* rei_vk_show_error (VkResult);

void rei_vk_create_instance_linux (xcb_connection_t* xcb_conn, u32 xcb_window, rei_vk_instance_t* out);
void rei_vk_destroy_instance (rei_vk_instance_t* instance);

void rei_vk_create_device (const rei_vk_instance_t* instance, rei_vk_device_t* out);

void rei_vk_create_allocator (const rei_vk_instance_t* instance, const rei_vk_device_t* device, rei_vk_allocator_t* out);
void rei_vk_destroy_allocator (rei_vk_allocator_t* allocator);

void rei_vk_flush_buffers (rei_vk_allocator_t* allocator, u32 count, rei_vk_buffer_t* buffers);

void rei_vk_create_image (
  const rei_vk_device_t* device,
  rei_vk_allocator_t* allocator,
  const rei_vk_image_ci_t* create_info,
  rei_vk_image_t* out
);

void rei_vk_destroy_image (const rei_vk_device_t* device, rei_vk_allocator_t* allocator, rei_vk_image_t* image);

void rei_vk_create_buffer (rei_vk_allocator_t* allocator, u64 size, rei_vk_buffer_type_e type, rei_vk_buffer_t* out);

void rei_vk_map_buffer (rei_vk_allocator_t* allocator, rei_vk_buffer_t* buffer);
void rei_vk_unmap_buffer (rei_vk_allocator_t* allocator, rei_vk_buffer_t* buffer);

void rei_vk_destroy_buffer (rei_vk_allocator_t* allocator, rei_vk_buffer_t* buffer);

void rei_vk_create_swapchain (
  const rei_vk_instance_t* instance,
  const rei_vk_device_t* device,
  rei_vk_allocator_t* allocator,
  VkSwapchainKHR old,
  u32 width,
  u32 height,
  rei_vk_swapchain_t* out
);

void rei_vk_destroy_swapchain (const rei_vk_device_t* device, rei_vk_allocator_t* allocator, rei_vk_swapchain_t* swapchain);

void rei_vk_create_render_pass (
  const rei_vk_device_t* device,
  const rei_vk_swapchain_t* swapchain,
  const rei_vec4_u* clear_color,
  rei_vk_render_pass_t* out
);

void rei_vk_destroy_render_pass (const rei_vk_device_t* device, rei_vk_render_pass_t* render_pass);

void rei_vk_create_cmd_pool (const rei_vk_device_t* device, u32 queue_index, VkCommandPoolCreateFlags flags, VkCommandPool* out);

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

void rei_vk_create_pipeline_layout (
  const rei_vk_device_t* device,
  u32 desc_count,
  const VkDescriptorSetLayout* desc_layouts,
  u32 push_count,
  const VkPushConstantRange* push_constants,
  VkPipelineLayout* out
);

void rei_vk_create_gfx_pipeline (const rei_vk_device_t* device, const rei_vk_gfx_pipeline_ci_t* create_info, VkPipeline* out);

void rei_vk_create_imm_ctxt (const rei_vk_device_t* device, u32 queue_index, rei_vk_imm_ctxt_t* out);
void rei_vk_destroy_imm_ctxt (const rei_vk_device_t* device, rei_vk_imm_ctxt_t* context);

void rei_vk_start_imm_cmd (const rei_vk_device_t* device, const rei_vk_imm_ctxt_t* context, VkCommandBuffer* out);

void rei_vk_transition_image_cmd (VkCommandBuffer cmd_buffer, const rei_vk_image_trans_info_t* trans_info, VkImage image);

void rei_vk_copy_buffer_cmd (
  VkCommandBuffer cmd_buffer,
  u64 size,
  u64 src_offset,
  const rei_vk_buffer_t* restrict src,
  rei_vk_buffer_t* restrict dst
);

void rei_vk_copy_buffer_to_image_cmd (
  VkCommandBuffer cmd_buffer,
  const rei_vk_buffer_t* src,
  rei_vk_image_t* dst,
  u32 mip_level,
  u32 width,
  u32 height
);

// Decompress and create a texture loaded from a rtex file.
void rei_vk_create_texture_cmd (
  const rei_vk_device_t* device,
  rei_vk_allocator_t* allocator,
  VkCommandBuffer cmd_buffer,
  rei_vk_buffer_t* staging_buffer,
  const rei_texture_t* src,
  rei_vk_image_t* out
);

void rei_vk_end_imm_cmd (const rei_vk_device_t* device, const rei_vk_imm_ctxt_t* context, VkCommandBuffer cmd_buffer);

void rei_vk_create_sampler (const rei_vk_device_t* device, f32 min_lod, f32 max_lod, VkFilter filter, VkSampler* out);

// Create texture from raw pixels in RGBA format.
void rei_vk_create_texture_raw (
  const rei_vk_device_t* device,
  rei_vk_allocator_t* allocator,
  const rei_vk_imm_ctxt_t* context,
  u32 width,
  u32 height,
  const u8* src,
  rei_vk_image_t* out
);

void rei_vk_create_descriptor_layout (
  const rei_vk_device_t* device,
  u32 bind_count,
  const VkDescriptorSetLayoutBinding* bindings,
  VkDescriptorSetLayout* out
);

void rei_vk_create_descriptor_pool (
  const rei_vk_device_t* device,
  u32 max_count,
  u32 size_count,
  const VkDescriptorPoolSize* sizes,
  VkDescriptorPool* out
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
