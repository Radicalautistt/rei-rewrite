#include <stdlib.h>
#include <string.h>
#include <memory.h>

#include "rei_file.h"
#include "rei_debug.h"
#include "rei_defines.h"
#include "rei_asset_loaders.h"

#include <png.h>
#include <jpeglib.h>
#include <jsmn/jsmn.h>

struct _s_gltf_state_t {
  const char* json;
  const jsmntok_t* current_token;
};

void rei_load_wav (const char* relativePath, rei_wav_t * out) {
  REI_LOG_INFO ("Loading a sound from " REI_ANSI_YELLOW "\"%s\"", relativePath);

  rei_file_t file;
  REI_CHECK (rei_map_file (relativePath, &file));

  u8* data = file.data;
  // Skip all the stuff that I don't care about at the moment.
  data += 22;

  out->channel_count = *((u16*) data);
  data += 2;
  out->sample_rate = *((s32*) data);
  data += 10;
  out->bits_per_sample = *((u16*) data);
  // FIXME This offset can be either 6 or 8 depending on some bullshit,
  // find a better way to handle it (read the spec and stop being a lazy piece of ass).
  data += 8;

  out->size = *((s32*) data);
  data += 4;

  out->data = malloc ((u64) out->size);
  memcpy (out->data, data, (u64) out->size);

  rei_unmap_file (&file);
}

// Custom png read function.
static void _s_read_png (png_structp png_reader, png_bytep out, size_t count) {
  rei_file_t* png_file = (rei_file_t*) png_get_io_ptr (png_reader);
  memcpy (out, png_file->data, count);
  png_file->data += count;
}

void rei_load_png (const char* relative_path, rei_image_t* out) {
  REI_LOG_INFO ("Loading an image from " REI_ANSI_YELLOW "\"%s\"", relative_path);

  rei_file_t png_file;
  REI_CHECK (rei_map_file (relative_path, &png_file));

  // Make sure that provided image is a valid PNG.
  REI_ASSERT (!png_sig_cmp (png_file.data, 0, 8));
  png_file.data += 8;

  png_structp png_reader = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop png_info = png_create_info_struct (png_reader);

  png_set_read_fn (png_reader, &png_file, _s_read_png);
  png_set_sig_bytes (png_reader, 8);

  png_read_png (png_reader, png_info, 0, NULL);
  rei_unmap_file (&png_file);

  png_get_IHDR (png_reader, png_info, &out->width, &out->height, NULL, NULL, NULL, NULL, NULL);

  u32 bytes_per_row = (u32) png_get_rowbytes (png_reader, png_info);
  // FIXME hardcoded.
  out->component_count = 4;
  out->pixels = malloc (bytes_per_row * out->height);

  const png_bytepp rows = png_get_rows (png_reader, png_info);

  for (u32 i = 0; i < out->height; ++i)
    memcpy (out->pixels + (bytes_per_row * (out->height - 1 - i)), rows[i], bytes_per_row);

  png_destroy_read_struct (&png_reader, &png_info, NULL);
}

void rei_load_jpeg (const char* relative_path, rei_image_t* out) {
  REI_LOG_INFO ("Loading an image from " REI_ANSI_YELLOW "\"%s\"", relative_path);

  struct jpeg_decompress_struct decomp_info;
  struct jpeg_error_mgr error_mgr;

  decomp_info.err = jpeg_std_error (&error_mgr);

  jpeg_create_decompress (&decomp_info);

  // TODO find a way to use rei_map_file instead of stdio.
  FILE* image_file = fopen (relative_path, "rb");
  if (!image_file) {
    fprintf (stderr, "Failed to open \"%s\". No such file.\n", relative_path);
    exit (EXIT_FAILURE);
  }

  jpeg_stdio_src (&decomp_info, image_file);
  jpeg_read_header (&decomp_info, 1);

  jpeg_start_decompress (&decomp_info);

  out->width = decomp_info.image_width;
  out->height = decomp_info.image_height;
  out->component_count = (u32) decomp_info.output_components;
  out->pixels = malloc (out->width * out->height * out->component_count);

  const u32 row_stride = out->width * out->component_count;

  while (decomp_info.output_scanline < out->height) {
    u8* current_row[] = {&out->pixels[row_stride * decomp_info.output_scanline]};
    jpeg_read_scanlines (&decomp_info, current_row, 1);
  }

  jpeg_finish_decompress (&decomp_info);
  jpeg_destroy_decompress (&decomp_info);

  fclose (image_file);
}

