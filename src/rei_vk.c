#include <math.h>
#include <alloca.h>
#include <string.h>
#include <malloc.h>

#include "rei_vk.h"
#include "rei_file.h"
#include "rei_debug.h"
#include "rei_defines.h"

#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

const char* rei_vk_show_error (VkResult error) {
  #define SHOW_ERROR(name) case VK_##name: return #name

  switch (error) {
    SHOW_ERROR (TIMEOUT);
    SHOW_ERROR (NOT_READY);
    SHOW_ERROR (EVENT_SET);
    SHOW_ERROR (INCOMPLETE);
    SHOW_ERROR (EVENT_RESET);
    SHOW_ERROR (SUBOPTIMAL_KHR);
    SHOW_ERROR (ERROR_DEVICE_LOST);
    SHOW_ERROR (ERROR_OUT_OF_DATE_KHR);
    SHOW_ERROR (ERROR_TOO_MANY_OBJECTS);
    SHOW_ERROR (ERROR_SURFACE_LOST_KHR);
    SHOW_ERROR (ERROR_MEMORY_MAP_FAILED);
    SHOW_ERROR (ERROR_LAYER_NOT_PRESENT);
    SHOW_ERROR (ERROR_INVALID_SHADER_NV);
    SHOW_ERROR (ERROR_OUT_OF_HOST_MEMORY);
    SHOW_ERROR (ERROR_FEATURE_NOT_PRESENT);
    SHOW_ERROR (ERROR_INCOMPATIBLE_DRIVER);
    SHOW_ERROR (ERROR_OUT_OF_DEVICE_MEMORY);
    SHOW_ERROR (ERROR_FORMAT_NOT_SUPPORTED);
    SHOW_ERROR (ERROR_INITIALIZATION_FAILED);
    SHOW_ERROR (ERROR_VALIDATION_FAILED_EXT);
    SHOW_ERROR (ERROR_EXTENSION_NOT_PRESENT);
    SHOW_ERROR (ERROR_NATIVE_WINDOW_IN_USE_KHR);
    SHOW_ERROR (ERROR_INCOMPATIBLE_DISPLAY_KHR);
    default: return "Unknown";
  }

  #undef SHOW_ERROR
}

VKAPI_ATTR VkBool32 VKAPI_CALL rei_vk_debug_callback (
  VkDebugUtilsMessageSeverityFlagBitsEXT severity,
  VkDebugUtilsMessageTypeFlagsEXT type,
  const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
  void* user_data) {

  (void) type;
  (void) user_data;

  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    REI_LOG_ERROR ("%s\n", callback_data->pMessage);

  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    REI_LOG_WARN ("%s\n", callback_data->pMessage);

  return VK_FALSE;
}

void rei_vk_create_instance (const char* const* required_ext, u32 required_ext_count, VkInstance* out) {
  // Check support for required extensions.
  u32 available_count = 0;
  REI_VK_CHECK (vkEnumerateInstanceExtensionProperties (NULL, &available_count, NULL));

  VkExtensionProperties* available = alloca (sizeof *available * available_count);
  REI_VK_CHECK (vkEnumerateInstanceExtensionProperties (NULL, &available_count, available));

  u32 matched_count = 0;

  for (u32 i = 0; i < required_ext_count; ++i) {
    for (u32 j = 0; j < available_count; ++j) {
      if (!strcmp (required_ext[i], available[j].extensionName)) {
	++matched_count;
	break;
      }
    }
  }

  if (matched_count != required_ext_count) {
    REI_LOGS_ERROR ("Vk initialization failure: required vulkan extensions aren't supported by this device.");
    for (u32 i = 0; i < required_ext_count; ++i) REI_LOG_ERROR ("\t%s", required_ext[i]);
    exit (EXIT_FAILURE);
  }

  VkInstanceCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &(VkApplicationInfo) {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = REI_VK_VERSION},
    .ppEnabledExtensionNames = required_ext,
    .enabledExtensionCount = required_ext_count,
  };

  #ifndef NDEBUG
  REI_VK_DEBUG_MESSENGER_CI (debug_info);

  create_info.pNext = &debug_info;
  create_info.enabledLayerCount = 1;
  create_info.ppEnabledLayerNames = (const char*[]) {"VK_LAYER_KHRONOS_validation"};
  #endif

  REI_VK_CHECK (vkCreateInstance (&create_info, NULL, out));
  volkLoadInstanceOnly (*out);
}

