#include <memory.h>
#include <alloca.h>

#include "rei_asset.h"
#include "rei_model.h"
#include "rei_debug.h"
#include "rei_math.inl"
#include "rei_asset_loaders.h"

#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

// Quick sort (in-place) gltf primitives by material index to later form batches of them.
static void _s_sort_gltf_primitives (rei_gltf_primitive_t* primitives, u32 low, u32 high) {
  if (low < high) {
    u32 left = low - 1;
    u32 right = high + 1;

    // NOTE The cast of ((low + high) / 2) to s32 is made because conversion to f32
    // is faster with signed integers. Also, casting signed->unsigned and vice versa is free.
    // Reference: Agner Fog's Optimization Manual 1, page 30 and 40.
    const u32 middle = (u32) floorf ((f32) ((s32) ((low + high) / 2u)));
    const u32 pivot = primitives[middle].material_index;

    for (;;) {
      do ++left; while (primitives[left].material_index < pivot);
      do --right; while (primitives[right].material_index > pivot);

      if (left >= right) break;

      rei_gltf_primitive_t temp = primitives[left];
      primitives[left] = primitives[right];
      primitives[right] = temp;
    }

    _s_sort_gltf_primitives (primitives, low, right);
    _s_sort_gltf_primitives (primitives, right + 1, high);
  }
}

static void* _s_get_gltf_accessor_data (const rei_gltf_t* const gltf, u32 index) {
  const rei_gltf_accessor_t* accessor = &gltf->accessors[index];
  const rei_gltf_buffer_view_t* buffer_view = &gltf->buffer_views[accessor->buffer_view_index];

  const rei_file_t* buffer = &gltf->buffers[buffer_view->buffer_index];

  return buffer->data + accessor->byte_offset + buffer_view->offset;
}

static void _s_load_textures (
  const char* const model_path,
  const rei_gltf_t* const gltf,
  const rei_vk_device_t* vk_device,
  VmaAllocator vk_allocator,
  const rei_vk_imm_ctxt_t* vk_imm_ctxt,
  rei_model_t* out) {

  out->texture_count = gltf->texture_count;
  out->textures = malloc (sizeof *out->textures * out->texture_count);

  char full_path[128] = {0};
  strcpy (full_path, model_path);
  char* filename = strrchr (full_path, '/');

  rei_vk_buffer_t* staging_buffers = malloc (sizeof *staging_buffers * out->texture_count);

  VkCommandBuffer vk_cmd_buffer;
  rei_vk_start_imm_cmd (vk_device, vk_imm_ctxt, &vk_cmd_buffer);

  for (u32 i = 0; i < gltf->texture_count; ++i) {
    const rei_gltf_texture_t* current_texture = &gltf->textures[i];
    const rei_gltf_image_t* current_image = &gltf->images[current_texture->image_index];

    strcpy (filename + 1, current_image->uri);

    rei_texture_t new_texture;

    char* ext = strrchr (full_path, '.');
    if (ext++) {
      strcpy (ext, "rtex");
      rei_texture_load (full_path, &new_texture);
    }

    rei_vk_create_texture_cmd (vk_device, vk_allocator, vk_cmd_buffer, &staging_buffers[i], &new_texture, &out->textures[i]);
    free (new_texture.compressed_data);
  }

  rei_vk_end_imm_cmd (vk_device, vk_imm_ctxt, vk_cmd_buffer);

  for (u32 i = 0; i < gltf->texture_count; ++i) rei_vk_destroy_buffer (vk_allocator, &staging_buffers[i]);
  free (staging_buffers);
}

