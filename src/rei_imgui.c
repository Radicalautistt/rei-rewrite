#include <memory.h>

#include "rei_imgui.h"
#include "rei_window.h"

void rei_imgui_create_ctxt (
  const rei_vk_device_t* vk_device,
  rei_vk_allocator_t* vk_allocator,
  const rei_vk_imm_ctxt_t* vk_imm_ctxt,
  rei_imgui_ctxt_t* out) {

  out->handle = igCreateContext (NULL);

  { // Create font texture.
    u8* pixels;
    s32 width, height;

    const ImGuiIO* io = igGetIO ();
    ImFontAtlas_GetTexDataAsRGBA32 (io->Fonts, &pixels, &width, &height, NULL);

    rei_vk_create_texture_raw (vk_device, vk_allocator, vk_imm_ctxt, (u32) width, (u32) height, pixels, &out->font_texture);
    ImFontAtlas_ClearTexData (io->Fonts);
    ImFontAtlas_SetTexID (io->Fonts, (void*) ((u64) out->font_texture.handle));
  }

  // Set IMGUI theme.
  ImGuiStyle* style = igGetStyle ();
  igStyleColorsClassic (style);

  style->WindowRounding = 0.f;
  style->ScrollbarRounding = 0.f;
  style->Colors[ImGuiCol_WindowBg].w = 1.f;

  #define _S_SET_BLACK_COLOR(__color) \
    style->Colors[__color].x = 0.f;  \
    style->Colors[__color].y = 0.f;  \
    style->Colors[__color].z = 0.f;  \
    style->Colors[__color].w = 1.f

  _S_SET_BLACK_COLOR (ImGuiCol_TitleBg);
  _S_SET_BLACK_COLOR (ImGuiCol_ScrollbarBg);
  _S_SET_BLACK_COLOR (ImGuiCol_TitleBgActive);

  #undef _S_SET_BLACK_COLOR
}

void rei_imgui_destroy_ctxt (const rei_vk_device_t* vk_device, rei_vk_allocator_t* vk_allocator, rei_imgui_ctxt_t* ctxt) {
  rei_vk_destroy_image (vk_device, vk_allocator, &ctxt->font_texture);

  igDestroyContext (ctxt->handle);
}

void rei_imgui_create_frame_data (
  const rei_vk_device_t* vk_device,
  rei_vk_allocator_t* vk_allocator,
  const rei_vk_render_pass_t* vk_render_pass,
  VkDescriptorPool vk_desc_pool,
  VkDescriptorSetLayout vk_desc_layout,
  VkSampler vk_text_sampler,
  const rei_imgui_ctxt_t* imgui_ctxt,
  rei_imgui_frame_data_t* out) {

  // Create dummy buffers (destroyed on the first frame) to elude (buffer->handle != VK_NULL_HANDLE) check,
  // which would otherwise be mandatory to do before buffer deletion.
  for (u32 i = 0; i < REI_VK_FRAME_COUNT; ++i) {
    out->buffers[i] = malloc (sizeof **out->buffers);
    rei_vk_create_buffer (vk_allocator, 1, REI_VK_BUFFER_TYPE_VTX_DYNAMIC, &out->buffers[i]->vtx_buffer);
    rei_vk_create_buffer (vk_allocator, 1, REI_VK_BUFFER_TYPE_IDX_DYNAMIC, &out->buffers[i]->idx_buffer);

    rei_vk_map_buffer (vk_allocator, &out->buffers[i]->vtx_buffer);
    rei_vk_map_buffer (vk_allocator, &out->buffers[i]->idx_buffer);
  }

  // Allocate descriptors.
  rei_vk_allocate_descriptors (vk_device, vk_desc_pool, vk_desc_layout, 1, &out->font_descriptor);
  rei_vk_write_image_descriptors (vk_device, vk_text_sampler, &imgui_ctxt->font_texture.view, 1, &out->font_descriptor);

  // Create graphics pipeline.
  rei_vk_create_pipeline_layout (
    vk_device,
    1,
    &vk_desc_layout,
    1,
    &(const VkPushConstantRange) {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof (rei_vec2_t)},
    &out->pipeline_layout
  );

  VkVertexInputBindingDescription binding = {
    .binding = 0,
    .stride = sizeof (ImDrawVert),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
  };

  VkVertexInputAttributeDescription vertex_attributes[3] = {
    [0] = {
      .binding = 0,
      .location = 0,
      .format = VK_FORMAT_R32G32_SFLOAT,
      .offset = REI_OFFSET_OF (ImDrawVert, pos),
    },

    [1] = {
      .binding = 0,
      .location = 1,
      .format = VK_FORMAT_R32G32_SFLOAT,
      .offset = REI_OFFSET_OF (ImDrawVert, uv)
    },

    [2] = {
      .binding = 0,
      .location = 2,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .offset = REI_OFFSET_OF (ImDrawVert, col)
    }
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &binding,
    .pVertexAttributeDescriptions = vertex_attributes,
    .vertexAttributeDescriptionCount = REI_ARRAY_SIZE (vertex_attributes),
  };

  VkViewport viewport = {
    .x = 0,
    .y = 0,
    .width = 1680,
    .height = 1050,
    .minDepth = 0.f,
    .maxDepth = 1.f,
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .scissorCount = 1,
    .viewportCount = 1,
    .pViewports = &viewport
  };

  VkDynamicState dynamic_states[1] = {VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamic_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = 1,
    .pDynamicStates = dynamic_states
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .lineWidth = 1.f,
    .cullMode = VK_CULL_MODE_NONE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .minDepthBounds = 0.f,
    .maxDepthBounds = 1.f,
    .depthCompareOp = VK_COMPARE_OP_ALWAYS
  };

  VkPipelineColorBlendAttachmentState color_blend_attachment = {
    .blendEnable = VK_TRUE,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT
      | VK_COLOR_COMPONENT_G_BIT
      | VK_COLOR_COMPONENT_B_BIT
      | VK_COLOR_COMPONENT_A_BIT,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
  };

  rei_vk_gfx_pipeline_ci_t info = {
    .color_blend_attachment_count = 1,
    .layout = out->pipeline_layout,
    .cache = VK_NULL_HANDLE,
    .render_pass = vk_render_pass->handle,
    .pixel_shader_path = "shaders/imgui.frag.spv",
    .vertex_shader_path = "shaders/imgui.vert.spv",

    .dynamic_state = &dynamic_state,
    .viewport_state = &viewport_state,
    .vertex_input_state = &vertex_input_state,
    .depth_stencil_state = &depth_stencil_state,
    .rasterization_state = &rasterization_state,
    .color_blend_attachments = &color_blend_attachment
  };

  rei_vk_create_gfx_pipeline (vk_device, &info, &out->pipeline);
}