b8 rei_vk_find_queue_indices (VkPhysicalDevice device, VkSurfaceKHR surface, rei_vk_queue_indices_t* out) {
  out->gfx = out->present = out->transfer = REI_U32_MAX;

  u32 count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties (device, &count, NULL);

  VkQueueFamilyProperties* props = alloca (sizeof *props * count);
  vkGetPhysicalDeviceQueueFamilyProperties (device, &count, props);

  #define IS_VALID(queue) (out->queue != REI_U32_MAX)

  for (u32 i = 0; i < count; ++i) {
    const VkQueueFamilyProperties* current = &props[i];

    if (current->queueCount) {
      VkBool32 present = VK_FALSE;
      REI_VK_CHECK (vkGetPhysicalDeviceSurfaceSupportKHR (device, i, surface, &present));

      if (present) out->present = i;
      if (current->queueFlags & VK_QUEUE_GRAPHICS_BIT) out->gfx = i;
      if (current->queueFlags & VK_QUEUE_TRANSFER_BIT) out->transfer = i;

      if (IS_VALID (gfx) && IS_VALID (present) && IS_VALID (transfer)) {
        return REI_TRUE;
      }
    }
  }

  #undef IS_VALID
  return REI_FALSE;
}

void rei_vk_choose_gpu (
  VkInstance instance,
  VkSurfaceKHR surface,
  const char* const* required_ext,
  u32 required_ext_count,
  VkPhysicalDevice* out) {

  *out = VK_NULL_HANDLE;

  u32 device_count = 0;
  REI_VK_CHECK (vkEnumeratePhysicalDevices (instance, &device_count, NULL));

  VkPhysicalDevice* devices = alloca (sizeof *devices * device_count);
  REI_VK_CHECK (vkEnumeratePhysicalDevices (instance, &device_count, devices));

  for (u32 i = 0; i < device_count; ++i) {
    u32 matched_count = 0;
    VkPhysicalDevice current = devices[i];

    u32 ext_count = 0;
    REI_VK_CHECK (vkEnumerateDeviceExtensionProperties (current, NULL, &ext_count, NULL));

    VkExtensionProperties* extensions = alloca (sizeof *extensions * ext_count);
    REI_VK_CHECK (vkEnumerateDeviceExtensionProperties (current, NULL, &ext_count, extensions));

    for (u32 j = 0; j < required_ext_count; ++j) {
      for (u32 k = 0; k < ext_count; ++k) {
        if (!strcmp (extensions[k].extensionName, required_ext[j])) {
          ++matched_count;
          break;
        }
      }
    }

    u32 format_count = 0, present_mode_count = 0;
    REI_VK_CHECK (vkGetPhysicalDeviceSurfaceFormatsKHR (current, surface, &format_count, NULL));
    REI_VK_CHECK (vkGetPhysicalDeviceSurfacePresentModesKHR (current, surface, &present_mode_count, NULL));

    rei_vk_queue_indices_t indices;
    const b8 supports_swapchain = format_count && present_mode_count;
    const b8 has_queue_families = rei_vk_find_queue_indices (current, surface, &indices);

    if (has_queue_families && supports_swapchain && (matched_count == required_ext_count)) {
      *out = current;
      break;
    }
  }

  // Make sure that chosen device supports image blitting with REI_VK_IMAGE_FORMAT.
  VkFormatProperties props;
  vkGetPhysicalDeviceFormatProperties (*out, REI_VK_IMAGE_FORMAT, &props);

  const b32 supports_blitting = props.optimalTilingFeatures & (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT);

  if (!*out && !supports_blitting) {
    REI_LOGS_ERROR ("Vk initialization failure: unable to choose a suitable physical device.");
    exit (EXIT_FAILURE);
  }
}

#ifdef __linux__
   void rei_vk_create_xcb_surface (VkInstance instance, u32 window_handle, xcb_connection_t* xcb_connection, VkSurfaceKHR* out) {
     VkXcbSurfaceCreateInfoKHR create_info = {
       .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
       .pNext = NULL,
       .flags = 0,
       .connection = xcb_connection,
       .window = window_handle
     };

     REI_VK_CHECK (vkCreateXcbSurfaceKHR (instance, &create_info, NULL, out));
   }
#else
#  error "Unhandled platform..."
#endif

void rei_vk_create_device (
  VkPhysicalDevice physical_device,
  VkSurfaceKHR surface,
  const char* const* enabled_ext,
  u32 enabled_ext_count,
  rei_vk_device_t* out) {

  rei_vk_queue_indices_t queue_indices;
  rei_vk_find_queue_indices (physical_device, surface, &queue_indices);

  out->gfx_index = queue_indices.gfx;
  out->present_index = queue_indices.present;

  u32 unique_count = 1;
  u32 previous_index = queue_indices.gfx;

  for (u32 i = 0; i < 3; ++i) {
    u32 current_index = *(((u32*) &queue_indices) + i);

    if (current_index != previous_index) {
      ++unique_count;
      previous_index = current_index;
    }
  }

  // TODO I need to test this on a system with separate gfx and present, etc queues.
  const f32 queue_priority = 1.f;
  VkDeviceQueueCreateInfo* queue_infos = alloca (sizeof *queue_infos * unique_count);

  for (u32 i = 0; i < unique_count; ++i) {
    VkDeviceQueueCreateInfo* current = &queue_infos[i];
    current->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    current->pNext = NULL;
    current->flags = 0;
    // FIXME This is certainly invalid, but I don't care at the moment.
    current->queueFamilyIndex = *(((u32*) &queue_indices) + i);
    current->queueCount = 1;
    current->pQueuePriorities = &queue_priority;
  }

  VkDeviceCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .queueCreateInfoCount = unique_count,
    .pQueueCreateInfos = queue_infos,
    .enabledLayerCount = 0,
    .ppEnabledLayerNames = NULL,
    .enabledExtensionCount = enabled_ext_count,
    .ppEnabledExtensionNames = enabled_ext,
    .pEnabledFeatures = &(VkPhysicalDeviceFeatures) {0},
  };

  REI_VK_CHECK (vkCreateDevice (physical_device, &create_info, NULL, &out->handle));
  volkLoadDevice (out->handle);

  // FIXME I'm not entirely sure that third argument is correct here...
  vkGetDeviceQueue (out->handle, out->gfx_index, 0, &out->gfx_queue);
  vkGetDeviceQueue (out->handle, out->present_index, 0, &out->present_queue);
}

