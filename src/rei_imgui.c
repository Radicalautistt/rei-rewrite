#include <memory.h>

#include "rei_window.h"
#include "rei_types.h"
#include "rei_imgui.h"
#include "rei_defines.h"

//#include <xcb/xcb.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui/cimgui.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

void rei_create_imgui_ctxt (
  rei_vk_device_t* device,
  VmaAllocator allocator,
  const rei_vk_imm_ctxt_t* imm_ctxt,
  rei_imgui_ctxt_t* out) {

  out->handle = igCreateContext (NULL);

  { // Create font texture.
    u8* pixels;
    s32 width, height;

    const ImGuiIO* io = igGetIO ();
    ImFontAtlas_GetTexDataAsRGBA32 (io->Fonts, &pixels, &width, &height, NULL);

    rei_vk_create_texture (device, allocator, imm_ctxt, pixels, (u32) width, (u32) height, &out->font_texture);
    ImFontAtlas_ClearTexData (io->Fonts);
    ImFontAtlas_SetTexID (io->Fonts, (void*) ((u64) out->font_texture.handle));
  }

  rei_vk_create_sampler (device, 0.f, 1.f, VK_FILTER_LINEAR, &out->font_sampler);

  // Set IMGUI theme.
  ImGuiStyle* style = igGetStyle ();
  igStyleColorsClassic (style);

  style->WindowRounding = 0.f;
  style->ScrollbarRounding = 0.f;
  style->Colors[ImGuiCol_WindowBg].w = 1.f;

  style->Colors[ImGuiCol_TitleBg].x = 0.f;
  style->Colors[ImGuiCol_TitleBg].y = 0.f;
  style->Colors[ImGuiCol_TitleBg].z = 0.f;
  style->Colors[ImGuiCol_TitleBg].w = 1.f;

  style->Colors[ImGuiCol_ScrollbarBg].x = 0.f;
  style->Colors[ImGuiCol_ScrollbarBg].y = 0.f;
  style->Colors[ImGuiCol_ScrollbarBg].z = 0.f;
  style->Colors[ImGuiCol_ScrollbarBg].w = 1.f;

  style->Colors[ImGuiCol_TitleBgActive].x = 0.f;
  style->Colors[ImGuiCol_TitleBgActive].y = 0.f;
  style->Colors[ImGuiCol_TitleBgActive].z = 0.f;
  style->Colors[ImGuiCol_TitleBgActive].w = 1.f;
}

void rei_destroy_imgui_ctxt (rei_vk_device_t* device, VmaAllocator allocator, rei_imgui_ctxt_t* ctxt) {
  vkDestroySampler (device->handle, ctxt->font_sampler, NULL);
  rei_vk_destroy_image (device, allocator, &ctxt->font_texture);

  igDestroyContext (ctxt->handle);
}

