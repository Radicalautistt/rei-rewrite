#ifndef REI_ASSET_LOADERS_H
#define REI_ASSET_LOADERS_H

#include "rei_file.h"

typedef enum rei_gltf_accessor_type_e {
  REI_GLTF_TYPE_VEC2,
  REI_GLTF_TYPE_VEC3,
  REI_GLTF_TYPE_VEC4,
  REI_GLTF_TYPE_MAT2,
  REI_GLTF_TYPE_MAT3,
  REI_GLTF_TYPE_MAT4,
  REI_GLTF_TYPE_SCALAR
} rei_gltf_accessor_type_e;

typedef enum rei_gltf_component_type_e {
  REI_GLTF_COMPONENT_TYPE_S8 = 5120,
  REI_GLTF_COMPONENT_TYPE_U8 = 5121,
  REI_GLTF_COMPONENT_TYPE_S16 = 5122,
  REI_GLTF_COMPONENT_TYPE_U16 = 5123,
  REI_GLTF_COMPONENT_TYPE_U32 = 5125,
  REI_GLTF_COMPONENT_TYPE_F32 = 5126
} rei_gltf_component_type_e;

typedef enum rei_gltf_sampler_filter_e {
  REI_GLTF_SAMPLER_FILTER_NEAR = 9728,
  REI_GLTF_SAMPLER_FILTER_LINEAR = 9729
} rei_gltf_sampler_filter_e;

typedef enum rei_gltf_sampler_wrap_mode_e {
  REI_GLTF_SAMPLER_WRAP_MODE_REPEAT = 10497,
  REI_GLTF_SAMPLER_WRAP_MODE_MIRRORED_REPEAT = 33648,
  REI_GLTF_SAMPLER_WRAP_MODE_CLAMP_TO_EDGE = 33071
} rei_gltf_sampler_wrap_mode_e;

typedef enum rei_gltf_image_type_e {
  REI_GLTF_IMAGE_TYPE_PNG,
  REI_GLTF_IMAGE_TYPE_JPEG
} rei_gltf_image_type_e;

typedef enum rei_gltf_interpolation_e {
  REI_GLTF_INTERPOLATION_LINEAR
} rei_gltf_interpolation_e;

typedef enum rei_gltf_channel_path_e {
  REI_GLTF_CHANNEL_PATH_SCALE,
  REI_GLTF_CHANNEL_PATH_WEIGHTS,
  REI_GLTF_CHANNEL_PATH_ROTATION,
  REI_GLTF_CHANNEL_PATH_TRANSLATION
} rei_gltf_channel_path_e;

typedef struct rei_wav_t {
  u16 bits_per_sample;
  u16 channel_count;
  s32 sample_rate;
  s32 size;
  u32 __padding_yay;
  u8* data;
} rei_wav_t;

typedef struct rei_font_symbol_t {
  u8 id;
  u8 x;
  u8 y;
  u8 width;
  u8 height;
  s8 xoffset;
  s8 yoffset;
  u8 xadvance;
} rei_font_symbol_t;

typedef struct rei_font_t {
  u32 symbol_count;
  u32 size;
  char* atlas_path;
  rei_font_symbol_t* symbols;
} rei_font_t;

typedef struct rei_gltf_node_t {
  u32 mesh_index;
  f32 scale_vector[3];
} rei_gltf_node_t;

typedef struct rei_gltf_buffer_view_t {
  u32 buffer_index;
  u32 __padding;
  u64 size;
  u64 offset;
} rei_gltf_buffer_view_t;

typedef struct rei_gltf_accessor_t {
  u32 count;
  u32 buffer_view_index;
  u64 byte_offset;
  rei_gltf_accessor_type_e type;
  rei_gltf_component_type_e component_type;
} rei_gltf_accessor_t;

typedef struct rei_gltf_sampler_t {
  rei_gltf_sampler_wrap_mode_e wrap_s;
  rei_gltf_sampler_wrap_mode_e wrap_t;
  rei_gltf_sampler_filter_e min_filter;
  rei_gltf_sampler_filter_e mag_filter;
} rei_gltf_sampler_t;

typedef struct rei_gltf_image_t {
  char* uri;
  u32 __padding;
  rei_gltf_image_type_e mime_type;
} rei_gltf_image_t;

typedef struct rei_gltf_texture_t {
  u32 image_index;
  u32 sampler_index;
} rei_gltf_texture_t;

typedef struct rei_gltf_material_t {
  u32 albedo_index;
} rei_gltf_material_t;

typedef struct rei_gltf_primitive_t {
  u32 indices_index;
  u32 material_index;

  u32 position_index;
  u32 normal_index;
  u32 uv_index;
} rei_gltf_primitive_t;

typedef struct rei_gltf_mesh_t {
  u32 primitive_count;
  u32 __padding;
  rei_gltf_primitive_t* primitives;
} rei_gltf_mesh_t;

typedef struct rei_gltf_animation_sampler_t {
  u32 input;
  rei_gltf_interpolation_e interpolation;
  u32 output;
} rei_gltf_animation_sampler_t;

typedef struct rei_gltf_animation_channel_t {
  u32 sampler_index;
  u32 target_node_index;
  rei_gltf_channel_path_e path;
} rei_gltf_animation_channel_t;

typedef struct rei_gltf_animation_t {
  u32 channel_count;
  u32 sampler_count;
  rei_gltf_animation_channel_t* channels;
  rei_gltf_animation_sampler_t* samplers;
} rei_gltf_animation_t;

typedef struct rei_gltf_t {
  u32 node_count;
  u32 mesh_count;
  u32 image_count;
  u32 sampler_count;
  u32 texture_count;
  u32 material_count;
  u32 accessor_count;
  u32 buffer_view_count;
  u32 animation_count;
  u32 buffer_count;

  rei_file_t* buffers;

  rei_gltf_node_t* nodes;
  rei_gltf_mesh_t* meshes;
  rei_gltf_image_t* images;
  rei_gltf_sampler_t* samplers;
  rei_gltf_texture_t* textures;
  rei_gltf_material_t* materials;
  rei_gltf_accessor_t* accessors;
  rei_gltf_animation_t* animations;
  rei_gltf_buffer_view_t* buffer_views;
} rei_gltf_t;

void rei_load_wav (const char* relative_path, rei_wav_t* out);
void rei_load_png (const char* relative_path, rei_image_t* out);
void rei_load_jpeg (const char* relative_path, rei_image_t* out);

void rei_load_font (const char* const relative_path, rei_font_t* out);
void rei_destroy_font (rei_font_t* font);

rei_result_e rei_gltf_load (const char* relative_path, rei_gltf_t* out);
void rei_gltf_destroy (rei_gltf_t* gltf);

#endif /* REI_ASSET_LOADERS_H */
