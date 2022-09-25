#include <alloca.h>
#include <string.h>
#include <malloc.h>

#include "rei_vk.h"
#include "rei_file.h"
#include "rei_debug.h"
#include "rei_defines.h"

#include <pthread.h>
#include <lz4/lib/lz4.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

#define _S_VK_VERSION VK_API_VERSION_1_0
#define _S_IMAGE_FORMAT VK_FORMAT_B8G8R8A8_SRGB
#define _S_TEXTURE_FORMAT VK_FORMAT_R8G8B8A8_SRGB
#define _S_DEPTH_FORMAT VK_FORMAT_X8_D24_UNORM_PACK32

#define _S_REQUIRED_DEVICE_EXT(__out) const char* const __out[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME}
#define _S_REQUIRED_DEVICE_EXT_COUNT(__out) const u32 __out = 1

struct _s_queue_indices_t {
  u32 gfx;
  u32 present;
  u32 transfer;
};

REI_IGNORE_WARN_START (-Wunused-function)

static VKAPI_ATTR VkBool32 VKAPI_CALL _s_vk_debug_callback (
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

REI_IGNORE_WARN_STOP

u32 _s_check_extensions (const VkExtensionProperties* available, u32 available_count, const char* const* required, u32 required_count) {
  u32 matched_count = 0;

  for (u32 i = 0; i < required_count; ++i) {
    for (u32 j = 0; j < available_count; ++j) {
      if (!strcmp (available[j].extensionName, required[i])) {
        ++matched_count;
        break;
      }
    }
  }

  return matched_count;
}

b8 _s_find_queue_indices (VkPhysicalDevice device, VkSurfaceKHR surface, struct _s_queue_indices_t* out) {
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

void _s_choose_gpu (rei_vk_instance_t* out) {
  out->gpu = VK_NULL_HANDLE;

  u32 device_count = 0;
  REI_VK_CHECK (vkEnumeratePhysicalDevices (out->handle, &device_count, NULL));

  VkPhysicalDevice* devices = alloca (sizeof *devices * device_count);
  REI_VK_CHECK (vkEnumeratePhysicalDevices (out->handle, &device_count, devices));

  _S_REQUIRED_DEVICE_EXT (required_ext);
  _S_REQUIRED_DEVICE_EXT_COUNT (required_ext_count);

  for (u32 i = 0; i < device_count; ++i) {
    const VkPhysicalDevice current = devices[i];

    u32 ext_count = 0;
    REI_VK_CHECK (vkEnumerateDeviceExtensionProperties (current, NULL, &ext_count, NULL));

    VkExtensionProperties* extensions = alloca (sizeof *extensions * ext_count);
    REI_VK_CHECK (vkEnumerateDeviceExtensionProperties (current, NULL, &ext_count, extensions));

    const u32 matched_ext_count = _s_check_extensions (extensions, ext_count, required_ext, required_ext_count);

    u32 format_count = 0, present_mode_count = 0;
    REI_VK_CHECK (vkGetPhysicalDeviceSurfaceFormatsKHR (current, out->surface, &format_count, NULL));
    REI_VK_CHECK (vkGetPhysicalDeviceSurfacePresentModesKHR (current, out->surface, &present_mode_count, NULL));

    struct _s_queue_indices_t indices;
    const b8 supports_swapchain = format_count && present_mode_count;
    const b8 has_queue_families = _s_find_queue_indices (current, out->surface, &indices);

    if (has_queue_families && supports_swapchain && (matched_ext_count == required_ext_count)) {
      out->gpu = current;
      break;
    }
  }

  if (!out->gpu) {
    REI_LOG_STR_ERROR ("Vk initialization failure: unable to choose a suitable physical device.");
    exit (EXIT_FAILURE);
  }
}


static void _s_set_swapchain_surface_format (const rei_vk_instance_t* instance, VkSurfaceFormatKHR* out) {
  u32 available_count = 0;
  REI_VK_CHECK (vkGetPhysicalDeviceSurfaceFormatsKHR (instance->gpu, instance->surface, &available_count, NULL));

  VkSurfaceFormatKHR* available = alloca (sizeof *available * available_count);
  REI_VK_CHECK (vkGetPhysicalDeviceSurfaceFormatsKHR (instance->gpu, instance->surface, &available_count, available));

  const VkFormat supported[] = {VK_FORMAT_B8G8R8A8_SRGB};
  const u32 supported_count = REI_ARRAY_SIZE (supported);

  for (u32 i = 0; i < available_count; ++i) {
    const VkSurfaceFormatKHR* current = &available[i];

    for (u32 j = 0; j < supported_count; ++j) {
      if (current->format == supported[j] && current->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        *out = available[i];
        break;
      }
    }
  }
}

static void _s_set_swapchain_present_mode (const rei_vk_instance_t* instance, VkPresentModeKHR* out) {
  // Set out to be FIFO by default since it is present on all the devices.
  *out = VK_PRESENT_MODE_FIFO_KHR;

  u32 count = 0;
  REI_VK_CHECK (vkGetPhysicalDeviceSurfacePresentModesKHR (instance->gpu, instance->surface, &count, NULL));

  VkPresentModeKHR* modes = alloca (sizeof *modes * count);
  REI_VK_CHECK (vkGetPhysicalDeviceSurfacePresentModesKHR (instance->gpu, instance->surface, &count, modes));

  // But prefer Mailbox.
  for (u32 i = 0; i < count; ++i) {
    if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
      *out = VK_PRESENT_MODE_MAILBOX_KHR;
      break;
    }
  }
}

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

void rei_vk_create_instance_linux (xcb_connection_t* xcb_conn, u32 xcb_window, rei_vk_instance_t* out) {
  const char* const required_ext[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
    #ifndef NDEBUG
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME
    #endif
  };

  u32 required_ext_count = REI_ARRAY_SIZE (required_ext);

  // Check support for required extensions.
  u32 available_count = 0;
  REI_VK_CHECK (vkEnumerateInstanceExtensionProperties (NULL, &available_count, NULL));

  VkExtensionProperties* available = alloca (sizeof *available * available_count);
  REI_VK_CHECK (vkEnumerateInstanceExtensionProperties (NULL, &available_count, available));

  u32 matched_count = _s_check_extensions (available, available_count, required_ext, required_ext_count);

  if (matched_count != required_ext_count) {
    REI_LOG_STR_ERROR ("Vk initialization failure: required vulkan extensions aren't supported by this device.");
    for (u32 i = 0; i < required_ext_count; ++i) REI_LOG_ERROR ("\t%s", required_ext[i]);
    exit (EXIT_FAILURE);
  }

  VkInstanceCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &(VkApplicationInfo) {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = _S_VK_VERSION},
    .ppEnabledExtensionNames = required_ext,
    .enabledExtensionCount = required_ext_count,
  };

  #ifndef NDEBUG
  VkDebugUtilsMessengerCreateInfoEXT dbg_messenger_ci = {
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .pNext = NULL,
    .pUserData = NULL,
    .flags = 0,
    .pfnUserCallback = _s_vk_debug_callback,
    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
  };

  create_info.pNext = &dbg_messenger_ci;
  create_info.enabledLayerCount = 1;
  create_info.ppEnabledLayerNames = (const char*[]) {"VK_LAYER_KHRONOS_validation"};
  #endif

  REI_VK_CHECK (vkCreateInstance (&create_info, NULL, &out->handle));
  volkLoadInstanceOnly (out->handle);

  #ifndef NDEBUG
  REI_VK_CHECK (vkCreateDebugUtilsMessengerEXT (out->handle, &dbg_messenger_ci, NULL, &out->dbg_messenger));
  #endif

  // Create XCB window surface.
  VkXcbSurfaceCreateInfoKHR surface_ci = {
    .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
    .pNext = NULL,
    .flags = 0,
    .connection = xcb_conn,
    .window = xcb_window
  };

  REI_VK_CHECK (vkCreateXcbSurfaceKHR (out->handle, &surface_ci, NULL, &out->surface));

  _s_choose_gpu (out);
}