void rei_imgui_destroy_frame_data (const rei_vk_device_t* vk_device, rei_vk_allocator_t* vk_allocator, rei_imgui_frame_data_t* frame_data) {
  vkDestroyPipeline (vk_device->handle, frame_data->pipeline, NULL);
  vkDestroyPipelineLayout (vk_device->handle, frame_data->pipeline_layout, NULL);

  for (u32 i = 0; i < REI_VK_FRAME_COUNT; ++i) {
    rei_vk_buffer_t* idx_buffer = &frame_data->buffers[i]->idx_buffer;
    rei_vk_buffer_t* vtx_buffer = &frame_data->buffers[i]->vtx_buffer;

    rei_vk_unmap_buffer (vk_allocator, idx_buffer);
    rei_vk_unmap_buffer (vk_allocator, vtx_buffer);

    rei_vk_destroy_buffer (vk_allocator, idx_buffer);
    rei_vk_destroy_buffer (vk_allocator, vtx_buffer);

    free (frame_data->buffers[i]);
  }
}

void rei_imgui_new_frame (ImGuiIO* io) {
  // FIXME hardcoded values
  io->DisplaySize.x = 1366.f;
  io->DisplaySize.y = 768.f;

  igNewFrame ();
}

void rei_imgui_handle_events (ImGuiIO* io, const rei_xcb_window_t* window, const xcb_generic_event_t* event) {
  switch (event->response_type) {
    case XCB_MOTION_NOTIFY: rei_xcb_get_mouse_pos (window, &io->MousePos.x); return;

    case XCB_BUTTON_PRESS: {
      switch (((xcb_button_press_event_t*) event)->detail) {
        case REI_X11_MOUSE_LEFT: io->MouseDown[0] = REI_TRUE; return;
        case REI_X11_MOUSE_RIGHT: io->MouseDown[1] = REI_TRUE; return;
	      case REI_X11_MOUSE_WHEEL_UP: io->MouseWheel = 1.f; return;
	      case REI_X11_MOUSE_WHEEL_DOWN: io->MouseWheel = -1.f; return;
	      default: return;
      }
    } break;

    case XCB_BUTTON_RELEASE: {
      switch (((xcb_button_press_event_t*) event)->detail) {
        case REI_X11_MOUSE_LEFT: io->MouseDown[0] = REI_FALSE; return;
        case REI_X11_MOUSE_RIGHT: io->MouseDown[1] = REI_FALSE; return;
	      default: return;
      }
    } break;
  }
}

