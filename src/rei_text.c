#include "rei_text.h"

static b8 _s_find_symbol (char c, const rei_font_t* font, rei_font_symbol_t* out) {
  for (u32 i = 0; i < font->symbol_count; ++i) {
    rei_font_symbol_t* current = &font->symbols[i];
    if ((char) current->id == c) {
      out = current;
      return REI_TRUE;
    }
  }

  return REI_FALSE;
}

void rei_text_create_ctxt (
  const char* font_path,
  const rei_vk_device_t* vk_device,
  VmaAllocator vk_allocator,
  const rei_vk_imm_ctxt_t* vk_imm_ctxt,
  rei_text_ctxt_t* out) {
}