void rei_vk_destroy_instance (rei_vk_instance_t* instance) {
  vkDestroySurfaceKHR (instance->handle, instance->surface, NULL);
  #ifndef NDEBUG
  vkDestroyDebugUtilsMessengerEXT (instance->handle, instance->dbg_messenger, NULL);
  #endif
  vkDestroyInstance (instance->handle, NULL);
}

void rei_vk_create_device (const rei_vk_instance_t* instance, rei_vk_device_t* out) {
  struct _s_queue_indices_t queue_indices;
  _s_find_queue_indices (instance->gpu, instance->surface, &queue_indices);

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

  _S_REQUIRED_DEVICE_EXT (enabled_ext);
  _S_REQUIRED_DEVICE_EXT_COUNT (enabled_ext_count);

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

  REI_VK_CHECK (vkCreateDevice (instance->gpu, &create_info, NULL, &out->handle));
  volkLoadDevice (out->handle);

  // FIXME I'm not entirely sure that third argument is correct here...
  vkGetDeviceQueue (out->handle, out->gfx_index, 0, &out->gfx_queue);
  vkGetDeviceQueue (out->handle, out->present_index, 0, &out->present_queue);
}

void rei_vk_create_allocator (const rei_vk_instance_t* instance, const rei_vk_device_t* device, rei_vk_allocator_t* out) {
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
    .physicalDevice = instance->gpu,
    .device = device->handle,
    .preferredLargeHeapBlockSize = 0,
    .pAllocationCallbacks = NULL,
    .pDeviceMemoryCallbacks = NULL,
    .pHeapSizeLimit = NULL,
    .pVulkanFunctions = &vk_function_pointers,
    .instance = instance->handle,
    .vulkanApiVersion = _S_VK_VERSION,
    .pTypeExternalMemoryHandleTypes = NULL,
  };

  REI_VK_CHECK (vmaCreateAllocator (&create_info, &out->vma_handle));

  pthread_mutex_init (&out->mutex, NULL);
}

