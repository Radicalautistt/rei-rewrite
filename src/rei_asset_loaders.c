#include <stdlib.h>
#include <string.h>
#include <memory.h>

#include "rei_defines.h"
#include "rei_file.h"
#include "rei_debug.h"
#include "rei_asset_loaders.h"

#include <png.h>
#include <jsmn/jsmn.h>

struct _s_rei_gltf_state_t {
  s32 position;
  u32 __padding;
  const char* const json;
  const jsmntok_t* const tokens;
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
static void _s_rei_read_png (png_structp png_reader, png_bytep out, size_t count) {
  rei_file_t* png_file = (rei_file_t*) png_get_io_ptr (png_reader);
  memcpy (out, png_file->data, count);
  png_file->data += count;
}

void rei_load_png (const char* relative_path, rei_png_t* out) {
  REI_LOG_INFO ("Loading an image from " REI_ANSI_YELLOW "\"%s\"", relative_path);

  rei_file_t png_file;
  REI_CHECK (rei_map_file (relative_path, &png_file));

  // Make sure that provided image is a valid PNG.
  REI_ASSERT (!png_sig_cmp (png_file.data, 0, 8));
  png_file.data += 8;

  png_structp png_reader = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop png_info = png_create_info_struct (png_reader);

  png_set_read_fn (png_reader, &png_file, _s_rei_read_png);
  png_set_sig_bytes (png_reader, 8);

  png_read_png (png_reader, png_info, 0, NULL);
  rei_unmap_file (&png_file);

  png_get_IHDR (png_reader, png_info, &out->width, &out->height, NULL, NULL, NULL, NULL, NULL);

  u32 bytes_per_row = (u32) png_get_rowbytes (png_reader, png_info);
  out->pixels = malloc (bytes_per_row * out->height);

  const png_bytepp rows = png_get_rows (png_reader, png_info);

  for (u32 i = 0; i < out->height; ++i)
    memcpy (out->pixels + (bytes_per_row * (out->height - 1 - i)), rows[i], bytes_per_row);

  png_destroy_read_struct (&png_reader, &png_info, NULL);
}

static REI_FORCE_INLINE b8 _s_rei_is_digit (char symbol) {
  return symbol >= '0' && symbol <= '9';
}

static u32 _s_rei_parse_u32 (const char* src) {
  s32 result = 0;

  while (*src && _s_rei_is_digit (*src)) {
    result = result * 10 + (*src - '0');
    ++src;
  }

  return (u32) result;
}

static u64 _s_rei_parse_u64 (const char* src) {
  s64 result = 0;

  while (*src && _s_rei_is_digit (*src)) {
    result = result * 10 + (*src - '0');
    ++src;
  }

  return (u64) result;
}

static b8 _s_rei_gltf_string_eq (const struct _s_rei_gltf_state_t* state, const char* json_string, const jsmntok_t* json_token) {
  return !strncmp (state->json + json_token->start, json_string, strlen (json_string));
}

static void _s_rei_gltf_skip (struct _s_rei_gltf_state_t* state) {
  ++state->position;
  s32 end = state->position + 1;
  while (state->position < end) {
    const jsmntok_t* current = &state->tokens[state->position];

    switch (current->type) {
      case JSMN_OBJECT: end += current->size * 2; break;
      case JSMN_ARRAY: end += current->size; break;
      default: break;
    }

    ++state->position;
  }
}

static void _s_rei_gltf_parse_u32 (struct _s_rei_gltf_state_t* state, u32* out) {
  ++state->position;

  const jsmntok_t* json_token = &state->tokens[state->position++];
  REI_ASSERT (json_token->type == JSMN_PRIMITIVE);

  *out = _s_rei_parse_u32 (state->json + json_token->start);
}

static void _s_rei_gltf_parse_u64 (struct _s_rei_gltf_state_t* state, u64* out) {
  ++state->position;

  const jsmntok_t* json_token = &state->tokens[state->position++];
  REI_ASSERT (json_token->type == JSMN_PRIMITIVE);

  *out = _s_rei_parse_u64 (state->json + json_token->start);
}

static void _s_rei_gltf_parse_floats (struct _s_rei_gltf_state_t* state, f32* out) {
  ++state->position;

  const jsmntok_t* root_token = &state->tokens[state->position++];

  for (s32 i = 0; i < root_token->size; ++i) {
    const jsmntok_t* current_token = &state->tokens[state->position++];

    // TODO Write my own function for parsing floats.
    out[i] = (f32) atof (state->json + current_token->start);
  }
}

static void _s_rei_gltf_parse_string (struct _s_rei_gltf_state_t* state, char* out) {
  ++state->position;
  const jsmntok_t* json_token = &state->tokens[state->position++];
  REI_ASSERT (json_token->type == JSMN_STRING);

  const char* start = state->json + json_token->start;
  const char* end = start;
  while (*end != '\"') ++end;

  strncpy (out, start, end - start);
}

static void _s_rei_gltf_parse_nodes (struct _s_rei_gltf_state_t* state, rei_gltf_t* out) {
  ++state->position;

  const jsmntok_t* root_token = &state->tokens[state->position++];
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->node_count = (u32) root_token->size;
  out->nodes = malloc (sizeof *out->nodes * out->node_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_node_t* new_node = &out->nodes[i];
    const jsmntok_t* current_node = &state->tokens[state->position++];

    for (s32 j = 0; j < current_node->size; ++j) {
      const jsmntok_t* current_token = &state->tokens[state->position];

      if (_s_rei_gltf_string_eq (state, "mesh", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_node->mesh_index);
      } else if (_s_rei_gltf_string_eq (state, "scale", current_token)) {
	_s_rei_gltf_parse_floats (state, new_node->scale_vector);
      }
    }
  }
}

static void _s_rei_gltf_parse_buffer_views (struct _s_rei_gltf_state_t* state, rei_gltf_t* out) {
  ++state->position;

  const jsmntok_t* root_token = &state->tokens[state->position++];
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->buffer_view_count = (u32) root_token->size;
  out->buffer_views = malloc (sizeof *out->buffer_views * out->buffer_view_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_buffer_view_t* new_buffer_view = &out->buffer_views[i];
    const jsmntok_t* current_buffer_view = &state->tokens[state->position++];

    for (s32 j = 0; j < current_buffer_view->size; ++j) {
      const jsmntok_t* current_token = &state->tokens[state->position];

      if (_s_rei_gltf_string_eq (state, "buffer", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_buffer_view->buffer_index);
      } else if (_s_rei_gltf_string_eq (state, "byteLength", current_token)) {
        _s_rei_gltf_parse_u64 (state, &new_buffer_view->size);
      } else if (_s_rei_gltf_string_eq (state, "byteOffset", current_token)) {
        _s_rei_gltf_parse_u64 (state, &new_buffer_view->offset);
      }
    }
  }
}

static void _s_rei_gltf_parse_accessors (struct _s_rei_gltf_state_t* state, rei_gltf_t* out) {
  ++state->position;

  const jsmntok_t* root_token = &state->tokens[state->position++];
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->accessor_count = (u32) root_token->size;
  out->accessors = malloc (sizeof *out->accessors * out->accessor_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_accessor_t* new_accessor = &out->accessors[i];
    const jsmntok_t* current_accessor = &state->tokens[state->position++];

    for (s32 j = 0; j < current_accessor->size; ++j) {
      const jsmntok_t* current_token = &state->tokens[state->position];

      if (_s_rei_gltf_string_eq (state, "bufferview", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_accessor->buffer_view_index);
      } else if (_s_rei_gltf_string_eq (state, "count", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_accessor->count);
      } else if (_s_rei_gltf_string_eq (state, "byteOffset", current_token)) {
        _s_rei_gltf_parse_u64 (state, &new_accessor->byte_offset);
      } else if (_s_rei_gltf_string_eq (state, "componentType", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_accessor->component_type);
      } else if (_s_rei_gltf_string_eq (state, "type", current_token)) {
        char type[16] = {0};
	_s_rei_gltf_parse_string (state, type);

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
        _s_rei_gltf_skip (state);
      }
    }
  }
}

static void _s_rei_gltf_parse_samplers (struct _s_rei_gltf_state_t* state, rei_gltf_t* out) {
  ++state->position;

  const jsmntok_t* root_token = &state->tokens[state->position++];
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->sampler_count = (u32) root_token->size;
  out->samplers = malloc (sizeof *out->samplers * out->sampler_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_sampler_t* new_sampler = &out->samplers[i];
    const jsmntok_t* current_sampler = &state->tokens[state->position++];

    for (s32 j = 0; j < current_sampler->size; ++j) {
      const jsmntok_t* current_token = &state->tokens[state->position];
      if (_s_rei_gltf_string_eq (state, "magFilter", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_sampler->mag_filter);
      } else if (_s_rei_gltf_string_eq (state, "minFilter", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_sampler->mag_filter);
      } else if (_s_rei_gltf_string_eq (state, "wrapS", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_sampler->mag_filter);
      } else if (_s_rei_gltf_string_eq (state, "wrapT", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_sampler->mag_filter);
      }
    }
  }
}

static void _s_rei_gltf_parse_images (struct _s_rei_gltf_state_t* state, rei_gltf_t* out) {
  ++state->position;

  const jsmntok_t* root_token = &state->tokens[state->position++];
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->image_count = (u32) root_token->size;
  out->images = malloc (sizeof *out->images * out->image_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_image_t* new_image = &out->images[i];
    const jsmntok_t* current_image = &state->tokens[state->position++];

    for (s32 j = 0; j < current_image->size; ++j) {
      const jsmntok_t* current_token = &state->tokens[state->position];
      if (_s_rei_gltf_string_eq (state, "uri", current_token)) {
        // FIXME Malloc strings properly.
        char uri[128] = {0};
	_s_rei_gltf_parse_string (state, uri);

	u64 size = strlen (uri);
	new_image->uri = malloc (size + 1);
	strcpy (new_image->uri, uri);
      } else if (_s_rei_gltf_string_eq (state, "mimeType", current_token)) {
        char mime_type[16] = {0};
	_s_rei_gltf_parse_string (state, mime_type);

	if (!strcmp (mime_type, "image/jpeg")) {
          new_image->mime_type = REI_GLTF_IMAGE_TYPE_JPEG;
	} else if (!strcmp (mime_type, "image/png")) {
          new_image->mime_type = REI_GLTF_IMAGE_TYPE_PNG;
	}
      }
    }
  }
}

static void _s_rei_gltf_parse_textures (struct _s_rei_gltf_state_t* state, rei_gltf_t* out) {
  ++state->position;

  const jsmntok_t* root_token = &state->tokens[state->position++];
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->texture_count = (u32) root_token->size;
  out->textures = malloc (sizeof *out->textures * out->texture_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_texture_t* new_texture = &out->textures[i];
    const jsmntok_t* current_texture = &state->tokens[state->position++];

    for (s32 j = 0; j < current_texture->size; ++j) {
      const jsmntok_t* current_token = &state->tokens[state->position];
      if (_s_rei_gltf_string_eq (state, "sampler", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_texture->sampler_index);
      } else if (_s_rei_gltf_string_eq (state, "source", current_token)) {
        _s_rei_gltf_parse_u32 (state, &new_texture->image_index);
      }
    }
  }
}

static void _s_rei_gltf_parse_materials (struct _s_rei_gltf_state_t* state, rei_gltf_t* out) {
  ++state->position;

  const jsmntok_t* root_token = &state->tokens[state->position++];
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->material_count = (u32) root_token->size;
  out->materials = malloc (sizeof *out->materials * out->material_count);

#if 0
  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_material_t* new_material = &out->materials[i];
    const jsmntok_t* current_material = &state->tokens[state->position++];

    for (s32 j = 0; j < current_material->size; ++j) {
      const jsmntok_t* current_token = &state->tokens[state->position];

      if (_s_rei_gltf_string_eq (state, "pbrMetallicRoughness", current_token)) {
        ++state->position;

	const jsmntok_t* pbr = &state->tokens[state->position++];
	for (s32 k = 0; k < pbr->size; ++k) {
          const jsmntok_t* zhopa = &state->tokens[state->position];

          if (_s_rei_gltf_string_eq (state, "baseColorTexture", &state->tokens[state->position])) {
            ++state->position;
            for (s32 m = 0; m < zhopa->size; ++m) {
              if (_s_rei_gltf_string_eq (state, "index", &state->tokens[state->position])) {
                _s_rei_gltf_parse_u32 (state, &new_material->albedo_index);
	      } else {
                _s_rei_gltf_skip (state);
	      }
	    }
	  } else {
            _s_rei_gltf_skip (state);
	  }
	}

      } else {
        _s_rei_gltf_skip (state);
      }
    }

    REI_LOG_ERROR ("ALBEDO %u", new_material->albedo_index);
  }
#endif
}