void rei_create_model (
  const char* relative_path,
  const rei_vk_device_t* vk_device,
  VmaAllocator vk_allocator,
  const rei_vk_imm_ctxt_t* vk_imm_ctxt,
  VkSampler vk_sampler,
  VkDescriptorSetLayout vk_descriptor_layout,
  rei_model_t* out) {

  rei_gltf_t gltf;
  REI_CHECK (rei_gltf_load (relative_path, &gltf));

  { // Create vertex and index buffers.

    // Merge all primitives into a single array.
    u64 primitive_count = 0;
    for (u32 i = 0; i < gltf.mesh_count; ++i) primitive_count += gltf.meshes[i].primitive_count;

    u64 primitive_offset = 0;
    rei_gltf_primitive_t* sorted_primitives = malloc (sizeof *sorted_primitives * primitive_count);

    for (u32 i = 0; i < gltf.mesh_count; ++i) {
      const rei_gltf_mesh_t* current_mesh = &gltf.meshes[i];

      const u64 current_size = sizeof (rei_gltf_primitive_t) * current_mesh->primitive_count;
      memcpy (sorted_primitives + primitive_offset, current_mesh->primitives, current_size);

      primitive_offset += current_size;
    }

    _s_sort_gltf_primitives (sorted_primitives, 0, (u32) primitive_count - 1);

    u32 vertex_count = 0;
    u32 index_count = 0;

    // Count overall number of vertices and indices.
    for (u64 i = 0; i < primitive_count; ++i) {
      const rei_gltf_primitive_t* current = &sorted_primitives[i];

      vertex_count += gltf.accessors[current->position_index].count;
      index_count += gltf.accessors[current->indices_index].count;
    }

    const u64 vertex_buffer_size = sizeof (rei_vertex_t) * vertex_count;
    // FIXME I don't get why do I have to use u32 as the index type when indices in the model are definitely u16...
    const u64 index_buffer_size = sizeof (u32) * index_count;

    rei_vk_buffer_t staging_buffer;
    rei_vk_create_buffer (
      vk_allocator,
      vertex_buffer_size + index_buffer_size,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VMA_MEMORY_USAGE_CPU_ONLY,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      &staging_buffer
    );

    rei_vk_map_buffer (vk_allocator, &staging_buffer);

    rei_vertex_t* vertices = (rei_vertex_t*) staging_buffer.mapped;
    u32* indices = (u32*) (staging_buffer.mapped + vertex_buffer_size);

    u32 vertex_offset = 0;
    u32 index_offset = 0;

    const u64 vec2_size = sizeof (f32) * 2;
    const u64 vec3_size = sizeof (f32) * 3;

    out->batch_count = gltf.material_count;
    out->batches = malloc (sizeof *out->batches * out->batch_count);

    u32 batch_offset = 0;
    rei_batch_t current_batch = {0};

    for (u64 i = 0; i < primitive_count; ++i) {
      const rei_gltf_primitive_t* current_primitive = &sorted_primitives[i];

      const u32 vertex_start = vertex_offset;

      const f32* position = (const f32*) _s_get_gltf_accessor_data (&gltf, current_primitive->position_index);
      const f32* normal = (const f32*) _s_get_gltf_accessor_data (&gltf, current_primitive->normal_index);
      const f32* uv = (const f32*) _s_get_gltf_accessor_data (&gltf, current_primitive->uv_index);

      const u32 current_vertex_count = gltf.accessors[current_primitive->position_index].count;

      for (u32 j = 0; j < current_vertex_count; ++j) {
        rei_vertex_t* new_vertex = &vertices[vertex_offset++];

        memcpy (&new_vertex->x, &position[j * 3], vec3_size);
        memcpy (&new_vertex->nx, &normal[j * 3], vec3_size);
        memcpy (&new_vertex->u, &uv[j * 2], vec2_size);
      }

      const u32 current_index_count = gltf.accessors[current_primitive->indices_index].count;
      const u16* index_data = (const u16*) _s_get_gltf_accessor_data (&gltf, current_primitive->indices_index);

      if (current_batch.material_index == current_primitive->material_index) {
        current_batch.index_count += current_index_count;
      } else {
	memcpy (&out->batches[batch_offset++], &current_batch, sizeof (rei_batch_t));

	current_batch.first_index = index_offset;
	current_batch.index_count = current_index_count;
	current_batch.material_index = current_primitive->material_index;
      }

      for (u32 k = 0; k < current_index_count; ++k) {
        indices[index_offset++] = index_data[k] + vertex_start;
      }
    }

    memcpy (&out->batches[batch_offset], &current_batch, sizeof (rei_batch_t));

    rei_vk_unmap_buffer (vk_allocator, &staging_buffer);
    free (sorted_primitives);

    rei_vk_create_buffer (
      vk_allocator,
      vertex_buffer_size,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      &out->vertex_buffer
    );

    rei_vk_create_buffer (
      vk_allocator,
      index_buffer_size,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      &out->index_buffer
    );

    VkCommandBuffer cmd_buffer;
    rei_vk_start_imm_cmd (vk_device, vk_imm_ctxt, &cmd_buffer);

    rei_vk_copy_buffer_cmd (cmd_buffer, vertex_buffer_size, 0, &staging_buffer, &out->vertex_buffer);
    rei_vk_copy_buffer_cmd (cmd_buffer, index_buffer_size, vertex_buffer_size, &staging_buffer, &out->index_buffer);

    rei_vk_end_imm_cmd (vk_device, vk_imm_ctxt, cmd_buffer);
    rei_vk_destroy_buffer (vk_allocator, &staging_buffer);
  }

  { // Create model matrix.
    const rei_gltf_node_t* default_node = &gltf.nodes[0];

    rei_vec3_u scale_vector;
    scale_vector.x = default_node->scale_vector[0];
    scale_vector.y = default_node->scale_vector[1];
    scale_vector.z = default_node->scale_vector[2];
    rei_mat4_create_default (&out->model_matrix);
    rei_mat4_scale (&out->model_matrix, &scale_vector);
  }

  _s_load_textures (relative_path, &gltf, vk_device, vk_allocator, vk_imm_ctxt, out);

  // Create descriptor pool big enough to hold all the materials of a model.
  rei_vk_create_descriptor_pool (
    vk_device,
    gltf.material_count,
    1,
    &(const VkDescriptorPoolSize) {
      .descriptorCount = gltf.material_count,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    },
    &out->descriptor_pool
  );

  out->descriptors = malloc (sizeof *out->descriptors * gltf.material_count);
  rei_vk_allocate_descriptors (vk_device, out->descriptor_pool, vk_descriptor_layout, gltf.material_count, out->descriptors);

  VkImageView* texture_views = alloca (sizeof *texture_views * gltf.material_count);
  for (u32 i = 0; i < gltf.material_count; ++i) texture_views[i] = out->textures[gltf.materials[i].albedo_index].view;

  rei_vk_write_image_descriptors (vk_device, vk_sampler, texture_views, gltf.material_count, out->descriptors);

  rei_gltf_destroy (&gltf);
}

