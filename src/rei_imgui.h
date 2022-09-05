#ifndef REI_IMGUI_H
#define REI_IMGUI_H

#include "rei_vk.h"
#include "rei_defines.h"

#ifndef CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#  define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#endif

#include <cimgui/cimgui.h>

typedef struct rei_xcb_window_t rei_xcb_window_t;

typedef struct ImGuiIO ImGuiIO;
typedef struct ImDrawData ImDrawData;
typedef struct ImGuiContext ImGuiContext;

// All data required to draw an IMGUI frame.
typedef struct rei_imgui_frame_data_t {
  VkPipeline pipeline;
  VkDescriptorSet font_descriptor;
  VkPipelineLayout pipeline_layout;

  // Vertex/index buffers for each frame in flight. Reason for having multiple copies of the same data
  // is to prevent reiicate (maybe not so, but still) syncronization (e.g. waiting for fences) between frames.
  struct {rei_vk_buffer_t vtx_buffer, idx_buffer;}* buffers[2];
} rei_imgui_frame_data_t;

typedef struct {
  rei_vk_image_t font_texture;

  ImGuiContext* handle;
} rei_imgui_ctxt_t;

void rei_create_imgui_ctxt (
  const rei_vk_device_t* vk_device,
  rei_vk_allocator_t* vk_allocator,
  const rei_vk_imm_ctxt_t* vk_imm_ctxt,
  rei_imgui_ctxt_t* out
);

void rei_destroy_imgui_ctxt (const rei_vk_device_t* vk_device, rei_vk_allocator_t* vk_allocator, rei_imgui_ctxt_t* ctxt);

void rei_imgui_create_frame_data (
  const rei_vk_device_t* vk_device,
  rei_vk_allocator_t* vk_allocator,
  const rei_vk_render_pass_t* vk_render_pass,
  VkDescriptorPool vk_desc_pool,
  VkDescriptorSetLayout vk_desc_layout,
  VkSampler vk_text_sampler,
  const rei_imgui_ctxt_t* imgui_ctxt,
  rei_imgui_frame_data_t* out
);

void rei_imgui_destroy_frame_data (const rei_vk_device_t* vk_device, rei_vk_allocator_t* vk_allocator, rei_imgui_frame_data_t* frame_data);

void rei_imgui_new_frame (ImGuiIO* io);
void rei_imgui_handle_events (ImGuiIO* io, const rei_xcb_window_t* window, const xcb_generic_event_t* event);
void rei_imgui_update_buffers (rei_vk_allocator_t* vk_allocator, rei_imgui_frame_data_t* buffers, const ImDrawData* draw_data, u32 frame_index);

void rei_imgui_draw_cmd (VkCommandBuffer vk_cmd_buffer, const rei_imgui_frame_data_t* frame_data, const ImDrawData* draw_data, u32 frame_index);

// Widgets.
void rei_imgui_debug_window_wid (ImGuiIO* io);

#endif /* REI_IMGUI_H */