void rei_vk_destroy_allocator (rei_vk_allocator_t* allocator) {
  pthread_mutex_destroy (&allocator->mutex);
  vmaDestroyAllocator (allocator->vma_handle);
}

void rei_vk_flush_buffers (rei_vk_allocator_t* allocator, u32 count, rei_vk_buffer_t* buffers) {
  VmaAllocation* allocations = alloca (sizeof *allocations * count);
  for (u32 i = 0; i < count; ++i) allocations[i] = buffers[i].memory;

  REI_VK_CHECK (vmaFlushAllocations (allocator->vma_handle, count, allocations, NULL, NULL));
}

void rei_vk_create_image (
  const rei_vk_device_t* device,
  rei_vk_allocator_t* allocator,
  const rei_vk_image_ci_t* create_info,
  rei_vk_image_t* out) {

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

  REI_VK_CHECK (vmaCreateImage (allocator->vma_handle, &image_info, &alloc_info, &out->handle, &out->memory, NULL));

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

void rei_vk_destroy_image (const rei_vk_device_t* device, rei_vk_allocator_t* allocator, rei_vk_image_t* image) {
  vkDestroyImageView (device->handle, image->view, NULL);
  vmaDestroyImage (allocator->vma_handle, image->handle, image->memory);
}

void rei_vk_create_buffer (rei_vk_allocator_t* allocator, u64 size, rei_vk_buffer_type_e type, rei_vk_buffer_t* out) {
  // Lookup tables for which buffer type serves as an index. Alternative to ugly ifs/switches.
  static const VmaMemoryUsage vma_mem_usage_flags[] = {
    VMA_MEMORY_USAGE_CPU_ONLY,
    VMA_MEMORY_USAGE_GPU_ONLY,
    VMA_MEMORY_USAGE_GPU_ONLY,
    VMA_MEMORY_USAGE_CPU_TO_GPU,
    VMA_MEMORY_USAGE_CPU_TO_GPU,
  };

  static const VkBufferUsageFlags buffer_usage_flags[] = {
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
  };

  static const VkMemoryPropertyFlags mem_property_flags[] = {
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };

  const VkBufferCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = buffer_usage_flags[type]};
  const VmaAllocationCreateInfo alloc_info = {.usage = vma_mem_usage_flags[type], .requiredFlags = mem_property_flags[type]};

  out->size = size;
  REI_VK_CHECK (vmaCreateBuffer (allocator->vma_handle, &create_info, &alloc_info, &out->handle, &out->memory, NULL));
}