static REI_FORCE_INLINE b8 _s_is_digit (char symbol) {
  return symbol >= '0' && symbol <= '9';
}

static u32 _s_parse_u32 (const char* src) {
  s32 result = 0;

  while (*src && _s_is_digit (*src)) {
    result = result * 10 + (*src - '0');
    ++src;
  }

  return (u32) result;
}

static u64 _s_parse_u64 (const char* src) {
  s64 result = 0;

  while (*src && _s_is_digit (*src)) {
    result = result * 10 + (*src - '0');
    ++src;
  }

  return (u64) result;
}

static b8 _s_gltf_string_eq (const struct _s_gltf_state_t* state, const char* json_string, const jsmntok_t* json_token) {
  return !strncmp (state->json + json_token->start, json_string, strlen (json_string));
}

static void _s_gltf_skip (struct _s_gltf_state_t* state) {
  ++state->current_token;
  const jsmntok_t* end = state->current_token + 1;

  while (state->current_token < end) {
    switch (state->current_token->type) {
      case JSMN_OBJECT: end += state->current_token->size * 2; break;
      case JSMN_ARRAY: end += state->current_token->size; break;
      default: break;
    }

    ++state->current_token;
  }
}

static void _s_gltf_parse_u32 (struct _s_gltf_state_t* state, u32* out) {
  ++state->current_token;
  REI_ASSERT (state->current_token->type == JSMN_PRIMITIVE);

  *out = _s_parse_u32 (state->json + state->current_token->start);
  ++state->current_token;
}

static void _s_gltf_parse_u64 (struct _s_gltf_state_t* state, u64* out) {
  ++state->current_token;
  REI_ASSERT (state->current_token->type == JSMN_PRIMITIVE);

  *out = _s_parse_u64 (state->json + state->current_token->start);
  ++state->current_token;
}

static void _s_gltf_parse_floats (struct _s_gltf_state_t* state, f32* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  for (s32 i = 0; i < root_token->size; ++i) {
    // TODO Write my own function for parsing floats.
    out[i] = (f32) atof (state->json + state->current_token->start);
    ++state->current_token;
  }
}

static void _s_gltf_parse_string (struct _s_gltf_state_t* state, char* out) {
  ++state->current_token;
  REI_ASSERT (state->current_token->type == JSMN_STRING);

  const char* start = state->json + state->current_token->start;
  const char* end = start;
  while (*end != '\"') ++end;

  strncpy (out, start, end - start);
  ++state->current_token;
}

static void _s_gltf_parse_nodes (struct _s_gltf_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->node_count = (u32) root_token->size;
  out->nodes = malloc (sizeof *out->nodes * out->node_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_node_t* new_node = &out->nodes[i];
    const jsmntok_t* current_node = state->current_token++;

    for (s32 j = 0; j < current_node->size; ++j) {
      if (_s_gltf_string_eq (state, "mesh", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_node->mesh_index);
      } else if (_s_gltf_string_eq (state, "scale", state->current_token)) {
	_s_gltf_parse_floats (state, new_node->scale_vector);
      }
    }
  }
}

static void _s_gltf_parse_buffer_views (struct _s_gltf_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->buffer_view_count = (u32) root_token->size;
  out->buffer_views = malloc (sizeof *out->buffer_views * out->buffer_view_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_buffer_view_t* new_buffer_view = &out->buffer_views[i];
    const jsmntok_t* current_buffer_view = state->current_token++;

    for (s32 j = 0; j < current_buffer_view->size; ++j) {
      if (_s_gltf_string_eq (state, "buffer", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_buffer_view->buffer_index);
      } else if (_s_gltf_string_eq (state, "byteLength", state->current_token)) {
        _s_gltf_parse_u64 (state, &new_buffer_view->size);
      } else if (_s_gltf_string_eq (state, "byteOffset", state->current_token)) {
        _s_gltf_parse_u64 (state, &new_buffer_view->offset);
      }
    }
  }
}

