#ifndef REI_MODEL_H
#define REI_MODEL_H

#include "rei_vk.h"

typedef struct rei_model_t {
  struct {rei_vk_buffer_t vtx, idx;}* buffers;

  u32 texture_count;
  u32 batch_count;

  struct {
    u32* first_indices;
    u32* idx_counts;
    u32* material_indices;
  }* batches;

  rei_vk_image_t* textures;
  VkDescriptorPool descriptor_pool;
  VkDescriptorSet* descriptors;
  // TODO Create a global array of matrices which will be processed before rei_model_draw_cmd.
  rei_mat4_t* model_matrix;
} rei_model_t;

void rei_model_create (
  const char* relative_path,
  const rei_vk_device_t* vk_device,
  rei_vk_allocator_t* vk_allocator,
  const rei_vk_imm_ctxt_t* vk_imm_ctxt,
  VkSampler vk_sampler,
  VkDescriptorSetLayout vk_descriptor_layout,
  rei_model_t* out
);

void rei_model_draw_cmd (
  const rei_model_t* model,
  VkCommandBuffer vk_cmd_buffer,
  VkPipelineLayout vk_pipeline_layout,
  const rei_mat4_t* view_projection
);

void rei_model_destroy (const rei_vk_device_t* vk_device, rei_vk_allocator_t* vk_allocator, rei_model_t* model);

#endif /* REI_MODEL_H */