void rei_vk_map_buffer (rei_vk_allocator_t* allocator, rei_vk_buffer_t* buffer) {
  pthread_mutex_lock (&allocator->mutex);
  REI_VK_CHECK (vmaMapMemory (allocator->vma_handle, buffer->memory, &buffer->mapped));
  pthread_mutex_unlock (&allocator->mutex);
}

void rei_vk_unmap_buffer (rei_vk_allocator_t* allocator, rei_vk_buffer_t* buffer) {
  pthread_mutex_lock (&allocator->mutex);
  vmaUnmapMemory (allocator->vma_handle, buffer->memory);
  pthread_mutex_unlock (&allocator->mutex);
}

void rei_vk_destroy_buffer (rei_vk_allocator_t* allocator, rei_vk_buffer_t* buffer) {
  vmaDestroyBuffer (allocator->vma_handle, buffer->handle, buffer->memory);
}

void rei_vk_create_swapchain (
  const rei_vk_instance_t* instance,
  const rei_vk_device_t* device,
  rei_vk_allocator_t* allocator,
  VkSwapchainKHR old,
  u32 width,
  u32 height,
  rei_vk_swapchain_t* out) {

  {
    // Choose swapchain extent
    VkSurfaceCapabilitiesKHR capabilities;
    REI_VK_CHECK (vkGetPhysicalDeviceSurfaceCapabilitiesKHR (instance->gpu, instance->surface, &capabilities));

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

    VkSurfaceFormatKHR surface_format;
    _s_set_swapchain_surface_format (instance, &surface_format);
    out->format = surface_format.format;

    VkPresentModeKHR present_mode;
    _s_set_swapchain_present_mode (instance, &present_mode);

    VkSwapchainCreateInfoKHR info = {
      .clipped = VK_TRUE,
      .presentMode = present_mode,
      .surface = instance->surface,
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

    out->images = malloc (sizeof *out->images);
    out->images->handles = malloc (sizeof *out->images->handles * out->image_count);
    out->images->views = malloc (sizeof *out->images->views * out->image_count);

    vkGetSwapchainImagesKHR (device->handle, out->handle, &out->image_count, out->images->handles);

    VkImageViewCreateInfo info = {
      .pNext = NULL,
      .flags = 0,
      .format = _S_IMAGE_FORMAT,
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

    for (u32 i = 0; i < out->image_count; ++i) {
      info.image = out->images->handles[i];
      REI_VK_CHECK (vkCreateImageView (device->handle, &info, NULL, &out->images->views[i]));
    }
  }

  // Create depth image
  rei_vk_image_ci_t info = {
    .width = out->width,
    .height = out->height,
    .mip_levels = 1,
    .format = _S_DEPTH_FORMAT,
    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
  };

  rei_vk_create_image (device, allocator, &info, &out->images->depth_image);
}

void rei_vk_destroy_swapchain (const rei_vk_device_t* device, rei_vk_allocator_t* allocator, rei_vk_swapchain_t* swapchain) {
  rei_vk_destroy_image (device, allocator, &swapchain->images->depth_image);
  for (u32 i = 0; i < swapchain->image_count; ++i) vkDestroyImageView (device->handle, swapchain->images->views[i], NULL);
  vkDestroySwapchainKHR (device->handle, swapchain->handle, NULL);

  free (swapchain->images->views);
  free (swapchain->images->handles);
  free (swapchain->images);
}

void rei_vk_create_render_pass (
  const rei_vk_device_t* device,
  const rei_vk_swapchain_t* swapchain,
  const rei_vec4_u* clear_color,
  rei_vk_render_pass_t* out) {

  out->clear_value_count = 2;
  out->clear_values = malloc (sizeof *out->clear_values * out->clear_value_count);

  out->clear_values[0].color.float32[0] = clear_color->x;
  out->clear_values[0].color.float32[1] = clear_color->y;
  out->clear_values[0].color.float32[2] = clear_color->z;
  out->clear_values[0].color.float32[3] = clear_color->w;
  out->clear_values[1].depthStencil.depth = 1.f;
  out->clear_values[1].depthStencil.stencil = 0;

  { // Create render pass.
    VkAttachmentDescription attachments[2] = {
      [0] = { // Color attachment
        .flags = 0,
        .format = swapchain->format,
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
        .format = _S_DEPTH_FORMAT,
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
    vk_create_info.pAttachments = (VkImageView[]) {swapchain->images->views[i], swapchain->images->depth_image.view};
    REI_VK_CHECK (vkCreateFramebuffer (device->handle, &vk_create_info, NULL, &out->framebuffers[i]));
  }
}

void rei_vk_destroy_render_pass (const rei_vk_device_t* device, rei_vk_render_pass_t* render_pass) {
  for (u32 i = 0; i < render_pass->framebuffer_count; ++i) vkDestroyFramebuffer (device->handle, render_pass->framebuffers[i], NULL);
  free (render_pass->framebuffers);

  free (render_pass->clear_values);
  vkDestroyRenderPass (device->handle, render_pass->handle, NULL);
}

void rei_vk_create_cmd_pool (const rei_vk_device_t* device, u32 queue_index, VkCommandPoolCreateFlags flags, VkCommandPool* out) {
  VkCommandPoolCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = NULL,
    .flags = flags,
    .queueFamilyIndex = queue_index,
  };

  REI_VK_CHECK (vkCreateCommandPool (device->handle, &create_info, NULL, out));
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

u32 rei_vk_begin_frame (
  const rei_vk_device_t* device,
  const rei_vk_render_pass_t* render_pass,
  const rei_vk_frame_data_t* current_frame,
  const rei_vk_swapchain_t* swapchain,
  VkCommandBuffer* out) {

  VkCommandBuffer cmd_buffer = current_frame->cmd_buffer;
  VkFence submit_fence = current_frame->submit_fence;
  VkSemaphore present_semaphore = current_frame->present_semaphore;

  REI_VK_CHECK (vkWaitForFences (device->handle, 1, &submit_fence, VK_TRUE, ~0ull));
  REI_VK_CHECK (vkResetFences (device->handle, 1, &submit_fence));

  u32 image_index = 0;
  REI_VK_CHECK (vkAcquireNextImageKHR (device->handle, swapchain->handle, ~0ull, present_semaphore, VK_NULL_HANDLE, &image_index));

  const VkCommandBufferBeginInfo cmd_begin_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = NULL,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = NULL,
  };

  REI_VK_CHECK (vkBeginCommandBuffer (cmd_buffer, &cmd_begin_info));

  VkRenderPassBeginInfo rndr_begin_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .pNext = NULL,
    .renderPass = render_pass->handle,
    .framebuffer = render_pass->framebuffers[image_index],
    .renderArea.extent.width = swapchain->width,
    .renderArea.extent.height = swapchain->height,
    .renderArea.offset.x = 0,
    .renderArea.offset.y = 0,
    .clearValueCount = render_pass->clear_value_count,
    .pClearValues = render_pass->clear_values
  };

  vkCmdBeginRenderPass (cmd_buffer, &rndr_begin_info, VK_SUBPASS_CONTENTS_INLINE);

  *out = cmd_buffer;
  return image_index;
}

void rei_vk_end_frame (
  const rei_vk_device_t* device,
  const rei_vk_frame_data_t* current_frame,
  const rei_vk_swapchain_t* swapchain,
  u32 image_index) {

  vkCmdEndRenderPass (current_frame->cmd_buffer);
  REI_VK_CHECK (vkEndCommandBuffer (current_frame->cmd_buffer));

  VkSubmitInfo submit_info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext = NULL,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &current_frame->present_semaphore,
    .pWaitDstStageMask = (VkPipelineStageFlags[]) {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
    .commandBufferCount = 1,
    .pCommandBuffers = &current_frame->cmd_buffer,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = &current_frame->render_semaphore,
  };

  REI_VK_CHECK (vkQueueSubmit (device->gfx_queue, 1, &submit_info, current_frame->submit_fence));

  VkPresentInfoKHR present_info = {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .pNext = NULL,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &current_frame->render_semaphore,
    .swapchainCount = 1,
    .pSwapchains = &swapchain->handle,
    .pImageIndices = &image_index,
    .pResults = NULL
  };

  REI_VK_CHECK (vkQueuePresentKHR (device->present_queue, &present_info));
}

void rei_vk_destroy_frame_data (const rei_vk_device_t* device, rei_vk_frame_data_t* frame_data) {
  vkDestroySemaphore (device->handle, frame_data->render_semaphore, NULL);
  vkDestroySemaphore (device->handle, frame_data->present_semaphore, NULL);
  vkDestroyFence (device->handle, frame_data->submit_fence, NULL);
}

void rei_vk_create_shader_module (const rei_vk_device_t* device, const char* relative_path, VkShaderModule* out) {
  rei_file_t shader_code;
  REI_CHECK (rei_read_file (relative_path, &shader_code));

  VkShaderModuleCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .codeSize = shader_code.size,
    .pCode = (u32*) shader_code.data,
  };

  REI_VK_CHECK (vkCreateShaderModule (device->handle, &create_info, NULL, out));
  rei_free_file (&shader_code);
}

void rei_vk_create_pipeline_layout (
  const rei_vk_device_t* device,
  u32 desc_count,
  const VkDescriptorSetLayout* desc_layouts,
  u32 push_count,
  const VkPushConstantRange* push_constants,
  VkPipelineLayout* out) {

  VkPipelineLayoutCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .setLayoutCount = desc_count,
    .pSetLayouts = desc_layouts,
    .pushConstantRangeCount = push_count,
    .pPushConstantRanges = push_constants
  };

  REI_VK_CHECK (vkCreatePipelineLayout (device->handle, &create_info, NULL, out));
}

