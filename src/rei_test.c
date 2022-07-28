#include <sys/time.h>

#include "rei_vk.h"
#include "rei_debug.h"
#include "rei_model.h"
#include "rei_imgui.h"
#include "rei_window.h"
#include "rei_camera.h"
#include "rei_defines.h"
#include "rei_asset_loaders.h"

#include <xcb/xcb.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

int main (void) {
  struct timeval timer_start;

  // TODO move this data away from the stack.
  rei_vk_instance_t vk_instance;

  rei_xcb_window_t window;
  VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
  rei_vk_device_t vk_device;
  VmaAllocator vk_allocator;

  rei_vk_swapchain_t vk_swapchain;
  rei_vk_render_pass_t vk_render_pass;
  u32 frame_index = 0;
  VkCommandPool vk_frame_cmd_pool;
  rei_vk_frame_data_t* vk_frames;

  rei_vk_imm_ctxt_t imm_ctxt;
  VkPipeline default_pipeline;
  VkDescriptorPool main_desc_pool;
  VkPipelineLayout default_pipeline_layout;
  VkDescriptorSetLayout default_desc_layout;
  VkSampler default_sampler;

  rei_model_t test_model;
  rei_camera_t camera;
  rei_mat4_t* camera_projection;

  rei_imgui_ctxt_t imgui_ctxt;
  rei_imgui_frame_data_t imgui_frame_data;

  // Start timer
  gettimeofday (&timer_start, NULL);

  #ifdef __linux__
     rei_create_xcb_window (640, 480, REI_FALSE, &window);
  #else
  #  error "Unhandled platform..."
  #endif

  REI_VK_CHECK (volkInitialize ());

  rei_vk_create_instance_linux (window.conn, window.handle, &vk_instance);

  { // Choose physical device and create logical one.
    const char* const required_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const u32 required_extension_count = (const u32) REI_ARRAY_SIZE (required_extensions);

    rei_vk_choose_gpu (&vk_instance, required_extensions, required_extension_count, &vk_physical_device);
    rei_vk_create_device (vk_physical_device, vk_instance.surface, required_extensions, required_extension_count, &vk_device);
  }

  rei_vk_create_allocator (vk_instance.handle, vk_physical_device, &vk_device, &vk_allocator);

  rei_vk_create_swapchain (
    &vk_device,
    vk_allocator,
    VK_NULL_HANDLE,
    vk_instance.surface,
    window.width,
    window.height,
    vk_physical_device,
    &vk_swapchain
  );

  rei_vk_create_render_pass (&vk_device, &vk_swapchain, &(const rei_vec4_u) {.x = 1.f, .y = 1.f, .z = 0.f, .w = 1.f}, &vk_render_pass);

  // Create one command pool and frame data (sync structures, cmd buffers) for every frame in flight.
  rei_vk_create_cmd_pool (&vk_device, vk_device.gfx_index, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, &vk_frame_cmd_pool);

  vk_frames = malloc (sizeof *vk_frames * REI_VK_FRAME_COUNT);
  for (u32 i = 0; i < REI_VK_FRAME_COUNT; ++i) rei_vk_create_frame_data (&vk_device, vk_frame_cmd_pool, &vk_frames[i]);

  rei_vk_create_imm_ctxt (&vk_device, vk_device.gfx_index, &imm_ctxt);

  // FIXME max_lod is hardcoded.
  rei_vk_create_sampler (&vk_device, 0.f, 0.f, VK_FILTER_NEAREST, &default_sampler);

  rei_vk_create_descriptor_layout (
    &vk_device,
    1,
    &(const VkDescriptorSetLayoutBinding) {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = NULL,
    },
    &default_desc_layout
  );

  // Create main descriptor pool for miscellaneous resources (e.g. IMGUI font texture).
  rei_vk_create_descriptor_pool (
    &vk_device,
    1,
    1,
    &(const VkDescriptorPoolSize) {
      .descriptorCount = 1,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    },
    &main_desc_pool
  );

  rei_vk_create_pipeline_layout (
    &vk_device,
    1, &default_desc_layout,
    1, &(const VkPushConstantRange) {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof (rei_mat4_t)},
    &default_pipeline_layout
  );

  {
    VkVertexInputBindingDescription binding = {
      .binding = 0,
      .stride = sizeof (rei_vertex_t),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attributes[3] = {
      [0] = { // Position
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = REI_OFFSET_OF (rei_vertex_t, x),
      },

      [1] = { // Normal
        .location = 1,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = REI_OFFSET_OF (rei_vertex_t, nx),
      },

      [2] = { // Uv
        .location = 2,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = REI_OFFSET_OF (rei_vertex_t, u),
      }
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &binding,
      .vertexAttributeDescriptionCount = REI_ARRAY_SIZE (attributes),
      .pVertexAttributeDescriptions = attributes
    };

    VkRect2D scissor = {
      .offset = {0, 0},
      .extent.width = vk_swapchain.width,
      .extent.height = vk_swapchain.height
    };

    VkViewport viewport = {
      .x = 0.f,
      .y = 0.f,
      .minDepth = 0.f,
      .maxDepth = 1.f,
      .width = (f32) vk_swapchain.width,
      .height = (f32) vk_swapchain.height
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
      .pNext = NULL,
      .scissorCount = 1,
      .viewportCount = 1,
      .pScissors = &scissor,
      .pViewports = &viewport,
      .flags = 0,
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
      .pNext = NULL,
      .lineWidth = 1.f,
      .depthBiasClamp = 0.f,
      .flags = 0,
      .depthBiasSlopeFactor = 0.f,
      .depthBiasEnable = VK_FALSE,
      .depthClampEnable = VK_FALSE,
      .depthBiasConstantFactor = 0.f,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
      .back = {0},
      .back.compareOp = VK_COMPARE_OP_ALWAYS,
      .front = {0},
      .pNext = NULL,
      .minDepthBounds = 0.f,
      .maxDepthBounds = 1.f,
      .flags = 0,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .stencilTestEnable = VK_FALSE,
      .depthBoundsTestEnable = VK_FALSE,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
      .colorWriteMask = 0xF,
      .blendEnable = VK_FALSE,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO
    };

    rei_vk_gfx_pipeline_ci_t create_info = {
      .cache = VK_NULL_HANDLE,
      .render_pass = vk_render_pass.handle,
      .layout = default_pipeline_layout,

      .subpass_index = 0,
      .color_blend_attachment_count = 1,

      .pixel_shader_path = "shaders/model.frag.spv",
      .vertex_shader_path = "shaders/model.vert.spv",

      .dynamic_state = NULL,
      .viewport_state = &viewport_state,
      .vertex_input_state = &vertex_input_state,
      .depth_stencil_state = &depth_stencil_state,
      .color_blend_attachments = &color_blend_attachment,
      .rasterization_state = &rasterization_state
    };

    rei_vk_create_gfx_pipeline (&vk_device, &create_info, &default_pipeline);
  }

  rei_model_create ("assets/sponza/Sponza.gltf", &vk_device, vk_allocator, &imm_ctxt, default_sampler, default_desc_layout, &test_model);

  rei_camera_create (0.f, 1.f, 0.f, -90.f, 0.f, &camera);

  rei_camera_position_t camera_position = {.data = {.x = 0.f, .y = 1.f, .z = 60.f}};
  camera_projection = rei_camera_create_projection ((f32) (vk_swapchain.width / vk_swapchain.height));

  rei_create_imgui_ctxt (&vk_device, vk_allocator, &imm_ctxt, &imgui_ctxt);
  rei_imgui_create_frame_data (&vk_device, vk_allocator, &vk_render_pass, main_desc_pool, default_desc_layout, &imgui_ctxt, &imgui_frame_data);

#if 0
  rei_create_openal_ctxt (&openal_ctxt);
#endif
  const f32 delta_time = 1.f / 60.f;

  for (;;) {
    ImGuiIO* imgui_io = igGetIO ();
    imgui_io->DeltaTime = delta_time;

    xcb_generic_event_t* event = xcb_poll_for_event (window.conn);

    if (event) {
      rei_imgui_handle_events (imgui_io, &window, event);

      if (event->response_type == XCB_KEY_PRESS) {
        const xcb_key_press_event_t* key_press = (const xcb_key_press_event_t*) event;

        if (key_press->detail == REI_X11_KEY_ESCAPE) goto RESOURCE_CLEANUP_L;
        if (key_press->detail == REI_X11_KEY_A) rei_camera_move_left (&camera, &camera_position, delta_time);
        if (key_press->detail == REI_X11_KEY_D) rei_camera_move_right (&camera, &camera_position, delta_time);
        if (key_press->detail == REI_X11_KEY_W) rei_camera_move_forward (&camera, &camera_position, delta_time);
        if (key_press->detail == REI_X11_KEY_S) rei_camera_move_backward (&camera, &camera_position, delta_time);
      }

      free (event);
    }

    frame_index %= REI_VK_FRAME_COUNT;

    VkCommandBuffer vk_cmd_buffer;
    const rei_vk_frame_data_t* vk_current_frame = &vk_frames[frame_index];
    const u32 vk_image_index = rei_vk_begin_frame (&vk_device, &vk_render_pass, vk_current_frame, &vk_swapchain, &vk_cmd_buffer);

    rei_mat4_t view_projection;
    rei_camera_get_view_projection (&camera, &camera_position, camera_projection, &view_projection);

    vkCmdBindPipeline (vk_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, default_pipeline);
    rei_model_draw_cmd (&test_model, vk_cmd_buffer, default_pipeline_layout, &view_projection);

    rei_imgui_new_frame (imgui_io);
    rei_imgui_debug_window_wid (imgui_io);
    igRender ();

    const ImDrawData* imgui_data = igGetDrawData ();
    rei_imgui_update_buffers (vk_allocator, &imgui_frame_data, imgui_data, frame_index);

    rei_imgui_draw_cmd (vk_cmd_buffer, &imgui_frame_data, imgui_data, frame_index);

    rei_vk_end_frame (&vk_device, vk_current_frame, &vk_swapchain, vk_image_index);
    ++frame_index;
  }

RESOURCE_CLEANUP_L:
  vkDeviceWaitIdle (vk_device.handle);

  free (camera_projection);

#if 0
  rei_destroy_openal_ctxt (&openal_ctxt);
#endif
  rei_imgui_destroy_frame_data (&vk_device, vk_allocator, &imgui_frame_data);
  rei_destroy_imgui_ctxt (&vk_device, vk_allocator, &imgui_ctxt);

  rei_model_destroy (&vk_device, vk_allocator, &test_model);
  vkDestroyPipeline (vk_device.handle, default_pipeline, NULL);
  vkDestroyPipelineLayout (vk_device.handle, default_pipeline_layout, NULL);
  vkDestroyDescriptorPool (vk_device.handle, main_desc_pool, NULL);

  rei_vk_destroy_imm_ctxt (&vk_device, &imm_ctxt);
  vkDestroyDescriptorSetLayout (vk_device.handle, default_desc_layout, NULL);
  vkDestroySampler (vk_device.handle, default_sampler, NULL);

  for (u32 i = 0; i < REI_VK_FRAME_COUNT; ++i) rei_vk_destroy_frame_data (&vk_device, &vk_frames[i]);
  free (vk_frames);
  vkDestroyCommandPool (vk_device.handle, vk_frame_cmd_pool, NULL);

  rei_vk_destroy_render_pass (&vk_device, &vk_render_pass);
  rei_vk_destroy_swapchain (&vk_device, vk_allocator, &vk_swapchain);

  vmaDestroyAllocator (vk_allocator);
  vkDestroyDevice (vk_device.handle, NULL);

  rei_vk_destroy_instance (&vk_instance);
  rei_destroy_xcb_window (&window);
}
