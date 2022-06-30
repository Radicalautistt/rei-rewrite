#include <memory.h>

#include "rei_model.h"
#include "rei_debug.h"
#include "rei_math.inl"
#include "rei_asset_loaders.h"

#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

static void* _s_get_gltf_accessor_data (const rei_gltf_t* gltf, u32 index) {
  const rei_gltf_accessor_t* accessor = &gltf->accessors[index];
  const rei_gltf_buffer_view_t* buffer_view = &gltf->buffer_views[accessor->buffer_view_index];

  return &((u8*)gltf->buffer.data)[accessor->byte_offset + buffer_view->offset];
}

void rei_create_model (
  const char* relative_path,
  const rei_vk_device_t* vk_device,
  VmaAllocator vk_allocator,
  const rei_vk_imm_ctxt_t* vk_imm_ctxt,
  rei_model_t* out) {

  rei_gltf_t gltf;
  REI_CHECK (rei_gltf_load (relative_path, &gltf));

  { // Create vertex and index buffers.
    u32 vertex_count = 0;
    out->index_count = 0;

    // Count overall number of vertices and indices.
    for (u32 i = 0; i < gltf.mesh_count; ++i) {
      const rei_gltf_mesh_t* current_mesh = &gltf.meshes[i];

      for (u32 j = 0; j < current_mesh->primitive_count; ++j) {
        const rei_gltf_primitive_t* current_primitive = &current_mesh->primitives[j];

        vertex_count += gltf.accessors[current_primitive->position_index].count;
        out->index_count += gltf.accessors[current_primitive->indices_index].count;
      }
    }

    const u64 vertex_buffer_size = sizeof (rei_vertex_t) * vertex_count;
    // FIXME I don't get why do I have to use u32 as the index type when indices in the model are definitely u16...
    const u64 index_buffer_size = sizeof (u32) * out->index_count;

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

    for (u32 i = 0; i < gltf.mesh_count; ++i) {
      const rei_gltf_mesh_t* current_mesh = &gltf.meshes[i];

      for (u32 j = 0; j < current_mesh->primitive_count; ++j) {
        const rei_gltf_primitive_t* current_primitive = &current_mesh->primitives[j];

        const u32 vertex_start = vertex_offset;

        const f32* position = (const f32*) _s_get_gltf_accessor_data (&gltf, current_primitive->position_index);
        const f32* normal = (const f32*) _s_get_gltf_accessor_data (&gltf, current_primitive->normal_index);
        const f32* uv = (const f32*) _s_get_gltf_accessor_data (&gltf, current_primitive->uv_index);

        const u32 current_vertex_count = gltf.accessors[current_primitive->position_index].count;

        for (u32 k = 0; k < current_vertex_count; ++k) {
          rei_vertex_t* new_vertex = &vertices[vertex_offset++];

          memcpy (&new_vertex->x, &position[k * 3], vec3_size);
          memcpy (&new_vertex->nx, &normal[k * 3], vec3_size);
          memcpy (&new_vertex->u, &uv[k * 2], vec2_size);
        }

        const u32 current_index_count = gltf.accessors[current_primitive->indices_index].count;
        const u16* index_data = (const u16*) _s_get_gltf_accessor_data (&gltf, current_primitive->indices_index);

        for (u32 k = 0; k < current_index_count; ++k) {
          indices[index_offset++] = index_data[k] + vertex_start;
        }
      }
    }

    rei_vk_unmap_buffer (vk_allocator, &staging_buffer);

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
    const rei_vec3_t scale_vector = {.x = default_node->scale_vector[0], .y = default_node->scale_vector[1], .z = default_node->scale_vector[2]};

    rei_mat4_create_default (&out->model_matrix);
    rei_mat4_scale (&out->model_matrix, &scale_vector);
  }

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

  vkCmdDrawIndexed (vk_cmd_buffer, model->index_count, 1, 0, 0, 0);
}

void rei_destroy_model (VmaAllocator vk_allocator, rei_model_t* model) {
  rei_vk_destroy_buffer (vk_allocator, &model->index_buffer);
  rei_vk_destroy_buffer (vk_allocator, &model->vertex_buffer);
}
