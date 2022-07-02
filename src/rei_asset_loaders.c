#include <memory.h>

#include "rei_json.h"
#include "rei_debug.h"
#include "rei_asset_loaders.h"

#include <png.h>
#include <jpeglib.h>

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

static void _s_gltf_parse_nodes (rei_json_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->node_count = (u32) root_token->size;
  out->nodes = malloc (sizeof *out->nodes * out->node_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_node_t* new_node = &out->nodes[i];
    const jsmntok_t* current_node = state->current_token++;

    for (s32 j = 0; j < current_node->size; ++j) {
      if (rei_json_string_eq (state, "mesh", state->current_token)) {
        rei_json_parse_u32 (state, &new_node->mesh_index);
      } else if (rei_json_string_eq (state, "scale", state->current_token)) {
	rei_json_parse_floats (state, new_node->scale_vector);
      }
    }
  }
}

static void _s_gltf_parse_buffer_views (rei_json_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->buffer_view_count = (u32) root_token->size;
  out->buffer_views = malloc (sizeof *out->buffer_views * out->buffer_view_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_buffer_view_t* new_buffer_view = &out->buffer_views[i];
    const jsmntok_t* current_buffer_view = state->current_token++;

    for (s32 j = 0; j < current_buffer_view->size; ++j) {
      if (rei_json_string_eq (state, "buffer", state->current_token)) {
        rei_json_parse_u32 (state, &new_buffer_view->buffer_index);
      } else if (rei_json_string_eq (state, "byteLength", state->current_token)) {
        rei_json_parse_u64 (state, &new_buffer_view->size);
      } else if (rei_json_string_eq (state, "byteOffset", state->current_token)) {
        rei_json_parse_u64 (state, &new_buffer_view->offset);
      }
    }
  }
}

static void _s_gltf_parse_accessors (rei_json_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->accessor_count = (u32) root_token->size;
  out->accessors = malloc (sizeof *out->accessors * out->accessor_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_accessor_t* new_accessor = &out->accessors[i];
    const jsmntok_t* current_accessor = state->current_token++;

    for (s32 j = 0; j < current_accessor->size; ++j) {
      if (rei_json_string_eq (state, "bufferView", state->current_token)) {
        rei_json_parse_u32 (state, &new_accessor->buffer_view_index);
      } else if (rei_json_string_eq (state, "count", state->current_token)) {
        rei_json_parse_u32 (state, &new_accessor->count);
      } else if (rei_json_string_eq (state, "byteOffset", state->current_token)) {
        rei_json_parse_u64 (state, &new_accessor->byte_offset);
      } else if (rei_json_string_eq (state, "componentType", state->current_token)) {
        rei_json_parse_u32 (state, &new_accessor->component_type);
      } else if (rei_json_string_eq (state, "type", state->current_token)) {
        rei_string_view_t type;
	rei_json_parse_string (state, &type);

        if (!strncmp (type.src, "VEC2", type.size)) {
          new_accessor->type = REI_GLTF_TYPE_VEC2;
	} else if (!strncmp (type.src, "VEC3", type.size)) {
          new_accessor->type = REI_GLTF_TYPE_VEC3;
	} else if (!strncmp (type.src, "VEC4", type.size)) {
          new_accessor->type = REI_GLTF_TYPE_VEC4;
	} else if (!strncmp (type.src, "MAT2", type.size)) {
          new_accessor->type = REI_GLTF_TYPE_MAT2;
	} else if (!strncmp (type.src, "MAT3", type.size)) {
          new_accessor->type = REI_GLTF_TYPE_MAT3;
	} else if (!strncmp (type.src, "MAT4", type.size)) {
          new_accessor->type = REI_GLTF_TYPE_MAT4;
	} else if (!strncmp (type.src, "SCALAR", type.size)) {
          new_accessor->type = REI_GLTF_TYPE_SCALAR;
	}

      } else {
        rei_json_skip (state);
      }
    }
  }
}

static void _s_gltf_parse_samplers (rei_json_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->sampler_count = (u32) root_token->size;
  out->samplers = malloc (sizeof *out->samplers * out->sampler_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_sampler_t* new_sampler = &out->samplers[i];
    const jsmntok_t* current_sampler = state->current_token++;

    for (s32 j = 0; j < current_sampler->size; ++j) {
      if (rei_json_string_eq (state, "magFilter", state->current_token)) {
        rei_json_parse_u32 (state, &new_sampler->mag_filter);
      } else if (rei_json_string_eq (state, "minFilter", state->current_token)) {
        rei_json_parse_u32 (state, &new_sampler->mag_filter);
      } else if (rei_json_string_eq (state, "wrapS", state->current_token)) {
        rei_json_parse_u32 (state, &new_sampler->mag_filter);
      } else if (rei_json_string_eq (state, "wrapT", state->current_token)) {
        rei_json_parse_u32 (state, &new_sampler->mag_filter);
      }
    }
  }
}

static void _s_gltf_parse_images (rei_json_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->image_count = (u32) root_token->size;
  out->images = malloc (sizeof *out->images * out->image_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_image_t* new_image = &out->images[i];
    const jsmntok_t* current_image = state->current_token++;

    for (s32 j = 0; j < current_image->size; ++j) {
      if (rei_json_string_eq (state, "uri", state->current_token)) {
        rei_string_view_t uri;
	rei_json_parse_string (state, &uri);

	new_image->uri = malloc (uri.size + 1);
	strncpy (new_image->uri, uri.src, uri.size);
	new_image->uri[uri.size] = '\0';

      } else if (rei_json_string_eq (state, "mimeType", state->current_token)) {
        rei_string_view_t mime_type;
	rei_json_parse_string (state, &mime_type);

	if (!strncmp (mime_type.src, "image/jpeg", mime_type.size)) {
          new_image->mime_type = REI_GLTF_IMAGE_TYPE_JPEG;
	} else if (!strncmp (mime_type.src, "image/png", mime_type.size)) {
          new_image->mime_type = REI_GLTF_IMAGE_TYPE_PNG;
	}
      }
    }
  }
}