void rei_vk_create_allocator (VkInstance instance, VkPhysicalDevice physical_device, const rei_vk_device_t* device, VmaAllocator* out) {
  // Provide all the function pointers VMA needs to do its stuff.
  VmaVulkanFunctions vk_function_pointers = {
    .vkMapMemory = vkMapMemory,
    .vkFreeMemory = vkFreeMemory,
    .vkUnmapMemory = vkUnmapMemory,
    .vkCreateImage = vkCreateImage,
    .vkDestroyImage = vkDestroyImage,
    .vkCreateBuffer = vkCreateBuffer,
    .vkDestroyBuffer = vkDestroyBuffer,
    .vkCmdCopyBuffer = vkCmdCopyBuffer,
    .vkAllocateMemory = vkAllocateMemory,
    .vkBindImageMemory = vkBindImageMemory,
    .vkBindBufferMemory = vkBindBufferMemory,
    .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
    .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
    .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
    .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
    .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
    .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties
  };

  VmaAllocatorCreateInfo create_info = {
    .flags = 0,
    .physicalDevice = physical_device,
    .device = device->handle,
    .preferredLargeHeapBlockSize = 0,
    .pAllocationCallbacks = NULL,
    .pDeviceMemoryCallbacks = NULL,
    .pHeapSizeLimit = NULL,
    .pVulkanFunctions = &vk_function_pointers,
    .instance = instance,
    .vulkanApiVersion = REI_VK_VERSION,
    .pTypeExternalMemoryHandleTypes = NULL,
  };

  REI_VK_CHECK (vmaCreateAllocator (&create_info, out));
}

void rei_vk_create_image (const rei_vk_device_t* device, VmaAllocator allocator, const rei_vk_image_ci_t* create_info, rei_vk_image_t* out) {
  VkImageCreateInfo image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = create_info->format,
    .extent.depth = 1,
    .extent.width = create_info->width,
    .extent.height = create_info->height,
    .mipLevels = create_info->mip_levels,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = create_info->usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = NULL,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_GPU_ONLY, .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  REI_VK_CHECK (vmaCreateImage (allocator, &image_info, &alloc_info, &out->handle, &out->memory, NULL));

  VkImageViewCreateInfo view_info = {
    .pNext = NULL,
    .image = out->handle,
    .flags = 0,
    .format = create_info->format,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,

    .components.r = VK_COMPONENT_SWIZZLE_R,
    .components.g = VK_COMPONENT_SWIZZLE_G,
    .components.b = VK_COMPONENT_SWIZZLE_B,
    .components.a = VK_COMPONENT_SWIZZLE_A,

    .subresourceRange.levelCount = 1,
    .subresourceRange.layerCount = 1,
    .subresourceRange.baseMipLevel = 0,
    .subresourceRange.baseArrayLayer = 0,
    .subresourceRange.aspectMask = create_info->aspect_mask
  };

  REI_VK_CHECK (vkCreateImageView (device->handle, &view_info, NULL, &out->view));
}

void rei_vk_destroy_image (const rei_vk_device_t* device, VmaAllocator allocator, rei_vk_image_t* image) {
  vkDestroyImageView (device->handle, image->view, NULL);
  vmaDestroyImage (allocator, image->handle, image->memory);
}

void rei_vk_create_buffer (
  VmaAllocator allocator,
  u64 size,
  VkBufferUsageFlags usage,
  u64 vma_memory_usage,
  VkMemoryPropertyFlags memory_flags,
  rei_vk_buffer_t* out) {

  VkBufferCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage};
  VmaAllocationCreateInfo alloc_info = {.usage = (VmaMemoryUsage) vma_memory_usage, .requiredFlags = memory_flags};

  out->size = size;
  REI_VK_CHECK (vmaCreateBuffer (allocator, &create_info, &alloc_info, &out->handle, &out->memory, NULL));
}

