#ifndef REI_ASSET_H
#define REI_ASSET_H

#include "rei_types.h"

typedef struct rei_texture_t {
  u32 width;
  u32 height;
  u32 component_count;
  u32 compressed_size;
  char* compressed_data;
} rei_texture_t;

// Load and compress image file (png, jpeg) into a rei texture.
rei_result_e rei_texture_compress (const char* const relative_path);

// Compress all textures in a directory. relative_path is assumed to have a trailing slash.
rei_result_e rei_compress_texture_dir (const char* const relative_path);

// Load compressed texture.
rei_result_e rei_texture_load (const char* const relative_path, rei_texture_t* out);

#endif /* REI_ASSET_H */