static void _s_rei_gltf_parse_meshes (struct _s_rei_gltf_state_t* state, rei_gltf_t* out) {
  ++state->position;

  const jsmntok_t* root_token = &state->tokens[state->position++];
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->mesh_count = (u32) root_token->size;
  out->meshes = malloc (sizeof *out->meshes * out->mesh_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_mesh_t* new_mesh = &out->meshes[i];
    const jsmntok_t* current_mesh = &state->tokens[state->position++];

    for (s32 j = 0; j < current_mesh->size; ++j) {
      const jsmntok_t* current_token = &state->tokens[state->position];

      if (_s_rei_gltf_string_eq (state, "primitives", current_token)) {
	++state->position;

	const jsmntok_t* primitives = &state->tokens[state->position];
	REI_ASSERT (primitives->type == JSMN_ARRAY);

	new_mesh->primitive_count = (u64) primitives->size;
	new_mesh->primitives = malloc (sizeof *new_mesh->primitives * new_mesh->primitive_count);

	++state->position;

	for (s32 k = 0; k < primitives->size; ++k) {
	  const jsmntok_t* primitive = &state->tokens[state->position];
	  rei_gltf_primitive_t* new_primitive = &new_mesh->primitives[k];

	  ++state->position;
	  for (s32 l = 0; l < primitive->size; ++l) {
            const jsmntok_t* zalupa = &state->tokens[state->position];

	    if (_s_rei_gltf_string_eq (state, "indices", zalupa)) {
              _s_rei_gltf_parse_u32 (state, &new_primitive->index_count);
	    } else if (_s_rei_gltf_string_eq (state, "material", zalupa)) {
              _s_rei_gltf_parse_u32 (state, &new_primitive->material_index);
	    } else if (_s_rei_gltf_string_eq (state, "attributes", zalupa)) {
	      const jsmntok_t* padla = &state->tokens[++state->position];

	      ++state->position;

	      for (s32 m = 0; m < padla->size; ++m) {
                const jsmntok_t* zhopa = &state->tokens[state->position];
		if (_s_rei_gltf_string_eq (state, "POSITION", zhopa)) {
                  _s_rei_gltf_parse_u32 (state, &new_primitive->position_index);
		} else if (_s_rei_gltf_string_eq (state, "NORMAL", zhopa)) {
                  _s_rei_gltf_parse_u32 (state, &new_primitive->normal_index);
		} else if (_s_rei_gltf_string_eq (state, "TEXCOORD_0", zhopa)) {
                  _s_rei_gltf_parse_u32 (state, &new_primitive->uv_index);
		} else {
                  _s_rei_gltf_skip (state);
		}
	      }
	    } else {
              _s_rei_gltf_skip (state);
	    }
	  }
	}
      } else {
        _s_rei_gltf_skip (state);
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
  struct _s_rei_gltf_state_t gltf_state = {.position = 1, .json = json, .tokens = json_tokens};

  jsmn_init (&json_parser);
  jsmn_parse (&json_parser, json, gltf.size, json_tokens, (u32) token_count);

  const jsmntok_t* root_token = &gltf_state.tokens[0];
  REI_ASSERT (root_token->type == JSMN_OBJECT);

  for (s32 i = 0; i < root_token->size; ++i) {
    const jsmntok_t* current_token = &json_tokens[gltf_state.position];

    if (_s_rei_gltf_string_eq (&gltf_state, "asset", current_token)) {
      _s_rei_gltf_skip (&gltf_state);
    } else if (_s_rei_gltf_string_eq (&gltf_state, "nodes", current_token)) {
      _s_rei_gltf_parse_nodes (&gltf_state, out);
    } else if (_s_rei_gltf_string_eq (&gltf_state, "buffers", current_token)) {
      _s_rei_gltf_skip (&gltf_state);
    } else if (_s_rei_gltf_string_eq (&gltf_state, "bufferViews", current_token)) {
      _s_rei_gltf_parse_buffer_views (&gltf_state, out);
    } else if (_s_rei_gltf_string_eq (&gltf_state, "accessors", current_token)) {
      _s_rei_gltf_parse_accessors (&gltf_state, out);
    } else if (_s_rei_gltf_string_eq (&gltf_state, "samplers", current_token)) {
      _s_rei_gltf_parse_samplers (&gltf_state, out);
    } else if (_s_rei_gltf_string_eq (&gltf_state, "images", current_token)) {
      _s_rei_gltf_parse_images (&gltf_state, out);
    } else if (_s_rei_gltf_string_eq (&gltf_state, "textures", current_token)) {
      _s_rei_gltf_parse_textures (&gltf_state, out);
    } else if (_s_rei_gltf_string_eq (&gltf_state, "materials", current_token)) {
      //_s_rei_gltf_parse_materials (&gltf_state, out);
      _s_rei_gltf_skip (&gltf_state);
    } else if (_s_rei_gltf_string_eq (&gltf_state, "meshes", current_token)) {
      _s_rei_gltf_parse_meshes (&gltf_state, out);
    } else {
      _s_rei_gltf_skip (&gltf_state);
    }
  }

  free (json_tokens);
  rei_unmap_file (&gltf);

  return REI_RESULT_SUCCESS;
}

void rei_gltf_destroy (rei_gltf_t* gltf) {
  rei_unmap_file (&gltf->buffer);

  for (u32 i = 0; gltf->mesh_count; ++i) {
    for (u64 j = 0; gltf->meshes[i].primitive_count; ++j) {
      free (&gltf->meshes[i].primitives[j]);
    }
  }

  free (gltf->meshes);
  //free (gltf->materials);
  free (gltf->textures);

  for (u32 i = 0; i < gltf->image_count; ++i) free (gltf->images[i].uri);
  free (gltf->images);

  free (gltf->samplers);
  free (gltf->accessors);
  free (gltf->buffer_views);
  free (gltf->nodes);
}