static void _s_gltf_parse_accessors (struct _s_gltf_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->accessor_count = (u32) root_token->size;
  out->accessors = malloc (sizeof *out->accessors * out->accessor_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_accessor_t* new_accessor = &out->accessors[i];
    const jsmntok_t* current_accessor = state->current_token++;

    for (s32 j = 0; j < current_accessor->size; ++j) {
      if (_s_gltf_string_eq (state, "bufferView", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_accessor->buffer_view_index);
      } else if (_s_gltf_string_eq (state, "count", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_accessor->count);
      } else if (_s_gltf_string_eq (state, "byteOffset", state->current_token)) {
        _s_gltf_parse_u64 (state, &new_accessor->byte_offset);
      } else if (_s_gltf_string_eq (state, "componentType", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_accessor->component_type);
      } else if (_s_gltf_string_eq (state, "type", state->current_token)) {
        char type[16] = {0};
	_s_gltf_parse_string (state, type);

        if (!strcmp (type, "VEC2")) {
          new_accessor->type = REI_GLTF_TYPE_VEC2;
	} else if (!strcmp (type, "VEC3")) {
          new_accessor->type = REI_GLTF_TYPE_VEC3;
	} else if (!strcmp (type, "VEC4")) {
          new_accessor->type = REI_GLTF_TYPE_VEC4;
	} else if (!strcmp (type, "MAT2")) {
          new_accessor->type = REI_GLTF_TYPE_MAT2;
	} else if (!strcmp (type, "MAT3")) {
          new_accessor->type = REI_GLTF_TYPE_MAT3;
	} else if (!strcmp (type, "MAT4")) {
          new_accessor->type = REI_GLTF_TYPE_MAT4;
	} else if (!strcmp (type, "SCALAR")) {
          new_accessor->type = REI_GLTF_TYPE_SCALAR;
	}

      } else {
        _s_gltf_skip (state);
      }
    }
  }
}

static void _s_gltf_parse_samplers (struct _s_gltf_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->sampler_count = (u32) root_token->size;
  out->samplers = malloc (sizeof *out->samplers * out->sampler_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_sampler_t* new_sampler = &out->samplers[i];
    const jsmntok_t* current_sampler = state->current_token++;

    for (s32 j = 0; j < current_sampler->size; ++j) {
      if (_s_gltf_string_eq (state, "magFilter", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_sampler->mag_filter);
      } else if (_s_gltf_string_eq (state, "minFilter", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_sampler->mag_filter);
      } else if (_s_gltf_string_eq (state, "wrapS", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_sampler->mag_filter);
      } else if (_s_gltf_string_eq (state, "wrapT", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_sampler->mag_filter);
      }
    }
  }
}

static void _s_gltf_parse_images (struct _s_gltf_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->image_count = (u32) root_token->size;
  out->images = malloc (sizeof *out->images * out->image_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_image_t* new_image = &out->images[i];
    const jsmntok_t* current_image = state->current_token++;

    for (s32 j = 0; j < current_image->size; ++j) {
      if (_s_gltf_string_eq (state, "uri", state->current_token)) {
        // FIXME Malloc strings properly.
        char uri[128] = {0};
	_s_gltf_parse_string (state, uri);

	u64 size = strlen (uri);
	new_image->uri = malloc (size + 1);
	strcpy (new_image->uri, uri);
      } else if (_s_gltf_string_eq (state, "mimeType", state->current_token)) {
        char mime_type[16] = {0};
	_s_gltf_parse_string (state, mime_type);

	if (!strcmp (mime_type, "image/jpeg")) {
          new_image->mime_type = REI_GLTF_IMAGE_TYPE_JPEG;
	} else if (!strcmp (mime_type, "image/png")) {
          new_image->mime_type = REI_GLTF_IMAGE_TYPE_PNG;
	}
      }
    }
  }
}