void rei_vk_map_buffer (VmaAllocator allocator, rei_vk_buffer_t* buffer) {
  REI_VK_CHECK (vmaMapMemory (allocator, buffer->memory, &buffer->mapped));
}

void rei_vk_unmap_buffer (VmaAllocator allocator, rei_vk_buffer_t* buffer) {
  vmaUnmapMemory (allocator, buffer->memory);
}

void rei_vk_destroy_buffer (VmaAllocator allocator, rei_vk_buffer_t* buffer) {
  vmaDestroyBuffer (allocator, buffer->handle, buffer->memory);
}

void rei_vk_create_swapchain (
  const rei_vk_device_t* device,
  VmaAllocator allocator,
  VkSwapchainKHR old,
  VkSurfaceKHR surface,
  u32 width,
  u32 height,
  VkPhysicalDevice physical_device,
  rei_vk_swapchain_t* out) {

  {
    // Choose swapchain extent
    VkSurfaceCapabilitiesKHR capabilities;
    REI_VK_CHECK (vkGetPhysicalDeviceSurfaceCapabilitiesKHR (physical_device, surface, &capabilities));

    if (capabilities.currentExtent.width != REI_U32_MAX) {
      out->width = capabilities.currentExtent.width;
      out->height = capabilities.currentExtent.height;
    } else {
      out->width = REI_CLAMP (
        width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
      );

      out->height = REI_CLAMP (
        height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
      );
    }

    u32 min_images_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && min_images_count > capabilities.maxImageCount)
      min_images_count = capabilities.maxImageCount;

    // Choose surface format
    VkSurfaceFormatKHR surface_format;
    {
      u32 count = 0;
      REI_VK_CHECK (vkGetPhysicalDeviceSurfaceFormatsKHR (physical_device, surface, &count, NULL));

      VkSurfaceFormatKHR* formats = alloca (sizeof *formats * count);
      REI_VK_CHECK (vkGetPhysicalDeviceSurfaceFormatsKHR (physical_device, surface, &count, formats));

      surface_format = formats[0];
      for (u32 i = 0; i < count; ++i) {
	const VkSurfaceFormatKHR* current = &formats[i];

        if (current->format == REI_VK_IMAGE_FORMAT && current->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
          surface_format = formats[i];
          break;
        }
      }
    }

    out->format = surface_format.format;

    // Choose present mode
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    {
      u32 count = 0;
      REI_VK_CHECK (vkGetPhysicalDeviceSurfacePresentModesKHR (physical_device, surface, &count, NULL));

      VkPresentModeKHR* modes = alloca (sizeof *modes * count);
      REI_VK_CHECK (vkGetPhysicalDeviceSurfacePresentModesKHR (physical_device, surface, &count, modes));

      for (u32 i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
          present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
	  break;
	}
      }
    }

    VkSwapchainCreateInfoKHR info = {
      .clipped = VK_TRUE,
      .presentMode = present_mode,
      .surface = surface,
      .oldSwapchain = old,

      .imageArrayLayers = 1,
      .imageExtent.width = out->width,
      .imageExtent.height = out->height,
      .minImageCount = min_images_count,
      .imageFormat = surface_format.format,
      .imageColorSpace = surface_format.colorSpace,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,

      .preTransform = capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,

      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext = NULL,
      .flags = 0,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = NULL,
    };

    REI_VK_CHECK (vkCreateSwapchainKHR (device->handle, &info, NULL, &out->handle));
  }

  { // Create swapchain images and views
    vkGetSwapchainImagesKHR (device->handle, out->handle, &out->image_count, NULL);
    const u32 image_count = out->image_count;

    out->images = malloc (sizeof *out->images * image_count);
    vkGetSwapchainImagesKHR (device->handle, out->handle, &out->image_count, out->images);

    out->views = malloc (sizeof *out->views * image_count);

    VkImageViewCreateInfo info = {
      .pNext = NULL,
      .flags = 0,
      .format = REI_VK_IMAGE_FORMAT,
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,

      .components.r = VK_COMPONENT_SWIZZLE_R,
      .components.g = VK_COMPONENT_SWIZZLE_G,
      .components.b = VK_COMPONENT_SWIZZLE_B,
      .components.a = VK_COMPONENT_SWIZZLE_A,

      .subresourceRange.levelCount = 1,
      .subresourceRange.layerCount = 1,
      .subresourceRange.baseMipLevel = 0,
      .subresourceRange.baseArrayLayer = 0,
      .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
    };

    for (u32 i = 0; i < image_count; ++i) {
      info.image = out->images[i];
      REI_VK_CHECK (vkCreateImageView (device->handle, &info, NULL, &out->views[i]));
    }
  }

  // Create depth image
  rei_vk_image_ci_t info = {
    .width = out->width,
    .height = out->height,
    .mip_levels = 1,
    .format = REI_VK_DEPTH_FORMAT,
    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
  };

  rei_vk_create_image (device, allocator, &info, &out->depth_image);
}

