#ifndef REI_IMGUI_H
#define REI_IMGUI_H

#include "rei_vk.h"

typedef struct rei_xcb_window_t rei_xcb_window_t;

typedef struct ImGuiIO ImGuiIO;
typedef struct ImDrawData ImDrawData;
typedef struct ImGuiContext ImGuiContext;

// All IMGUI data required to draw a frame.
typedef struct rei_imgui_frame_t {
  VkPipeline pipeline;
  VkDescriptorSet font_descriptor;
  VkPipelineLayout pipeline_layout;

  // Vertex/index buffers for each frame in flight. Reason for having multiple copies of the same data
  // is to prevent reiicate (maybe not so, but still) syncronization (e.g. waiting for fences) between frames.
  rei_vk_buffer_t index_buffers[2];
  rei_vk_buffer_t vertex_buffers[2];
} rei_imgui_frame_t;

typedef struct rei_imgui_frame_data_ci_t {
  VkRenderPass render_pass;
  VkDescriptorPool descriptor_pool;
  VkDescriptorSetLayout descriptor_layout;
  VkPipelineCache pipeline_cache;
  VkSampler sampler;
  const rei_vk_image_t* texture;
} rei_imgui_frame_data_ci_t;

typedef struct {
  VkSampler font_sampler;
  rei_vk_image_t font_texture;

  ImGuiContext* handle;
} rei_imgui_ctxt_t;

void rei_create_imgui_ctxt (rei_vk_device_t* device, VmaAllocator allocator, const rei_vk_imm_ctxt_t* imm_ctxt, rei_imgui_ctxt_t* out);
void rei_destroy_imgui_ctxt (rei_vk_device_t* device, VmaAllocator allocator, rei_imgui_ctxt_t* ctxt);

void rei_create_imgui_frame_data (
  rei_vk_device_t* device,
  VmaAllocator allocator,
  const rei_imgui_frame_data_ci_t* create_info,
  rei_imgui_frame_t* out
);

void rei_destroy_imgui_frame_data (rei_vk_device_t* device, VmaAllocator allocator, rei_imgui_frame_t* frame_data);

void rei_new_imgui_frame (ImGuiIO* io);
//void rei_handle_imgui_events (ImGuiIO* io, const rei_xcb_window_t* window, const xcb_generic_event_t* event);
void rei_update_imgui (VmaAllocator allocator, rei_imgui_frame_t* frame_data, const ImDrawData* draw_data, u32 frame_index);
void rei_render_imgui_cmd (VkCommandBuffer cmd_buffer, rei_imgui_frame_t* frame_data, const ImDrawData* draw_data, u32 frame_index);

// Widgets.
void rei_imgui_debug_window (ImGuiIO* io);

#endif /* REI_IMGUI_H */