static void _s_gltf_parse_textures (struct _s_gltf_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->texture_count = (u32) root_token->size;
  out->textures = malloc (sizeof *out->textures * out->texture_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_texture_t* new_texture = &out->textures[i];
    const jsmntok_t* current_texture = state->current_token++;

    for (s32 j = 0; j < current_texture->size; ++j) {
      if (_s_gltf_string_eq (state, "sampler", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_texture->sampler_index);
      } else if (_s_gltf_string_eq (state, "source", state->current_token)) {
        _s_gltf_parse_u32 (state, &new_texture->image_index);
      }
    }
  }
}

static void _s_gltf_parse_materials (struct _s_gltf_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->material_count = (u32) root_token->size;
  out->materials = malloc (sizeof *out->materials * out->material_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_material_t* new_material = &out->materials[i];
    const jsmntok_t* current_material = state->current_token++;

    for (s32 j = 0; j < current_material->size; ++j) {
      if (_s_gltf_string_eq (state, "pbrMetallicRoughness", state->current_token)) {
        ++state->current_token;
        const jsmntok_t* pbr = state->current_token++;

	for (s32 k = 0; k < pbr->size; ++k) {
          if (_s_gltf_string_eq (state, "baseColorTexture", state->current_token)) {
	    ++state->current_token;
            const jsmntok_t* albedo = state->current_token++;

	    for (s32 m = 0; m < albedo->size; ++m) {
	      if (_s_gltf_string_eq (state, "index", state->current_token)) {
                _s_gltf_parse_u32 (state, &new_material->albedo_index);
	      } else {
                _s_gltf_skip (state);
	      }
	    }
	  } else {
            _s_gltf_skip (state);
	  }
	}
      } else {
        _s_gltf_skip (state);
      }
    }
  }
}

static void _s_gltf_parse_meshes (struct _s_gltf_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->mesh_count = (u32) root_token->size;
  out->meshes = malloc (sizeof *out->meshes * out->mesh_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_mesh_t* new_mesh = &out->meshes[i];
    const jsmntok_t* current_mesh = state->current_token++;

    for (s32 j = 0; j < current_mesh->size; ++j) {
      if (_s_gltf_string_eq (state, "primitives", state->current_token)) {
	++state->current_token;
	const jsmntok_t* primitives = state->current_token++;
	REI_ASSERT (primitives->type == JSMN_ARRAY);

	new_mesh->primitive_count = (u64) primitives->size;
	new_mesh->primitives = malloc (sizeof *new_mesh->primitives * new_mesh->primitive_count);

	for (s32 k = 0; k < primitives->size; ++k) {
	  const jsmntok_t* primitive = state->current_token++;
	  rei_gltf_primitive_t* new_primitive = &new_mesh->primitives[k];

	  for (s32 l = 0; l < primitive->size; ++l) {
	    if (_s_gltf_string_eq (state, "indices", state->current_token)) {
              _s_gltf_parse_u32 (state, &new_primitive->indices_index);
	    } else if (_s_gltf_string_eq (state, "material", state->current_token)) {
              _s_gltf_parse_u32 (state, &new_primitive->material_index);
	    } else if (_s_gltf_string_eq (state, "attributes", state->current_token)) {
              ++state->current_token;
	      const jsmntok_t* padla = state->current_token++;

	      for (s32 m = 0; m < padla->size; ++m) {
		if (_s_gltf_string_eq (state, "POSITION", state->current_token)) {
                  _s_gltf_parse_u32 (state, &new_primitive->position_index);
		} else if (_s_gltf_string_eq (state, "NORMAL", state->current_token)) {
                  _s_gltf_parse_u32 (state, &new_primitive->normal_index);
		} else if (_s_gltf_string_eq (state, "TEXCOORD_0", state->current_token)) {
                  _s_gltf_parse_u32 (state, &new_primitive->uv_index);
		} else {
                  _s_gltf_skip (state);
		}
	      }
	    } else {
              _s_gltf_skip (state);
	    }
	  }
	}
      } else {
        _s_gltf_skip (state);
      }
    }
  }
}