void rei_vk_destroy_swapchain (const rei_vk_device_t* device, VmaAllocator allocator, rei_vk_swapchain_t* swapchain) {
  rei_vk_destroy_image (device, allocator, &swapchain->depth_image);
  for (u32 i = 0; i < swapchain->image_count; ++i) vkDestroyImageView (device->handle, swapchain->views[i], NULL);
  vkDestroySwapchainKHR (device->handle, swapchain->handle, NULL);

  free (swapchain->views);
  free (swapchain->images);
}

void rei_vk_create_render_pass (const rei_vk_device_t* device, const rei_vk_render_pass_ci_t* create_info, rei_vk_render_pass_t* out) {
  out->clear_value_count = 2;
  out->clear_values = malloc (sizeof *out->clear_values * out->clear_value_count);

  out->clear_values[0].color.float32[0] = create_info->r;
  out->clear_values[0].color.float32[1] = create_info->g;
  out->clear_values[0].color.float32[2] = create_info->b;
  out->clear_values[0].color.float32[3] = create_info->a;
  out->clear_values[1].depthStencil.depth = 1.f;
  out->clear_values[1].depthStencil.stencil = 0;

  { // Create render pass.
    VkAttachmentDescription attachments[2] = {
      [0] = { // Color attachment
        .flags = 0,
        .format = create_info->swapchain->format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE
      },

      [1] = { // Depth attachment
        .flags = 0,
        .format = REI_VK_DEPTH_FORMAT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
      }
    };

    VkAttachmentReference color_reference = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference depth_reference = {
      .attachment = 1,
      .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass = {
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_reference,
      .pDepthStencilAttachment = &depth_reference,
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,

      .flags = 0,
      .inputAttachmentCount = 0,
      .pInputAttachments = NULL,
      .pResolveAttachments = NULL,
      .preserveAttachmentCount = 0,
      .pPreserveAttachments = NULL
    };

    VkRenderPassCreateInfo vk_create_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .subpassCount = 1,
      .attachmentCount = 2,
      .pSubpasses = &subpass,
      .pAttachments = attachments,

      .pNext = NULL,
      .flags = 0,
      .dependencyCount = 0,
      .pDependencies = NULL,
    };

    REI_VK_CHECK (vkCreateRenderPass (device->handle, &vk_create_info, NULL, &out->handle));
  }

  // Create framebuffers for every swapchain image.
  const rei_vk_swapchain_t* swapchain = create_info->swapchain;

  VkFramebufferCreateInfo vk_create_info = {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .renderPass = out->handle,
    .attachmentCount = 2,
    .width = swapchain->width,
    .height = swapchain->height,
    .layers = 1
  };

  out->framebuffer_count = swapchain->image_count;
  out->framebuffers = malloc (sizeof *out->framebuffers * out->framebuffer_count);

  for (u32 i = 0; i < swapchain->image_count; ++i) {
    vk_create_info.pAttachments = (VkImageView[]) {swapchain->views[i], swapchain->depth_image.view};
    REI_VK_CHECK (vkCreateFramebuffer (device->handle, &vk_create_info, NULL, &out->framebuffers[i]));
  }
}

void rei_vk_destroy_render_pass (const rei_vk_device_t* device, rei_vk_render_pass_t* render_pass) {
  for (u32 i = 0; i < render_pass->framebuffer_count; ++i) vkDestroyFramebuffer (device->handle, render_pass->framebuffers[i], NULL);
  free (render_pass->framebuffers);

  free (render_pass->clear_values);
  vkDestroyRenderPass (device->handle, render_pass->handle, NULL);
}

void rei_vk_create_frame_data (const rei_vk_device_t* device, VkCommandPool cmd_pool, rei_vk_frame_data_t* out) {
  VkCommandBufferAllocateInfo buffer_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext = NULL,
    .commandPool = cmd_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1
  };

  VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = NULL, .flags = 0};
  VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = NULL, .flags = VK_FENCE_CREATE_SIGNALED_BIT};

  REI_VK_CHECK (vkAllocateCommandBuffers (device->handle, &buffer_info, &out->cmd_buffer));
  REI_VK_CHECK (vkCreateFence (device->handle, &fence_info, NULL, &out->submit_fence));
  REI_VK_CHECK (vkCreateSemaphore (device->handle, &semaphore_info, NULL, &out->present_semaphore));
  REI_VK_CHECK (vkCreateSemaphore (device->handle, &semaphore_info, NULL, &out->render_semaphore));
}

void rei_vk_destroy_frame_data (const rei_vk_device_t* device, rei_vk_frame_data_t* frame_data) {
  vkDestroySemaphore (device->handle, frame_data->render_semaphore, NULL);
  vkDestroySemaphore (device->handle, frame_data->present_semaphore, NULL);
  vkDestroyFence (device->handle, frame_data->submit_fence, NULL);
}