static void _s_gltf_parse_textures (rei_json_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->texture_count = (u32) root_token->size;
  out->textures = malloc (sizeof *out->textures * out->texture_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_texture_t* new_texture = &out->textures[i];
    const jsmntok_t* current_texture = state->current_token++;

    for (s32 j = 0; j < current_texture->size; ++j) {
      if (rei_json_string_eq (state, "sampler", state->current_token)) {
        rei_json_parse_u32 (state, &new_texture->sampler_index);
      } else if (rei_json_string_eq (state, "source", state->current_token)) {
        rei_json_parse_u32 (state, &new_texture->image_index);
      }
    }
  }
}

static void _s_gltf_parse_materials (rei_json_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->material_count = (u32) root_token->size;
  out->materials = malloc (sizeof *out->materials * out->material_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_material_t* new_material = &out->materials[i];
    const jsmntok_t* current_material = state->current_token++;

    for (s32 j = 0; j < current_material->size; ++j) {
      if (rei_json_string_eq (state, "pbrMetallicRoughness", state->current_token)) {
        ++state->current_token;
        const jsmntok_t* pbr = state->current_token++;

	for (s32 k = 0; k < pbr->size; ++k) {
          if (rei_json_string_eq (state, "baseColorTexture", state->current_token)) {
	    ++state->current_token;
            const jsmntok_t* albedo = state->current_token++;

	    for (s32 m = 0; m < albedo->size; ++m) {
	      if (rei_json_string_eq (state, "index", state->current_token)) {
                rei_json_parse_u32 (state, &new_material->albedo_index);
	      } else {
                rei_json_skip (state);
	      }
	    }
	  } else {
            rei_json_skip (state);
	  }
	}
      } else {
        rei_json_skip (state);
      }
    }
  }
}

static void _s_gltf_parse_meshes (rei_json_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->mesh_count = (u32) root_token->size;
  out->meshes = malloc (sizeof *out->meshes * out->mesh_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_mesh_t* new_mesh = &out->meshes[i];
    const jsmntok_t* current_mesh = state->current_token++;

    for (s32 j = 0; j < current_mesh->size; ++j) {
      if (rei_json_string_eq (state, "primitives", state->current_token)) {
	++state->current_token;
	const jsmntok_t* primitives = state->current_token++;
	REI_ASSERT (primitives->type == JSMN_ARRAY);

	new_mesh->primitive_count = (u64) primitives->size;
	new_mesh->primitives = malloc (sizeof *new_mesh->primitives * new_mesh->primitive_count);

	for (s32 k = 0; k < primitives->size; ++k) {
	  const jsmntok_t* primitive = state->current_token++;
	  rei_gltf_primitive_t* new_primitive = &new_mesh->primitives[k];

	  for (s32 l = 0; l < primitive->size; ++l) {
	    if (rei_json_string_eq (state, "indices", state->current_token)) {
              rei_json_parse_u32 (state, &new_primitive->indices_index);
	    } else if (rei_json_string_eq (state, "material", state->current_token)) {
              rei_json_parse_u32 (state, &new_primitive->material_index);
	    } else if (rei_json_string_eq (state, "attributes", state->current_token)) {
              ++state->current_token;
	      const jsmntok_t* padla = state->current_token++;

	      for (s32 m = 0; m < padla->size; ++m) {
		if (rei_json_string_eq (state, "POSITION", state->current_token)) {
                  rei_json_parse_u32 (state, &new_primitive->position_index);
		} else if (rei_json_string_eq (state, "NORMAL", state->current_token)) {
                  rei_json_parse_u32 (state, &new_primitive->normal_index);
		} else if (rei_json_string_eq (state, "TEXCOORD_0", state->current_token)) {
                  rei_json_parse_u32 (state, &new_primitive->uv_index);
		} else {
                  rei_json_skip (state);
		}
	      }
	    } else {
              rei_json_skip (state);
	    }
	  }
	}
      } else {
        rei_json_skip (state);
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

  rei_json_state_t json_state;
  REI_CHECK (rei_json_tokenize ((const char*) gltf.data, gltf.size, &json_state));

  const jsmntok_t* root_token = json_state.current_token++;
  REI_ASSERT (root_token->type == JSMN_OBJECT);

  for (s32 i = 0; i < root_token->size; ++i) {
    if (rei_json_string_eq (&json_state, "asset", json_state.current_token)) {
      rei_json_skip (&json_state);
    } else if (rei_json_string_eq (&json_state, "nodes", json_state.current_token)) {
      _s_gltf_parse_nodes (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "buffers", json_state.current_token)) {
      rei_json_skip (&json_state);
    } else if (rei_json_string_eq (&json_state, "bufferViews", json_state.current_token)) {
      _s_gltf_parse_buffer_views (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "accessors", json_state.current_token)) {
      _s_gltf_parse_accessors (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "samplers", json_state.current_token)) {
      _s_gltf_parse_samplers (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "images", json_state.current_token)) {
      _s_gltf_parse_images (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "textures", json_state.current_token)) {
      _s_gltf_parse_textures (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "materials", json_state.current_token)) {
      _s_gltf_parse_materials (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "meshes", json_state.current_token)) {
      _s_gltf_parse_meshes (&json_state, out);
    } else {
      rei_json_skip (&json_state);
    }
  }

  free (json_state.json_tokens);
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