rei_result_e rei_gltf_load (const char* relative_path, rei_gltf_t* out) {
  REI_LOG_INFO ("Loading GLTF model from " REI_ANSI_YELLOW "\"%s\"", relative_path);

  { // Load geometry buffer.
    char buffer_path[128] = {0};
    strcpy (buffer_path, relative_path);

    char* extension = strrchr (buffer_path, '.');

    if (extension) {
      memcpy (extension + 1, "bin", 4);
      REI_CHECK (rei_map_file (buffer_path, &out->buffer));
    } else {
      return REI_RESULT_INVALID_FILE_PATH;
    }
  }

  rei_file_t gltf;
  REI_CHECK (rei_map_file (relative_path, &gltf));

  const char* json = (const char*) gltf.data;

  jsmn_parser json_parser = {0, 0, 0};

  s32 token_count = jsmn_parse (&json_parser, json, gltf.size, NULL, 0);
  if (token_count <= 0) return REI_RESULT_INVALID_JSON;

  jsmntok_t* json_tokens = malloc (sizeof *json_tokens * (u32) token_count);
  struct _s_gltf_state_t gltf_state = {.json = json, .current_token = &json_tokens[1]};

  jsmn_init (&json_parser);
  jsmn_parse (&json_parser, json, gltf.size, json_tokens, (u32) token_count);

  const jsmntok_t* root_token = &json_tokens[0];
  REI_ASSERT (root_token->type == JSMN_OBJECT);

  for (s32 i = 0; i < root_token->size; ++i) {
    if (_s_gltf_string_eq (&gltf_state, "asset", gltf_state.current_token)) {
      _s_gltf_skip (&gltf_state);
    } else if (_s_gltf_string_eq (&gltf_state, "nodes", gltf_state.current_token)) {
      _s_gltf_parse_nodes (&gltf_state, out);
    } else if (_s_gltf_string_eq (&gltf_state, "buffers", gltf_state.current_token)) {
      _s_gltf_skip (&gltf_state);
    } else if (_s_gltf_string_eq (&gltf_state, "bufferViews", gltf_state.current_token)) {
      _s_gltf_parse_buffer_views (&gltf_state, out);
    } else if (_s_gltf_string_eq (&gltf_state, "accessors", gltf_state.current_token)) {
      _s_gltf_parse_accessors (&gltf_state, out);
    } else if (_s_gltf_string_eq (&gltf_state, "samplers", gltf_state.current_token)) {
      _s_gltf_parse_samplers (&gltf_state, out);
    } else if (_s_gltf_string_eq (&gltf_state, "images", gltf_state.current_token)) {
      _s_gltf_parse_images (&gltf_state, out);
    } else if (_s_gltf_string_eq (&gltf_state, "textures", gltf_state.current_token)) {
      _s_gltf_parse_textures (&gltf_state, out);
    } else if (_s_gltf_string_eq (&gltf_state, "materials", gltf_state.current_token)) {
      _s_gltf_parse_materials (&gltf_state, out);
    } else if (_s_gltf_string_eq (&gltf_state, "meshes", gltf_state.current_token)) {
      _s_gltf_parse_meshes (&gltf_state, out);
    } else {
      _s_gltf_skip (&gltf_state);
    }
  }

  free (json_tokens);
  rei_unmap_file (&gltf);

  return REI_RESULT_SUCCESS;
}

void rei_gltf_destroy (rei_gltf_t* gltf) {
  rei_unmap_file (&gltf->buffer);

  for (u32 i = 0; i < gltf->mesh_count; ++i) free (gltf->meshes[i].primitives);
  free (gltf->meshes);

  free (gltf->materials);
  free (gltf->textures);

  for (u32 i = 0; i < gltf->image_count; ++i) free (gltf->images[i].uri);
  free (gltf->images);

  free (gltf->samplers);
  free (gltf->accessors);
  free (gltf->buffer_views);
  free (gltf->nodes);
}