void rei_create_imgui_frame_data (rei_vk_device_t* device, VmaAllocator allocator, const rei_imgui_frame_data_ci_t* create_info, rei_imgui_frame_t* out) {
  { // Create dummy buffers for the first frame so that we can elude
    // assertion of buffer being a valid handle every time we want to delete it.
    for (u32 i = 0; i < 2; ++i) {
      rei_vk_create_buffer (
	allocator,
	1,
	VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	VMA_MEMORY_USAGE_CPU_TO_GPU,
	VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	&out->vertex_buffers[i]
      );

      rei_vk_create_buffer (
	allocator,
	1,
	VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	VMA_MEMORY_USAGE_CPU_TO_GPU,
	VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	&out->index_buffers[i]
      );

      // Persistently map buffers to update them every time new data arrives from IMGUI.
      REI_VK_CHECK (vmaMapMemory (allocator, out->vertex_buffers[i].memory, &out->vertex_buffers[i].mapped));
      REI_VK_CHECK (vmaMapMemory (allocator, out->index_buffers[i].memory, &out->index_buffers[i].mapped));
    }
  }

  {
    VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = NULL,
      .descriptorPool = create_info->descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &create_info->descriptor_layout
    };

    REI_VK_CHECK (vkAllocateDescriptorSets (device->handle, &alloc_info, &out->font_descriptor));
  }

  {
    VkDescriptorImageInfo image_info = {
      .sampler = create_info->sampler,
      .imageView = create_info->texture->view,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext = NULL,
      .dstSet = out->font_descriptor,
      .dstBinding = 0,
      .dstArrayElement = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &image_info,
      .pBufferInfo = NULL,
      .pTexelBufferView = NULL
    };

    vkUpdateDescriptorSets (device->handle, 1, &write, 0, NULL);
  }

  {
    VkPushConstantRange push_constant = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof (rei_vec2_t)};
    VkPipelineLayoutCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .setLayoutCount = 1,
      .pSetLayouts = &create_info->descriptor_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_constant
    };

    REI_VK_CHECK (vkCreatePipelineLayout (device->handle, &info, NULL, &out->pipeline_layout));
  }

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
    .cache = create_info->pipeline_cache,
    .render_pass = create_info->render_pass,
    .pixel_shader_path = "shaders/imgui.frag.spv",
    .vertex_shader_path = "shaders/imgui.vert.spv",

    .dynamic_state = &dynamic_state,
    .viewport_state = &viewport_state,
    .vertex_input_state = &vertex_input_state,
    .depth_stencil_state = &depth_stencil_state,
    .rasterization_state = &rasterization_state,
    .color_blend_attachments = &color_blend_attachment
  };

  rei_vk_create_gfx_pipeline (device, &info, &out->pipeline);
}

void rei_destroy_imgui_frame_data (rei_vk_device_t* device, VmaAllocator allocator, rei_imgui_frame_t* frame_data) {
  vkDestroyPipeline (device->handle, frame_data->pipeline, NULL);
  vkDestroyPipelineLayout (device->handle, frame_data->pipeline_layout, NULL);

  for (u32 i = 0; i < 2; ++i) {
    vmaUnmapMemory (allocator, frame_data->index_buffers[i].memory);
    vmaUnmapMemory (allocator, frame_data->vertex_buffers[i].memory);
    rei_vk_destroy_buffer (allocator, &frame_data->index_buffers[i]);
    rei_vk_destroy_buffer (allocator, &frame_data->vertex_buffers[i]);
  }
}

void rei_new_imgui_frame (ImGuiIO* io) {
  // FIXME hardcoded values
  io->DisplaySize.x = 1366.f;
  io->DisplaySize.y = 768.f;

  igNewFrame ();
}

