#ifndef REI_MODEL_H
#define REI_MODEL_H

#include "rei_vk.h"

#if 0
typedef struct rei_batch_t {
  u32 first_index;
  u32 index_count;
  u32 material_index;
} rei_batch_t;
#endif

typedef struct rei_model_t {
  rei_vk_buffer_t vertex_buffer;
  rei_vk_buffer_t index_buffer;
  u32 index_count;
  u32 __padding;
  rei_mat4_t model_matrix;
} rei_model_t;

void rei_create_model (
  const char* relative_path,
  const rei_vk_device_t* vk_device,
  VmaAllocator vk_allocator,
  const rei_vk_imm_ctxt_t* vk_imm_ctxt,
  rei_model_t* out
);

void rei_draw_model_cmd (
  const rei_model_t* model,
  VkCommandBuffer vk_cmd_buffer,
  VkPipelineLayout vk_pipeline_layout,
  const rei_mat4_t* view_projection
);

void rei_destroy_model (VmaAllocator vk_allocator, rei_model_t* model);

#endif /* REI_MODEL_H */