void rei_vk_create_pipeline_cache (const rei_vk_device_t* device, const char* relative_path, VkPipelineCache* out) {
  u64 cache_size = 0;
  void* cache_data = NULL;

  rei_file_t cache_file;
  switch (rei_read_file (relative_path, &cache_file)) {
    case REI_RESULT_SUCCESS:
      REI_LOG_INFO ("Reusing pipeline cache from %s...", relative_path);
      cache_size = cache_file.size;
      cache_data = cache_file.data;
      break;

    case REI_RESULT_FILE_DOES_NOT_EXIST:
      REI_LOG_WARN ("Failed to obtain pipeline cache data from %s, creating one from scratch.", relative_path);
      break;

    default: break;
  }

  const VkPipelineCacheCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .initialDataSize = cache_size,
    .pInitialData = cache_data
  };

  REI_VK_CHECK (vkCreatePipelineCache (device->handle, &create_info, NULL, out));
  rei_free_file (&cache_file);
}

void rei_vk_destroy_pipeline_cache (const rei_vk_device_t* device, VkPipelineCache cache, const char* out_relative_path) {
  u64 cache_size = 0;
  REI_VK_CHECK (vkGetPipelineCacheData (device->handle, cache, &cache_size, NULL));

  // TODO Add a proper check for file size or something, cause I might get stack overflow in case it's too big.
  void* cache_data = alloca (cache_size);
  REI_VK_CHECK (vkGetPipelineCacheData (device->handle, cache, &cache_size, cache_data));

  rei_write_file (out_relative_path, cache_data, cache_size);
  vkDestroyPipelineCache (device->handle, cache, NULL);
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
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT
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

void rei_vk_copy_buffer_cmd (
  VkCommandBuffer cmd_buffer,
  u64 size,
  u64 src_offset,
  const rei_vk_buffer_t* restrict src,
  rei_vk_buffer_t* restrict dst) {

  VkBufferCopy copy_info = {
    .srcOffset = src_offset,
    .dstOffset = 0,
    .size = size
  };

  vkCmdCopyBuffer (cmd_buffer, src->handle, dst->handle, 1, &copy_info);
}

void rei_vk_copy_buffer_to_image_cmd (
  VkCommandBuffer cmd_buffer,
  const rei_vk_buffer_t* src,
  rei_vk_image_t* dst,
  u32 mip_level,
  u32 width,
  u32 height) {

  VkBufferImageCopy copy_info = {
    .bufferOffset = 0,
    .bufferRowLength = 0,
    .bufferImageHeight = 0,
    .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .imageSubresource.mipLevel = mip_level,
    .imageSubresource.baseArrayLayer = 0,
    .imageSubresource.layerCount = 1,
    .imageOffset.x = 0,
    .imageOffset.y = 0,
    .imageOffset.z = 0,
    .imageExtent.width = width,
    .imageExtent.height = height,
    .imageExtent.depth = 1
  };

  vkCmdCopyBufferToImage (cmd_buffer, src->handle, dst->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_info);
}

void rei_vk_create_texture_cmd (
  const rei_vk_device_t* device,
  rei_vk_allocator_t* allocator,
  VkCommandBuffer cmd_buffer,
  rei_vk_buffer_t* staging_buffer,
  const rei_texture_t* src,
  rei_vk_image_t* out) {

  REI_ASSERT (src->component_count == 3 || src->component_count == 4);

  const u64 image_size = src->width * src->height * src->component_count;
  rei_vk_create_buffer (allocator, image_size, REI_VK_BUFFER_TYPE_STAGING, staging_buffer);

  rei_vk_map_buffer (allocator, staging_buffer);
  LZ4_decompress_safe (src->compressed_data, (char*) staging_buffer->mapped, (s32) src->compressed_size, (s32) image_size);
  rei_vk_unmap_buffer (allocator, staging_buffer);

  rei_vk_create_image (
    device,
    allocator,
    &(const rei_vk_image_ci_t) {
      .width = src->width,
      .height = src->height,
      .mip_levels = 1,
      .format = src->component_count == 3 ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8A8_SRGB,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
    },
    out
  );

  VkImageSubresourceRange subresource_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1
  };

  rei_vk_transition_image_cmd (
    cmd_buffer,
    &(const rei_vk_image_trans_info_t) {
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .subresource_range = &subresource_range,
    },
    out->handle
  );

  rei_vk_copy_buffer_to_image_cmd (cmd_buffer, staging_buffer, out, 0, src->width, src->height);

  rei_vk_transition_image_cmd (
    cmd_buffer,
    &(const rei_vk_image_trans_info_t) {
      .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      .subresource_range = &subresource_range,
    },
    out->handle
  );
}

