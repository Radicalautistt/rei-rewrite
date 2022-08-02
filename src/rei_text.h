#ifndef REI_TEXT_H
#define REI_TEXT_H

#include "rei_vk.h"
#include "rei_asset_loaders.h"

typedef struct rei_text_ctxt_t {
  VkPipeline pipeline;
  VkPipelineLayout pipeline_layout;

  rei_vk_image_t font_texture;
  VkDescriptorSet font_descriptor;
  struct {rei_vk_buffer_t vtx, idx;}* buffers;
} rei_text_ctxt_t;

void rei_text_create_ctxt (
  const char* font_path,
  const rei_vk_device_t* vk_device,
  VmaAllocator vk_allocator,
  const rei_vk_imm_ctxt_t* vk_imm_ctxt,
  rei_text_ctxt_t* out
);

void rei_text_destroy_ctxt (const rei_vk_device_t* vk_device, VmaAllocator vk_allocator, rei_text_ctxt_t* ctxt);

void rei_text_add (rei_text_ctxt_t* text_ctxt, const char* const text, u64 size, const rei_vec2_t* position, const rei_vec2_t* extent);

void rei_text_draw_cmd (
  VkCommandBuffer cmd_buffer,
  VmaAllocator vk_allocator,
  rei_text_ctxt_t* text_ctxt
);

#endif /* REI_TEXT_H */
