#include <sys/time.h>

#include "rei_vk.h"
#include "rei_model.h"
#include "rei_window.h"
#include "rei_camera.h"
#include "rei_debug.h"
#include "rei_math.inl"
#include "rei_defines.h"
#include "rei_asset_loaders.h"

#include <xcb/xcb.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

#define REI_VK_FRAME_COUNT 2u

int main (void) {
  struct timeval timer_start;

  VkInstance instance;
  #ifndef NDEBUG
  VkDebugUtilsMessengerEXT debug_messenger;
  #endif

  rei_xcb_window_t window;
  VkSurfaceKHR window_surface;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  rei_vk_device_t vk_device;
  VmaAllocator vk_allocator;

  rei_vk_swapchain_t swapchain;
  rei_vk_render_pass_t vk_render_pass;
  u32 frame_index = 0;
  VkCommandPool vk_frame_cmd_pool;
  rei_vk_frame_data_t* vk_frames;

  rei_vk_imm_ctxt_t imm_ctxt;
  VkPipeline default_pipeline;
  VkDescriptorPool main_descriptor_pool;
  VkPipelineLayout default_pipeline_layout;
  VkDescriptorSetLayout default_descriptor_layout;
  VkSampler default_sampler;

  rei_model_t test_model;
  rei_camera_t camera;

  //rei_imgui_ctxt_t imgui_ctxt;
  //rei_imgui_frame_t imgui_frame_data;

  // Start timer
  gettimeofday (&timer_start, NULL);

  REI_VK_CHECK (volkInitialize ());

  { // Create instance and debug messenger.
    const char* const required_extensions[] = {
      VK_KHR_SURFACE_EXTENSION_NAME,
      VK_KHR_XCB_SURFACE_EXTENSION_NAME,
      #ifndef NDEBUG
      VK_EXT_DEBUG_UTILS_EXTENSION_NAME
      #endif
    };

    rei_vk_create_instance (required_extensions, REI_ARRAY_SIZE (required_extensions), &instance);
  }

  #ifndef NDEBUG
    {
      REI_VK_DEBUG_MESSENGER_CI (create_info);
      REI_VK_CHECK (vkCreateDebugUtilsMessengerEXT (instance, &create_info, NULL, &debug_messenger));
    }
  #endif

  #ifdef __linux__
     rei_create_xcb_window (640, 480, REI_FALSE, &window);
     rei_vk_create_xcb_surface (instance, window.handle, window.conn, &window_surface);
  #else
  #  error "Unhandled platform..."
  #endif

  { // Choose physical device and create logical one.
    const char* const required_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const u32 required_extension_count = (const u32) REI_ARRAY_SIZE (required_extensions);

    rei_vk_choose_gpu (instance, window_surface, required_extensions, required_extension_count, &physical_device);
    rei_vk_create_device (physical_device, window_surface, required_extensions, required_extension_count, &vk_device);
  }

  rei_vk_create_allocator (instance, physical_device, &vk_device, &vk_allocator);

  rei_vk_create_swapchain (
    &vk_device,
    vk_allocator,
    VK_NULL_HANDLE,
    window_surface,
    window.width,
    window.height,
    physical_device,
    &swapchain
  );

  {
    rei_vk_render_pass_ci_t create_info = {.r = 1.f, .g = 1.f, .b = 0.f, .a = 1.f, .swapchain = &swapchain};
    rei_vk_create_render_pass (&vk_device, &create_info, &vk_render_pass);
  }

  { // Create one command pool and frame data (sync structures, cmd buffers) for every frame in flight.
    VkCommandPoolCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = vk_device.gfx_index,

      .pNext = NULL,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };

    REI_VK_CHECK (vkCreateCommandPool (vk_device.handle, &create_info, NULL, &vk_frame_cmd_pool));

    vk_frames = malloc (sizeof *vk_frames * REI_VK_FRAME_COUNT);
    for (u32 i = 0; i < REI_VK_FRAME_COUNT; ++i) rei_vk_create_frame_data (&vk_device, vk_frame_cmd_pool, &vk_frames[i]);
  }

  // FIXME max_lod is hardcoded.
  rei_vk_create_sampler (&vk_device, 0.f, 9.f, VK_FILTER_NEAREST, &default_sampler);

  {
    VkDescriptorSetLayoutBinding albedo = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = NULL,
    };

    VkDescriptorSetLayoutCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .bindingCount = 1,
      .pBindings = &albedo,
    };

    REI_VK_CHECK (vkCreateDescriptorSetLayout (vk_device.handle, &create_info, NULL, &default_descriptor_layout));
  }

  rei_vk_create_imm_ctxt (&vk_device, vk_device.gfx_index, &imm_ctxt);

  { // Create main descriptor pool for miscellaneous resources (e.g. IMGUI font texture).
    VkDescriptorPoolCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = (VkDescriptorPoolSize[]) {{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1}}
    };

    REI_VK_CHECK (vkCreateDescriptorPool (vk_device.handle, &create_info, NULL, &main_descriptor_pool));
  }

  {
    VkPushConstantRange push_constant = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .offset = 0,
      .size = sizeof (rei_mat4_t)
    };

    VkPipelineLayoutCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .setLayoutCount = 0,
      .pSetLayouts = NULL,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_constant,
    };

    REI_VK_CHECK (vkCreatePipelineLayout (vk_device.handle, &create_info, NULL, &default_pipeline_layout));
  }

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
      .extent.width = swapchain.width,
      .extent.height = swapchain.height
    };

    VkViewport viewport = {
      .x = 0.f,
      .y = 0.f,
      .minDepth = 0.f,
      .maxDepth = 1.f,
      .width = (f32) swapchain.width,
      .height = (f32) swapchain.height
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

  rei_create_model ("assets/sponza/Sponza.gltf", &vk_device, vk_allocator, &imm_ctxt, &test_model);

  rei_create_camera (
    &(rei_vec3_t) {.x = 0.f, .y = 1.f, .z = 0.f},
    &(rei_vec3_t) {.x = 5.5f, .y = 5.f, .z = 60.f},
    (f32) (swapchain.width / swapchain.height),
    -90.f,
    0.f,
    &camera
  );

  //rei_create_imgui_ctxt (&vk_device, vk_allocator, &imm_ctxt, &imgui_ctxt);

#if 0
  {
    rei_imgui_frame_data_ci_t create_info = {
      .render_pass = render_pass,
      .descriptor_pool = main_descriptor_pool,
      .descriptor_layout = default_descriptor_layout,
      .pipeline_cache = VK_NULL_HANDLE,
      .sampler = imgui_ctxt.font_sampler,
      .texture = &imgui_ctxt.font_texture
    };

    rei_create_imgui_frame_data (&vk_device, vk_allocator, &create_info, &imgui_frame_data);
  }

  rei_create_openal_ctxt (&openal_ctxt);

#endif
  const f32 delta_time = 1.f / 60.f;
  xcb_generic_event_t* event;

  // Game loop invariants
  const VkCommandBufferBeginInfo cmd_begin_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = NULL,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = NULL,
  };

  const VkPipelineStageFlags pipeline_wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  for (;;) {
    //ImGuiIO* imgui_io = igGetIO ();
    //imgui_io->DeltaTime = delta_time;

    while ((event = xcb_poll_for_event (window.conn))) {
      //rei_handle_imgui_events (imgui_io, &window, event);

      switch (event->response_type) {
        case XCB_KEY_PRESS:
	  switch (((xcb_key_press_event_t*) event)->detail) {
	    // Use goto to get out of the game loop instead of
	    // while (running) {...} to prevent branching
	    case REI_X11_KEY_ESCAPE: free (event); goto RESOURCE_CLEANUP_L;
	    case REI_X11_KEY_A: rei_move_camera_left (&camera, delta_time); break;
	    case REI_X11_KEY_D: rei_move_camera_right (&camera, delta_time); break;
	    case REI_X11_KEY_W: rei_move_camera_forward (&camera, delta_time); break;
	    case REI_X11_KEY_S: rei_move_camera_backward (&camera, delta_time); break;
	    default: break;
	  }
	  break;
	default: break;
      }

      free (event);
    }

    frame_index %= REI_VK_FRAME_COUNT;

    const rei_vk_frame_data_t* current = &vk_frames[frame_index];
    VkCommandBuffer cmd_buffer = current->cmd_buffer;
    VkFence submit_fence = current->submit_fence;
    VkSemaphore present_semaphore = current->present_semaphore;
    VkSemaphore render_semaphore = current->render_semaphore;

    REI_VK_CHECK (vkWaitForFences (vk_device.handle, 1, &submit_fence, VK_TRUE, ~0ull));
    REI_VK_CHECK (vkResetFences (vk_device.handle, 1, &submit_fence));

    u32 image_index = 0;
    REI_VK_CHECK (vkAcquireNextImageKHR (vk_device.handle, swapchain.handle, ~0ull, present_semaphore, VK_NULL_HANDLE, &image_index));

    REI_VK_CHECK (vkBeginCommandBuffer (cmd_buffer, &cmd_begin_info));

    VkRenderPassBeginInfo rndr_begin_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .pNext = NULL,
      .renderPass = vk_render_pass.handle,
      .framebuffer = vk_render_pass.framebuffers[image_index],
      .renderArea.extent.width = swapchain.width,
      .renderArea.extent.height = swapchain.height,
      .renderArea.offset.x = 0,
      .renderArea.offset.y = 0,
      .clearValueCount = vk_render_pass.clear_value_count,
      .pClearValues = vk_render_pass.clear_values
    };

    vkCmdBeginRenderPass (cmd_buffer, &rndr_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    rei_vec3_t center;
    rei_mat4_t view_matrix;
    rei_vec3_add (&camera.position, &camera.front, &center);
    rei_look_at (&camera.position, &center, &camera.up, &view_matrix);

    rei_mat4_t view_projection;
    rei_mat4_mul (&camera.projection_matrix, &view_matrix, &view_projection);

    vkCmdBindPipeline (cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, default_pipeline);
    rei_draw_model_cmd (&test_model, cmd_buffer, default_pipeline_layout, &view_projection);

#if 0
    rei_new_imgui_frame (imgui_io);
    rei_imgui_debug_window (imgui_io);
    igRender ();

    const ImDrawData* imgui_data = igGetDrawData ();
    rei_update_imgui (vk_allocator, &imgui_frame_data, imgui_data, frame_index);
    rei_render_imgui_cmd (cmd_buffer, &imgui_frame_data, imgui_data, frame_index);
#endif

    vkCmdEndRenderPass (cmd_buffer);
    REI_VK_CHECK (vkEndCommandBuffer (cmd_buffer));

    VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = NULL,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &present_semaphore,
      .pWaitDstStageMask = &pipeline_wait_stage,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmd_buffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &render_semaphore,
    };

    REI_VK_CHECK (vkQueueSubmit (vk_device.gfx_queue, 1, &submit_info, submit_fence));

    VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext = NULL,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &render_semaphore,
      .swapchainCount = 1,
      .pSwapchains = &swapchain.handle,
      .pImageIndices = &image_index,
      .pResults = NULL
    };

    REI_VK_CHECK (vkQueuePresentKHR (vk_device.present_queue, &present_info));
    ++frame_index;
  }