void rei_imgui_update_buffers (
  rei_vk_allocator_t* vk_allocator,
  rei_imgui_frame_data_t* frame_data,
  const ImDrawData* draw_data,
  u32 frame_index) {

  const u64 new_idx_buffer_size = sizeof (ImDrawIdx) * (u64) draw_data->TotalIdxCount;
  const u64 new_vtx_buffer_size = sizeof (ImDrawVert) * (u64) draw_data->TotalVtxCount;

  rei_vk_buffer_t* idx_buffer = &frame_data->buffers[frame_index]->idx_buffer;
  rei_vk_buffer_t* vtx_buffer = &frame_data->buffers[frame_index]->vtx_buffer;

  // Recreate buffers if their sizes are insufficient to hold IMGUI geometry.
  if (new_vtx_buffer_size > vtx_buffer->size) {
    rei_vk_unmap_buffer (vk_allocator, vtx_buffer);
    rei_vk_destroy_buffer (vk_allocator, vtx_buffer);

    rei_vk_create_buffer (vk_allocator, new_vtx_buffer_size, REI_VK_BUFFER_TYPE_VTX_DYNAMIC, vtx_buffer);
    rei_vk_map_buffer (vk_allocator, vtx_buffer);
  }

  if (new_idx_buffer_size > idx_buffer->size) {
    rei_vk_unmap_buffer (vk_allocator, idx_buffer);
    rei_vk_destroy_buffer (vk_allocator, idx_buffer);

    rei_vk_create_buffer (vk_allocator, new_idx_buffer_size, REI_VK_BUFFER_TYPE_IDX_DYNAMIC, idx_buffer);
    rei_vk_map_buffer (vk_allocator, idx_buffer);
  }

  // Write IMGUI data to buffers.
  ImDrawVert* vertices = (ImDrawVert*) vtx_buffer->mapped;
  ImDrawIdx* indices = (ImDrawIdx*) idx_buffer->mapped;

  for (s32 i = 0; i < draw_data->CmdListsCount; ++i) {
    const ImDrawList* current = draw_data->CmdLists[i];
    const u64 idx_buffer_size = (u64) current->IdxBuffer.Size;
    const u64 vtx_buffer_size = (u64) current->VtxBuffer.Size;

    memcpy (vertices, current->VtxBuffer.Data, sizeof (ImDrawVert) * vtx_buffer_size);
    memcpy (indices, current->IdxBuffer.Data, sizeof (ImDrawIdx) * idx_buffer_size);

    vertices += vtx_buffer_size;
    indices += idx_buffer_size;
  }

  rei_vk_flush_buffers (vk_allocator, 2, frame_data->buffers[frame_index]);
}

void rei_imgui_draw_cmd (VkCommandBuffer vk_cmd_buffer, const rei_imgui_frame_data_t* frame_data, const ImDrawData* draw_data, u32 frame_index) {
  vkCmdBindPipeline (vk_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, frame_data->pipeline);
  REI_VK_BIND_DESCRIPTORS (vk_cmd_buffer, frame_data->pipeline_layout, 1, &frame_data->font_descriptor);

  const rei_vec2_t scale = {.x = 2.f / 1680.f, .y = 2.f / 1050.f};
  vkCmdPushConstants (vk_cmd_buffer, frame_data->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof (rei_vec2_t), &scale);

  vkCmdBindVertexBuffers (vk_cmd_buffer, 0, 1, &frame_data->buffers[frame_index]->vtx_buffer.handle, (const u64[]) {0});
  vkCmdBindIndexBuffer (vk_cmd_buffer, frame_data->buffers[frame_index]->idx_buffer.handle, 0, VK_INDEX_TYPE_UINT16);

  s32 vtx_offset = 0;
  u32 idx_offset = 0;

  for (s32 i = 0; i < draw_data->CmdListsCount; ++i) {
    const ImDrawList* cmd_list = draw_data->CmdLists[i];

    for (s32 j = 0; j < cmd_list->CmdBuffer.Size; ++j) {
      const ImDrawCmd* draw_cmd = &cmd_list->CmdBuffer.Data[j];
      const VkRect2D scissor = {
	      .offset.x = REI_MAX ((s32) draw_cmd->ClipRect.x, 0),
	      .offset.y = REI_MAX ((s32) draw_cmd->ClipRect.y, 0),
	      .extent.width = (u32) (draw_cmd->ClipRect.z - draw_cmd->ClipRect.x),
	      .extent.height = (u32) (draw_cmd->ClipRect.w - draw_cmd->ClipRect.y)
      };

      vkCmdSetScissor (vk_cmd_buffer, 0, 1, &scissor);
      vkCmdDrawIndexed (vk_cmd_buffer, draw_cmd->ElemCount, 1, draw_cmd->IdxOffset + idx_offset, (s32) draw_cmd->VtxOffset + vtx_offset, 0);
    }

    idx_offset += (u32) cmd_list->IdxBuffer.Size;
    vtx_offset += cmd_list->VtxBuffer.Size;
  }
}

void rei_imgui_debug_window_wid (ImGuiIO* io) {
  igBegin ("Debug menu:", NULL, 0);

  igSeparator ();
  igText ("Frame time: %.3f ms (%.1f FPS)", 1000.f / io->Framerate, io->Framerate);
  igSeparator ();

  igSeparator ();
  igText ("IMGUI data: %d vertices with %d indices", io->MetricsRenderVertices, io->MetricsRenderIndices);
  igSeparator ();

  igEnd ();
}