void rei_vk_create_texture_raw (
  const rei_vk_device_t* device,
  rei_vk_allocator_t* allocator,
  const rei_vk_imm_ctxt_t* context,
  u32 width,
  u32 height,
  const u8* src,
  rei_vk_image_t* out) {

  const u64 image_size = (u64) (width * height * 4);

  rei_vk_buffer_t staging_buffer;
  rei_vk_create_buffer (allocator, image_size, REI_VK_BUFFER_TYPE_STAGING, &staging_buffer);

  rei_vk_map_buffer (allocator, &staging_buffer);
  memcpy (staging_buffer.mapped, src, image_size);
  rei_vk_unmap_buffer (allocator, &staging_buffer);

  rei_vk_create_image (
    device,
    allocator,
    &(const rei_vk_image_ci_t) {
      .width = width,
      .height = height,
      .mip_levels = 1,
      .format = VK_FORMAT_R8G8B8A8_SRGB,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
    },
    out
  );

  VkCommandBuffer cmd_buffer;
  rei_vk_start_imm_cmd (device, context, &cmd_buffer);

  VkImageSubresourceRange subresource_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1
  };

  rei_vk_transition_image_cmd (
    cmd_buffer,
    &(const rei_vk_image_trans_info_t) {
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .subresource_range = &subresource_range,
    },
    out->handle
  );

  rei_vk_copy_buffer_to_image_cmd (cmd_buffer, &staging_buffer, out, 0, width, height);

  rei_vk_transition_image_cmd (
    cmd_buffer,
    &(const rei_vk_image_trans_info_t) {
      .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      .subresource_range = &subresource_range,
    },
    out->handle
  );

  rei_vk_end_imm_cmd (device, context, cmd_buffer);
  rei_vk_destroy_buffer (allocator, &staging_buffer);
}