void rei_vk_create_shader_module (const rei_vk_device_t* device, const char* relative_path, VkShaderModule* out) {
  rei_file_t shader_code;
  REI_CHECK (rei_map_file (relative_path, &shader_code));

  VkShaderModuleCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .codeSize = shader_code.size,
    .pCode = (u32*) shader_code.data,
  };

  REI_VK_CHECK (vkCreateShaderModule (device->handle, &create_info, NULL, out));
  rei_unmap_file (&shader_code);
}

void rei_vk_create_gfx_pipeline (const rei_vk_device_t* device, const rei_vk_gfx_pipeline_ci_t* create_info, VkPipeline* out) {
  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
    .pNext = NULL,
    .flags = 0,
    .primitiveRestartEnable = VK_FALSE,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
    .pNext = NULL,
    .pSampleMask = NULL,
    .flags = 0,
    .minSampleShading = 1.f,
    .alphaToOneEnable = VK_FALSE,
    .sampleShadingEnable = VK_FALSE,
    .alphaToCoverageEnable = VK_FALSE,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
    .pNext = NULL,
    .blendConstants = {0},
    .flags = 0,
    .logicOpEnable = VK_FALSE,
    .logicOp = VK_LOGIC_OP_NO_OP,
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .pAttachments = create_info->color_blend_attachments,
    .attachmentCount = create_info->color_blend_attachment_count,
  };

  VkShaderModule vertex_shader, pixel_shader;
  rei_vk_create_shader_module (device, create_info->vertex_shader_path, &vertex_shader);
  rei_vk_create_shader_module (device, create_info->pixel_shader_path, &pixel_shader);

  VkPipelineShaderStageCreateInfo shader_stages[2] = {
    [0] = {
      .pName = "main",
      .pNext = NULL,
      .flags = 0,
      .module = vertex_shader,
      .pSpecializationInfo = NULL,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
    },

    [1] = {
      .pName = "main",
      .pNext = NULL,
      .module = pixel_shader,
      .flags = 0,
      .pSpecializationInfo = NULL,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
    }
  };

  VkGraphicsPipelineCreateInfo info = {
    .pNext = NULL,
    .basePipelineIndex = 0,
    .flags = 0,
    .layout = create_info->layout,
    .subpass = create_info->subpass_index,
    .renderPass = create_info->render_pass,
    .basePipelineHandle = VK_NULL_HANDLE,
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = REI_ARRAY_SIZE (shader_stages),

    .pStages = shader_stages,
    .pTessellationState = NULL,
    .pColorBlendState = &color_blend_state,
    .pMultisampleState = &multisample_state,
    .pInputAssemblyState = &input_assembly_state,
    .pViewportState = create_info->viewport_state,
    .pDynamicState = create_info->dynamic_state,
    .pVertexInputState = create_info->vertex_input_state,
    .pDepthStencilState = create_info->depth_stencil_state,
    .pRasterizationState = create_info->rasterization_state
  };

  REI_VK_CHECK (vkCreateGraphicsPipelines (device->handle, create_info->cache, 1, &info, NULL, out));

  vkDestroyShaderModule (device->handle, pixel_shader, NULL);
  vkDestroyShaderModule (device->handle, vertex_shader, NULL);
}

void rei_vk_create_imm_ctxt (const rei_vk_device_t* device, u32 queue_index, rei_vk_imm_ctxt_t* out) {
  VkCommandPoolCreateInfo cmd_pool_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .queueFamilyIndex = queue_index
  };

  REI_VK_CHECK (vkCreateCommandPool (device->handle, &cmd_pool_info, NULL, &out->cmd_pool));

  VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = NULL, .flags = 0};

  REI_VK_CHECK (vkCreateFence (device->handle, &fence_info, NULL, &out->fence));
  vkGetDeviceQueue (device->handle, queue_index, 0, &out->queue);
}

void rei_vk_destroy_imm_ctxt (const rei_vk_device_t* device, rei_vk_imm_ctxt_t* context) {
  vkDestroyFence (device->handle, context->fence, NULL);
  vkDestroyCommandPool (device->handle, context->cmd_pool, NULL);
}

void rei_vk_start_imm_cmd (const rei_vk_device_t* device, const rei_vk_imm_ctxt_t* context, VkCommandBuffer* out) {
  VkCommandBufferAllocateInfo alloc_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext = NULL,
    .commandPool = context->cmd_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1
  };

  REI_VK_CHECK (vkAllocateCommandBuffers (device->handle, &alloc_info, out));

  VkCommandBufferBeginInfo begin_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = NULL,
    .flags = 0,
    .pInheritanceInfo = NULL
  };

  REI_VK_CHECK (vkBeginCommandBuffer (*out, &begin_info));
}