void rei_draw_model_cmd (
  const rei_model_t* model,
  VkCommandBuffer vk_cmd_buffer,
  VkPipelineLayout vk_pipeline_layout,
  const rei_mat4_t* view_projection) {

  vkCmdBindVertexBuffers (vk_cmd_buffer, 0, 1, &model->vertex_buffer.handle, (const u64[]) {0});
  vkCmdBindIndexBuffer (vk_cmd_buffer, model->index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

  rei_mat4_t mvp;
  rei_mat4_mul (view_projection, &model->model_matrix, &mvp);
  vkCmdPushConstants (vk_cmd_buffer, vk_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof (rei_mat4_t), &mvp);

  for (u32 i = 0; i < model->batch_count; ++i) {
    const rei_batch_t* current_batch = &model->batches[i];

    REI_VK_BIND_DESCRIPTORS (vk_cmd_buffer, vk_pipeline_layout, 1, &model->descriptors[current_batch->material_index]);
    vkCmdDrawIndexed (vk_cmd_buffer, current_batch->index_count, 1, current_batch->first_index, 0, 0);
  }
}

void rei_destroy_model (const rei_vk_device_t* vk_device, VmaAllocator vk_allocator, rei_model_t* model) {
  free (model->batches);

  vkDestroyDescriptorPool (vk_device->handle, model->descriptor_pool, NULL);
  free (model->descriptors);

  for (u32 i = 0; i < model->texture_count; ++i) rei_vk_destroy_image (vk_device, vk_allocator, &model->textures[i]);
  free (model->textures);

  rei_vk_destroy_buffer (vk_allocator, &model->index_buffer);
  rei_vk_destroy_buffer (vk_allocator, &model->vertex_buffer);
}