RESOURCE_CLEANUP_L:
  vkDeviceWaitIdle (vk_device.handle);

#if 0
  rei_destroy_openal_ctxt (&openal_ctxt);
  rei_destroy_imgui_frame_data (&vk_device, vk_allocator, &imgui_frame_data);
  rei_destroy_imgui_ctxt (&vk_device, vk_allocator, &imgui_ctxt);
#endif

  rei_destroy_model (vk_allocator, &test_model);
  vkDestroyPipeline (vk_device.handle, default_pipeline, NULL);
  vkDestroyPipelineLayout (vk_device.handle, default_pipeline_layout, NULL);
  vkDestroyDescriptorPool (vk_device.handle, main_descriptor_pool, NULL);

  rei_vk_destroy_imm_ctxt (&vk_device, &imm_ctxt);
  vkDestroyDescriptorSetLayout (vk_device.handle, default_descriptor_layout, NULL);
  vkDestroySampler (vk_device.handle, default_sampler, NULL);

  for (u32 i = 0; i < REI_VK_FRAME_COUNT; ++i) rei_vk_destroy_frame_data (&vk_device, &vk_frames[i]);
  free (vk_frames);
  vkDestroyCommandPool (vk_device.handle, vk_frame_cmd_pool, NULL);

  rei_vk_destroy_render_pass (&vk_device, &vk_render_pass);
  rei_vk_destroy_swapchain (&vk_device, vk_allocator, &swapchain);

  vmaDestroyAllocator (vk_allocator);
  vkDestroyDevice (vk_device.handle, NULL);
  rei_destroy_xcb_window (&window);

  vkDestroySurfaceKHR (instance, window_surface, NULL);

  #ifndef NDEBUG
    vkDestroyDebugUtilsMessengerEXT (instance, debug_messenger, NULL);
  #endif
  vkDestroyInstance (instance, NULL);
}