#if 0
void rei_handle_imgui_events (ImGuiIO* io, const rei_xcb_window_t* window, const xcb_generic_event_t* event) {
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
#endif

void rei_update_imgui (VmaAllocator allocator, rei_imgui_frame_t* frame_data, const ImDrawData* draw_data, u32 frame_index) {
  const u64 new_index_buffer_size = sizeof (ImDrawIdx) * (u64) draw_data->TotalIdxCount;
  const u64 new_vertex_buffer_size = sizeof (ImDrawVert) * (u64) draw_data->TotalVtxCount;

  rei_vk_buffer_t* index_buffer = &frame_data->index_buffers[frame_index];
  rei_vk_buffer_t* vertex_buffer = &frame_data->vertex_buffers[frame_index];

  // Recreate buffers if their sizes are insufficient to hold IMGUI geometry.
  if (new_vertex_buffer_size > vertex_buffer->size) {
    vmaUnmapMemory (allocator, vertex_buffer->memory);
    rei_vk_destroy_buffer (allocator, vertex_buffer);

    rei_vk_create_buffer (
      allocator,
      new_vertex_buffer_size,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VMA_MEMORY_USAGE_CPU_TO_GPU,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      vertex_buffer
    );

    REI_VK_CHECK (vmaMapMemory (allocator, vertex_buffer->memory, &vertex_buffer->mapped));
  }

  if (new_index_buffer_size > index_buffer->size) {
    vmaUnmapMemory (allocator, index_buffer->memory);
    rei_vk_destroy_buffer (allocator, index_buffer);

    rei_vk_create_buffer (
      allocator,
      new_index_buffer_size,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VMA_MEMORY_USAGE_CPU_TO_GPU,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      index_buffer
    );

    REI_VK_CHECK (vmaMapMemory (allocator, index_buffer->memory, &index_buffer->mapped));
  }

  // Write IMGUI data to buffers.
  ImDrawVert* vertices = (ImDrawVert*) vertex_buffer->mapped;
  ImDrawIdx* indices = (ImDrawIdx*) index_buffer->mapped;

  for (s32 i = 0; i < draw_data->CmdListsCount; ++i) {
    const ImDrawList* current = draw_data->CmdLists[i];
    const u64 index_buffer_size = (u64) current->IdxBuffer.Size;
    const u64 vertex_buffer_size = (u64) current->VtxBuffer.Size;

    memcpy (vertices, current->VtxBuffer.Data, sizeof (ImDrawVert) * vertex_buffer_size);
    memcpy (indices, current->IdxBuffer.Data, sizeof (ImDrawIdx) * index_buffer_size);

    vertices += vertex_buffer_size;
    indices += index_buffer_size;
  }

  REI_VK_CHECK (vmaFlushAllocations (
    allocator,
    2,
    (VmaAllocation[]) {vertex_buffer->memory, index_buffer->memory},
    (VkDeviceSize[]) {0, 0},
    (VkDeviceSize[]) {vertex_buffer->size, index_buffer->size}
  ));
}

void rei_render_imgui_cmd (VkCommandBuffer cmd_buffer, rei_imgui_frame_t* frame_data, const ImDrawData* draw_data, u32 frame_index) {
  vkCmdBindPipeline (cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, frame_data->pipeline);
  vkCmdBindDescriptorSets (cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, frame_data->pipeline_layout, 0, 1, &frame_data->font_descriptor, 0, NULL);

  const rei_vec2_t scale = {.x = 2.f / 1680.f, .y = 2.f / 1050.f};
  vkCmdPushConstants (cmd_buffer, frame_data->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof (rei_vec2_t), &scale);

  vkCmdBindVertexBuffers (cmd_buffer, 0, 1, &frame_data->vertex_buffers[frame_index].handle, (const u64[]) {0});
  vkCmdBindIndexBuffer (cmd_buffer, frame_data->index_buffers[frame_index].handle, 0, VK_INDEX_TYPE_UINT16);

  s32 vtx_offset = 0;
  u32 idx_offset = 0;

  for (s32 i = 0; i < draw_data->CmdListsCount; ++i) {
    const ImDrawList* cmdList = draw_data->CmdLists[i];

    for (s32 j = 0; j < cmdList->CmdBuffer.Size; ++j) {
      const ImDrawCmd* cmd = &cmdList->CmdBuffer.Data[j];
      const VkRect2D scissor = {
	.offset.x = REI_MAX ((s32) cmd->ClipRect.x, 0),
	.offset.y = REI_MAX ((s32) cmd->ClipRect.y, 0),
	.extent.width = (u32) (cmd->ClipRect.z - cmd->ClipRect.x),
	.extent.height = (u32) (cmd->ClipRect.w - cmd->ClipRect.y)
      };

      vkCmdSetScissor (cmd_buffer, 0, 1, &scissor);
      vkCmdDrawIndexed (cmd_buffer, cmd->ElemCount, 1, cmd->IdxOffset + idx_offset, (s32) cmd->VtxOffset + vtx_offset, 0);
    }

    idx_offset += (u32) cmdList->IdxBuffer.Size;
    vtx_offset += cmdList->VtxBuffer.Size;
  }
}

void rei_imgui_debug_window (ImGuiIO* io) {
  igBegin ("Debug menu:", NULL, 0);

  igSeparator ();
  igText ("Frame time: %.3f ms (%.1f FPS)", 1000.f / io->Framerate, io->Framerate);
  igSeparator ();

  igSeparator ();
  igText ("IMGUI data: %d vertices with %d indices", io->MetricsRenderVertices, io->MetricsRenderIndices);
  igSeparator ();

  igEnd ();
}