#if 0
void rei_vk_create_texture_mipmapped (
  const rei_vk_device_t* device,
  VmaAllocator allocator,
  const rei_vk_imm_ctxt_t* context,
  const rei_image_t* src,
  rei_vk_image_t* out) {

  // Calculate number of mip levels.
  const u32 mip_levels = (u32) (floorf (log2f ((f32) REI_MAX (src->width, src->height)))) + 1;

  {
    rei_vk_image_ci_t create_info = {
      .width = src->width,
      .height = src->height,
      .mip_levels = mip_levels,
      .format = VK_FORMAT_R8G8B8A8_SRGB,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
    };

    rei_vk_create_image (device, allocator, &create_info, out);
  }

  rei_vk_buffer_t staging;
  const u64 staging_size = (u64) (src->width * src->height * 4);

  rei_vk_create_buffer (
    allocator,
    staging_size,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    VMA_MEMORY_USAGE_CPU_ONLY,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    &staging
  );

  REI_VK_CHECK (vmaMapMemory (allocator, staging.memory, &staging.mapped));
  memcpy (staging.mapped, src->pixels, staging_size);
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

      .imageExtent.width = src->width,
      .imageExtent.height = src->height,
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
      .srcOffsets[1] = {.x = (s32) (src->width >> (i - 1)), .y = (s32) (src->height >> (i - 1)), .z = 1},

      .dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .dstSubresource.baseArrayLayer = 0,
      .dstSubresource.layerCount = 1,
      .dstSubresource.mipLevel = i,

      .dstOffsets[0] = {.x = 0, .y = 0, .z = 0},
      .dstOffsets[1] = {.x = (s32) (src->width >> i), .y = (s32) (src->height >> i), .z = 1},
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
#endif

void rei_vk_create_descriptor_layout (
  const rei_vk_device_t* device,
  u32 bind_count,
  const VkDescriptorSetLayoutBinding* bindings,
  VkDescriptorSetLayout* out) {

  VkDescriptorSetLayoutCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .bindingCount = bind_count,
    .pBindings = bindings,
  };

  REI_VK_CHECK (vkCreateDescriptorSetLayout (device->handle, &create_info, NULL, out));
}