void rei_vk_end_imm_cmd (const rei_vk_device_t* device, const rei_vk_imm_ctxt_t* context, VkCommandBuffer cmd_buffer) {
  REI_VK_CHECK (vkEndCommandBuffer (cmd_buffer));

  VkSubmitInfo submit_info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext = NULL,
    .waitSemaphoreCount = 0,
    .pWaitSemaphores = NULL,
    .pWaitDstStageMask = NULL,
    .commandBufferCount = 1,
    .pCommandBuffers = &cmd_buffer,
    .signalSemaphoreCount = 0,
    .pSignalSemaphores = NULL,
  };

  REI_VK_CHECK (vkQueueSubmit (context->queue, 1, &submit_info, context->fence));
  REI_VK_CHECK (vkWaitForFences (device->handle, 1, &context->fence, VK_TRUE, ~0ull));

  REI_VK_CHECK (vkResetFences (device->handle, 1, &context->fence));
  REI_VK_CHECK (vkResetCommandPool (device->handle, context->cmd_pool, 0));
}

void rei_vk_create_sampler (const rei_vk_device_t* device, f32 min_lod, f32 max_lod, VkFilter filter, VkSampler* out) {
  VkSamplerCreateInfo create_info = {
    .minLod = min_lod,
    .maxLod = max_lod,
    .pNext = NULL,
    .mipLodBias = 0.f,
    .maxAnisotropy = 1.f,
    .flags = 0,
    .compareEnable = VK_FALSE,
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .anisotropyEnable = VK_FALSE,
    .magFilter = filter,
    .minFilter = filter,
    .compareOp = VK_COMPARE_OP_NEVER,
    .unnormalizedCoordinates = VK_FALSE,
    .mipmapMode = filter == VK_FILTER_NEAREST ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR,
    .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
  };

  REI_VK_CHECK (vkCreateSampler (device->handle, &create_info, NULL, out));
}

void rei_vk_transition_image_cmd (VkCommandBuffer cmd_buffer, const rei_vk_image_trans_info_t* trans_info, VkImage image) {
  VkImageMemoryBarrier barrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .image = image,
    .oldLayout = trans_info->old_layout,
    .newLayout = trans_info->new_layout,
    .subresourceRange = *trans_info->subresource_range,
  };

  switch (trans_info->old_layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      barrier.srcAccessMask = 0;
      break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      break;

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
      break;

    default:
      barrier.srcAccessMask = 0;
      break;
  }

  switch (trans_info->new_layout) {
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
      break;

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
      break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      break;

    default:
      barrier.dstAccessMask = 0;
      break;
  }

  vkCmdPipelineBarrier (cmd_buffer, trans_info->src_stage, trans_info->dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

void rei_vk_copy_buffer_cmd (VkCommandBuffer cmd_buffer, u64 size, u64 src_offset, const rei_vk_buffer_t* src, rei_vk_buffer_t* dst) {
  VkBufferCopy copy_info = {
    .srcOffset = src_offset,
    .dstOffset = 0,
    .size = size
  };

  vkCmdCopyBuffer (cmd_buffer, src->handle, dst->handle, 1, &copy_info);
}

void rei_vk_create_texture (
  const rei_vk_device_t* device,
  VmaAllocator allocator,
  const rei_vk_imm_ctxt_t* context,
  u8* pixels,
  u32 width,
  u32 height,
  rei_vk_image_t* out) {

  rei_vk_buffer_t staging;

  rei_vk_create_buffer (
    allocator,
    width * height * 4,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    VMA_MEMORY_USAGE_CPU_ONLY,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    &staging
  );

  REI_VK_CHECK (vmaMapMemory (allocator, staging.memory, &staging.mapped));
  memcpy (staging.mapped, pixels, width * height * 4);
  vmaUnmapMemory (allocator, staging.memory);

  {
    rei_vk_image_ci_t create_info = {
      .width = width,
      .height = height,
      .mip_levels = 1,
      .format = VK_FORMAT_R8G8B8A8_SRGB,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
    };

    rei_vk_create_image (device, allocator, &create_info, out);
  }

  VkImageSubresourceRange subresource_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1
  };

  VkCommandBuffer cmd_buffer;
  rei_vk_start_imm_cmd (device, context, &cmd_buffer);

  {
    rei_vk_image_trans_info_t trans_info = {
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .subresource_range = &subresource_range,
    };

    rei_vk_transition_image_cmd (cmd_buffer, &trans_info, out->handle);
  }

  {
    VkBufferImageCopy copy_info = {
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource.aspectMask = subresource_range.aspectMask,
      .imageSubresource.mipLevel = 0,
      .imageSubresource.baseArrayLayer = 0,
      .imageSubresource.layerCount = 1,
      .imageOffset.x = 0,
      .imageOffset.y = 0,
      .imageOffset.z = 0,
      .imageExtent.width = width,
      .imageExtent.height = height,
      .imageExtent.depth = 1
    };

    vkCmdCopyBufferToImage (cmd_buffer, staging.handle, out->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_info);
  }

  {
    rei_vk_image_trans_info_t trans_info = {
      .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      .subresource_range = &subresource_range,
    };

    rei_vk_transition_image_cmd (cmd_buffer, &trans_info, out->handle);
  }

  rei_vk_end_imm_cmd (device, context, cmd_buffer);
  rei_vk_destroy_buffer (allocator, &staging);
}

void rei_vk_create_texture_mipmapped (
  const rei_vk_device_t* device,
  VmaAllocator allocator,
  const rei_vk_imm_ctxt_t* context,
  u8* pixels,
  u32 width,
  u32 height,
  rei_vk_image_t* out) {

  // Calculate number of mip levels.
  const u32 mip_levels = (u32) (floorf (log2f ((f32) REI_MAX (width, height)))) + 1;

  {
    rei_vk_image_ci_t create_info = {
      .width = width,
      .height = height,
      .mip_levels = mip_levels,
      .format = VK_FORMAT_R8G8B8A8_SRGB,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
    };

    rei_vk_create_image (device, allocator, &create_info, out);
  }

  rei_vk_buffer_t staging;
  const u64 staging_size = (u64) (width * height * 4);

  rei_vk_create_buffer (
    allocator,
    staging_size,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    VMA_MEMORY_USAGE_CPU_ONLY,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    &staging
  );

  REI_VK_CHECK (vmaMapMemory (allocator, staging.memory, &staging.mapped));
  memcpy (staging.mapped, pixels, staging_size);
  memset (staging.mapped, 1, staging_size);
  vmaUnmapMemory (allocator, staging.memory);

  VkImageSubresourceRange subresource_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = mip_levels,
    .baseArrayLayer = 0,
    .layerCount = 1
  };

  VkCommandBuffer cmd_buffer;
  rei_vk_start_imm_cmd (device, context, &cmd_buffer);

  {
    rei_vk_image_trans_info_t trans_info = {
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .subresource_range = &subresource_range,
    };

    rei_vk_transition_image_cmd (cmd_buffer, &trans_info, out->handle);
  }

  { // Copy first mip level from the staging buffer.
    VkBufferImageCopy copy_info = {
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,

      .imageSubresource.aspectMask = subresource_range.aspectMask,
      .imageSubresource.mipLevel = 0,
      .imageSubresource.baseArrayLayer = 0,
      .imageSubresource.layerCount = 1,

      .imageOffset.x = 0,
      .imageOffset.y = 0,
      .imageOffset.z = 0,

      .imageExtent.width = width,
      .imageExtent.height = height,
      .imageExtent.depth = 1
    };

    vkCmdCopyBufferToImage (cmd_buffer, staging.handle, out->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_info);
  }

  { // Prepare image to be the source for the next mip level.
    rei_vk_image_trans_info_t trans_info = {
      .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .subresource_range = &subresource_range,
    };

    rei_vk_transition_image_cmd (cmd_buffer, &trans_info, out->handle);
  }

  subresource_range.levelCount = 1;
  for (u32 i = 1; i < mip_levels; ++i) {
    subresource_range.baseMipLevel = i;

    {
      rei_vk_image_trans_info_t trans_info = {
        .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .subresource_range = &subresource_range
      };

      rei_vk_transition_image_cmd (cmd_buffer, &trans_info, out->handle);
    }

    VkImageBlit blit = {
      .srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .srcSubresource.baseArrayLayer = 0,
      .srcSubresource.layerCount = 1,
      .srcSubresource.mipLevel = i - 1,

      .srcOffsets[0] = {.x = 0, .y = 0, .z = 0},
      .srcOffsets[1] = {.x = (s32) (width >> (i - 1)), .y = (s32) (height >> (i - 1)), .z = 1},

      .dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .dstSubresource.baseArrayLayer = 0,
      .dstSubresource.layerCount = 1,
      .dstSubresource.mipLevel = i,

      .dstOffsets[0] = {.x = 0, .y = 0, .z = 0},
      .dstOffsets[1] = {.x = (s32) (width >> i), .y = (s32) (height >> i), .z = 1},
    };

    vkCmdBlitImage (
      cmd_buffer,
      out->handle,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      out->handle,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1, &blit,
      VK_FILTER_NEAREST
    );

    rei_vk_image_trans_info_t trans_info = {
      .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .subresource_range = &subresource_range,
    };

    rei_vk_transition_image_cmd (cmd_buffer, &trans_info, out->handle);
  }

  subresource_range.baseMipLevel = 0;
  subresource_range.levelCount = mip_levels;

  // Prepare image for sampling in a pixel shader.
  rei_vk_image_trans_info_t trans_info = {
    .old_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
    .dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    .subresource_range = &subresource_range
  };

  rei_vk_transition_image_cmd (cmd_buffer, &trans_info, out->handle);

  rei_vk_end_imm_cmd (device, context, cmd_buffer);
  rei_vk_destroy_buffer (allocator, &staging);
}