void rei_vk_create_descriptor_pool (
  const rei_vk_device_t* device,
  u32 max_count,
  u32 size_count,
  const VkDescriptorPoolSize* sizes,
  VkDescriptorPool* out) {

  VkDescriptorPoolCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .maxSets = max_count,
    .poolSizeCount = size_count,
    .pPoolSizes = sizes
  };

  REI_VK_CHECK (vkCreateDescriptorPool (device->handle, &create_info, NULL, out));
}

void rei_vk_allocate_descriptors (
  const rei_vk_device_t* device,
  VkDescriptorPool pool,
  VkDescriptorSetLayout layout,
  u32 count,
  VkDescriptorSet* out) {

  // Allocate array of descriptor layouts and fill it with a given descriptor layout...
  // It's weird, but it needs to be done in order to be able to allocate all descriptors in a single call.
  VkDescriptorSetLayout* descriptor_layouts = alloca (sizeof *descriptor_layouts * count);
  for (u32 i = 0; i < count; ++i) descriptor_layouts[i] = layout;

  VkDescriptorSetAllocateInfo alloc_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .pNext = NULL,
    .descriptorPool = pool,
    .descriptorSetCount = count,
    .pSetLayouts = descriptor_layouts,
  };

  REI_VK_CHECK (vkAllocateDescriptorSets (device->handle, &alloc_info, out));
}

void rei_vk_write_image_descriptors (
  const rei_vk_device_t* device,
  VkSampler sampler,
  const VkImageView* src_views,
  u32 count,
  VkDescriptorSet* descriptors) {

  VkWriteDescriptorSet* writes = malloc (sizeof *writes * count);
  VkDescriptorImageInfo* image_infos = malloc (sizeof *image_infos * count);

  for (u32 i = 0; i < count; ++i) {
    VkDescriptorImageInfo* new_image_info = &image_infos[i];
    new_image_info->sampler = sampler;
    new_image_info->imageView = src_views[i];
    new_image_info->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet* new_write = &writes[i];
    new_write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    new_write->pNext = NULL;
    new_write->dstSet = descriptors[i];
    new_write->dstBinding = 0;
    new_write->dstArrayElement = 0;
    new_write->descriptorCount = 1;
    new_write->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    new_write->pImageInfo = new_image_info;
    new_write->pBufferInfo = NULL;
    new_write->pTexelBufferView = NULL;
  }

  vkUpdateDescriptorSets (device->handle, count, writes, 0, NULL);

  free (image_infos);
  free (writes);
}
