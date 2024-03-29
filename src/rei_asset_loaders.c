#include <memory.h>

#include "rei_parse.h"
#include "rei_debug.h"
#include "rei_asset_loaders.h"

#include <png.h>
#include <yxml/yxml.h>
#include <jpeglib.h>

void rei_load_wav (const char* relative_path, rei_wav_t * out) {
  REI_LOG_INFO ("Loading a sound from " REI_ANSI_YELLOW "\"%s\"", relative_path);

  rei_file_t file;
  REI_CHECK (rei_read_file (relative_path, &file));

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

  rei_free_file (&file);
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
  REI_CHECK (rei_read_file (relative_path, &png_file));

  // Make sure that provided image is a valid PNG.
  REI_ASSERT (!png_sig_cmp (png_file.data, 0, 8));
  png_file.data += 8;

  png_structp png_reader = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop png_info = png_create_info_struct (png_reader);

  png_set_read_fn (png_reader, &png_file, _s_read_png);
  png_set_sig_bytes (png_reader, 8);

  png_read_png (png_reader, png_info, 0, NULL);
  rei_free_file (&png_file);

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

  // TODO find a way to use rei_read_file instead of stdio.
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

void rei_load_font (const char* const relative_path, rei_font_t* out) {
  rei_file_t xml_file;
  REI_CHECK (rei_read_file (relative_path, &xml_file));

  const char* xml_data = (const char*) xml_file.data;

  #define _XML_BUFFER_SIZE 4096u

  yxml_t* xml_state = malloc (sizeof *xml_state + _XML_BUFFER_SIZE);
  yxml_init (xml_state, xml_state + 1, _XML_BUFFER_SIZE);

  #undef _XML_BUFFER_SIZE

  out->symbol_count = 0;

  u32 symb_offset = 0;
  rei_font_symbol_t* current_symb;

  u8 attr_value_offset = 0;
  char attr_value[32] = {0};

  for (; *xml_data; ++xml_data) {
    yxml_ret_t result = yxml_parse (xml_state, *xml_data);

    if (result < 0) {
      REI_LOG_STR_ERROR ("Zalupa kita");
      exit (EXIT_FAILURE);
    }

    if (!strcmp (xml_state->elem, "info")) {
      switch (result) {
        case YXML_ATTRSTART: break;
        case YXML_ATTRVAL: attr_value[attr_value_offset++] = *xml_state->data; break;
        case YXML_ATTREND:
          if (!strcmp (xml_state->attr, "size")) rei_parse_u32 (attr_value, &out->size);
	          attr_value_offset = 0;
	          memset (attr_value, 0, 32);
	          break;

        default: break;
      }
    }

    if (!strcmp (xml_state->elem, "page")) {
      switch (result) {
        case YXML_ATTRSTART: break;
        case YXML_ATTRVAL: attr_value[attr_value_offset++] = *xml_state->data; break;
        case YXML_ATTREND:
          if (!strcmp (xml_state->attr, "file")) {
	          out->atlas_path = malloc (strlen (relative_path) + strlen (attr_value) + 1);
	          const char* slash1 = strrchr (relative_path, '/');
	          strncpy (out->atlas_path, relative_path, (slash1 + 1) - relative_path);

	          char* slash2 = strrchr (out->atlas_path, '/');
	          strcpy (slash2 + 1, attr_value);
          }

	        attr_value_offset = 0;
	        memset (attr_value, 0, 32);
	        break;

        default: break;
      }
    }

    if (!strcmp (xml_state->elem, "chars")) {
      switch (result) {
        case YXML_ATTRSTART:
          break;

        case YXML_ATTRVAL:
          attr_value[attr_value_offset++] = *xml_state->data;
          break;

        case YXML_ATTREND:
	        if (!strcmp (xml_state->attr, "count")) {
	          rei_parse_u32 (attr_value, &out->symbol_count);
	          out->symbols = malloc (sizeof *out->symbols * out->symbol_count);
	        }

	        attr_value_offset = 0;
	        memset (attr_value, 0, 32);
          break;

        default: break;
      }
    } else if (!strcmp (xml_state->elem, "char")) {
      switch (result) {
        case YXML_ELEMSTART:
          current_symb = &out->symbols[symb_offset++];
          break;

	      case YXML_ATTRSTART:
	        break;

        case YXML_ATTRVAL:
          attr_value[attr_value_offset++] = *xml_state->data;
          break;

        case YXML_ATTREND:
	  if (!strcmp (xml_state->attr, "id")) {
            u32 result;
            rei_parse_u32 (attr_value, &result);
	    current_symb->id = (u8) result;
	  } else if (!strcmp (xml_state->attr, "x")) {
            u32 result;
            rei_parse_u32 (attr_value, &result);
            current_symb->x = (u8) result;
	  } else if (!strcmp (xml_state->attr, "y")) {
            u32 result;
            rei_parse_u32 (attr_value, &result);
            current_symb->y = (u8) result;
	  } else if (!strcmp (xml_state->attr, "width")) {
            u32 result;
            rei_parse_u32 (attr_value, &result);
            current_symb->width = (u8) result;
	  } else if (!strcmp (xml_state->attr, "height")) {
            u32 result;
            rei_parse_u32 (attr_value, &result);
            current_symb->height = (u8) result;
	  } else if (!strcmp (xml_state->attr, "xoffset")) {
	    current_symb->xoffset = (s8) atoi (attr_value);
	  } else if (!strcmp (xml_state->attr, "yoffset")) {
	    current_symb->yoffset = (s8) atoi (attr_value);
	  } else if (!strcmp (xml_state->attr, "xadvance")) {
            u32 result;
            rei_parse_u32 (attr_value, &result);
            current_symb->xadvance = (u8) result;
	  }

	  attr_value_offset = 0;
	  memset (attr_value, 0, 32);

	  break;

        case YXML_ELEMEND: break;
        default: break;
      }
    }
  }

  free (xml_state);

  rei_free_file (&xml_file);
}

void rei_destroy_font (rei_font_t* font) {
  free (font->atlas_path);
  free (font->symbols);
}

static void _s_gltf_parse_nodes (rei_json_state_t* state, rei_gltf_t* out) {
  // TODO Enable -Wnoincompatible-pointer-types globally.
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->nodes, &out->node_count, &out->nodes);
  REI_IGNORE_WARN_STOP

  register u32 count = out->node_count;
  rei_gltf_node_t* data = out->nodes;

  for (register u32 i = 0; i < count; ++i) {
    rei_gltf_node_t* new_node = &data[i];
    const jsmntok_t* current_node = state->current_token++;

    for (s32 j = 0; j < current_node->size; ++j) {
      if (rei_json_string_eq (state, "mesh", 4)) {
        rei_json_parse_u32 (state, &new_node->mesh_index);
      } else if (rei_json_string_eq (state, "scale", 5)) {
	rei_json_parse_floats (state, new_node->scale_vector);
      }
    }
  }
}

static void _s_gltf_parse_buffers (const char* const gltf_path, rei_json_state_t* state, rei_gltf_t* out) {
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->buffers, &out->buffer_count, &out->buffers);
  REI_IGNORE_WARN_STOP

  register u32 count = out->buffer_count;
  rei_file_t* data = out->buffers;

  for (register u32 i = 0; i < count; ++i) {
    rei_file_t* new_buffer = &data[i];
    const jsmntok_t* current_buffer = state->current_token++;

    for (s32 j = 0; j < current_buffer->size; ++j) {
      if (rei_json_string_eq (state, "byteLength", 10)) {
        rei_json_skip (state);
      } else if (rei_json_string_eq (state, "uri", 3)) {
        rei_string_view_t uri;
	      rei_json_parse_string (state, &uri);

	      char buffer_path[256] = {0};
	      char* slash_pos = strrchr (gltf_path, '/');

	      if (slash_pos++) {
	        strncpy (buffer_path, gltf_path, (u64) (slash_pos - gltf_path));
                strncpy (buffer_path + strlen (buffer_path), uri.src, uri.size);
	      }

        REI_CHECK (rei_read_file (buffer_path, new_buffer));
      }
    }
  }
}

static void _s_gltf_parse_buffer_views (rei_json_state_t* state, rei_gltf_t* out) {
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->buffer_views, &out->buffer_view_count, &out->buffer_views);
  REI_IGNORE_WARN_STOP

  register u32 count = out->buffer_view_count;
  rei_gltf_buffer_view_t* data = out->buffer_views;

  for (u32 i = 0; i < count; ++i) {
    rei_gltf_buffer_view_t* new_buffer_view = &data[i];
    const jsmntok_t* current_buffer_view = state->current_token++;

    for (s32 j = 0; j < current_buffer_view->size; ++j) {
      if (rei_json_string_eq (state, "buffer", 6)) {
        rei_json_parse_u32 (state, &new_buffer_view->buffer_index);
      } else if (rei_json_string_eq (state, "byteLength", 10)) {
        rei_json_parse_u64 (state, &new_buffer_view->size);
      } else if (rei_json_string_eq (state, "byteOffset", 10)) {
        rei_json_parse_u64 (state, &new_buffer_view->offset);
      }
    }
  }
}

static void _s_gltf_parse_accessors (rei_json_state_t* state, rei_gltf_t* out) {
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->accessors, &out->accessor_count, &out->accessors);
  REI_IGNORE_WARN_STOP

  register u32 count = out->accessor_count;
  rei_gltf_accessor_t* data = out->accessors;

  for (register u32 i = 0; i < count; ++i) {
    rei_gltf_accessor_t* new_accessor = &data[i];
    const jsmntok_t* current_accessor = state->current_token++;

    for (s32 j = 0; j < current_accessor->size; ++j) {
      if (rei_json_string_eq (state, "bufferView", 10)) {
        rei_json_parse_u32 (state, &new_accessor->buffer_view_index);
      } else if (rei_json_string_eq (state, "count", 5)) {
        rei_json_parse_u32 (state, &new_accessor->count);
      } else if (rei_json_string_eq (state, "byteOffset", 10)) {
        rei_json_parse_u64 (state, &new_accessor->byte_offset);
      } else if (rei_json_string_eq (state, "componentType", 13)) {
        rei_json_parse_u32 (state, &new_accessor->component_type);
      } else if (rei_json_string_eq (state, "type", 4)) {
        ++state->current_token;

        if (rei_json_string_eq (state, "VEC2", 4)) {
          new_accessor->type = REI_GLTF_TYPE_VEC2;
	} else if (rei_json_string_eq (state, "VEC3", 4)) {
          new_accessor->type = REI_GLTF_TYPE_VEC3;
	} else if (rei_json_string_eq (state, "VEC4", 4)) {
          new_accessor->type = REI_GLTF_TYPE_VEC4;
	} else if (rei_json_string_eq (state, "MAT2", 4)) {
          new_accessor->type = REI_GLTF_TYPE_MAT2;
	} else if (rei_json_string_eq (state, "MAT3", 4)) {
          new_accessor->type = REI_GLTF_TYPE_MAT3;
	} else if (rei_json_string_eq (state, "MAT4", 4)) {
          new_accessor->type = REI_GLTF_TYPE_MAT4;
	} else if (rei_json_string_eq (state, "SCALAR", 6)) {
          new_accessor->type = REI_GLTF_TYPE_SCALAR;
	}

	++state->current_token;
      } else {
        rei_json_skip (state);
      }
    }
  }
}

static void _s_gltf_parse_samplers (rei_json_state_t* state, rei_gltf_t* out) {
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->samplers, &out->sampler_count, &out->samplers);
  REI_IGNORE_WARN_STOP

  register u32 count = out->sampler_count;
  rei_gltf_sampler_t* data = out->samplers;

  for (register u32 i = 0; i < count; ++i) {
    rei_gltf_sampler_t* new_sampler = &data[i];
    const jsmntok_t* current_sampler = state->current_token++;

    for (s32 j = 0; j < current_sampler->size; ++j) {
      if (rei_json_string_eq (state, "magFilter", 9)) {
        rei_json_parse_u32 (state, &new_sampler->mag_filter);
      } else if (rei_json_string_eq (state, "minFilter", 9)) {
        rei_json_parse_u32 (state, &new_sampler->mag_filter);
      } else if (rei_json_string_eq (state, "wrapS", 5)) {
        rei_json_parse_u32 (state, &new_sampler->mag_filter);
      } else if (rei_json_string_eq (state, "wrapT", 5)) {
        rei_json_parse_u32 (state, &new_sampler->mag_filter);
      }
    }
  }
}

static void _s_gltf_parse_images (rei_json_state_t* state, rei_gltf_t* out) {
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->images, &out->image_count, &out->images);
  REI_IGNORE_WARN_STOP

  register u32 count = out->image_count;
  rei_gltf_image_t* data = out->images;

  for (register u32 i = 0; i < count; ++i) {
    rei_gltf_image_t* new_image = &data[i];
    const jsmntok_t* current_image = state->current_token++;

    for (s32 j = 0; j < current_image->size; ++j) {
      if (rei_json_string_eq (state, "uri", 3)) {
        rei_string_view_t uri;
	rei_json_parse_string (state, &uri);

	new_image->uri = malloc (uri.size + 1);
	strncpy (new_image->uri, uri.src, uri.size);
	new_image->uri[uri.size] = '\0';

      } else if (rei_json_string_eq (state, "mimeType", 8)) {
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
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->textures, &out->texture_count, &out->textures);
  REI_IGNORE_WARN_STOP

  register u32 count = out->texture_count;
  rei_gltf_texture_t* data = out->textures;

  for (register u32 i = 0; i < count; ++i) {
    rei_gltf_texture_t* new_texture = &data[i];
    const jsmntok_t* current_texture = state->current_token++;

    for (s32 j = 0; j < current_texture->size; ++j) {
      if (rei_json_string_eq (state, "sampler", 7)) {
        rei_json_parse_u32 (state, &new_texture->sampler_index);
      } else if (rei_json_string_eq (state, "source", 6)) {
        rei_json_parse_u32 (state, &new_texture->image_index);
      }
    }
  }
}

static void _s_gltf_parse_materials (rei_json_state_t* state, rei_gltf_t* out) {
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->materials, &out->material_count, &out->materials);
  REI_IGNORE_WARN_STOP

  register u32 count = out->material_count;
  rei_gltf_material_t* data = out->materials;

  for (register u32 i = 0; i < count; ++i) {
    rei_gltf_material_t* new_material = &data[i];
    const jsmntok_t* current_material = state->current_token++;

    for (s32 j = 0; j < current_material->size; ++j) {
      if (rei_json_string_eq (state, "pbrMetallicRoughness", 20)) {
        ++state->current_token;
        const jsmntok_t* pbr = state->current_token++;

	for (s32 k = 0; k < pbr->size; ++k) {
          if (rei_json_string_eq (state, "baseColorTexture", 16)) {
	    ++state->current_token;
            const jsmntok_t* albedo = state->current_token++;

	    for (s32 m = 0; m < albedo->size; ++m) {
	      if (rei_json_string_eq (state, "index", 5)) {
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

static void _s_gltf_parse_primitives (rei_json_state_t* state, rei_gltf_mesh_t* out) {
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->primitives, &out->primitive_count, &out->primitives);
  REI_IGNORE_WARN_STOP

  register u32 count = out->primitive_count;
  rei_gltf_primitive_t* data = out->primitives;

  for (register u32 i = 0; i < count; ++i) {
    const jsmntok_t* primitive = state->current_token++;
    rei_gltf_primitive_t* new_primitive = &data[i];

    for (s32 j = 0; j < primitive->size; ++j) {
      if (rei_json_string_eq (state, "indices", 7)) {
        rei_json_parse_u32 (state, &new_primitive->indices_index);
      } else if (rei_json_string_eq (state, "material", 8)) {
        rei_json_parse_u32 (state, &new_primitive->material_index);
      } else if (rei_json_string_eq (state, "attributes", 10)) {
        ++state->current_token;
        const jsmntok_t* current_attribute = state->current_token++;

        for (s32 k = 0; k < current_attribute->size; ++k) {
  	  if (rei_json_string_eq (state, "POSITION", 8)) {
            rei_json_parse_u32 (state, &new_primitive->position_index);
  	  } else if (rei_json_string_eq (state, "NORMAL", 6)) {
            rei_json_parse_u32 (state, &new_primitive->normal_index);
  	  } else if (rei_json_string_eq (state, "TEXCOORD_0", 10)) {
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
}

static void _s_gltf_parse_meshes (rei_json_state_t* state, rei_gltf_t* out) {
  REI_IGNORE_WARN_START (-Wincompatible-pointer-types)
  rei_json_parse_array (state, sizeof *out->meshes, &out->mesh_count, &out->meshes);
  REI_IGNORE_WARN_STOP

  register u32 count = out->mesh_count;
  rei_gltf_mesh_t* data = out->meshes;

  for (register u32 i = 0; i < count; ++i) {
    rei_gltf_mesh_t* new_mesh = &data[i];
    const jsmntok_t* current_mesh = state->current_token++;

    for (s32 j = 0; j < current_mesh->size; ++j) {
      if (rei_json_string_eq (state, "primitives", 10)) {
        _s_gltf_parse_primitives (state, new_mesh);
      } else {
        rei_json_skip (state);
      }
    }

    REI_ASSERT (new_mesh->primitive_count);
  }
}

#if 0
static void _s_gltf_parse_animation_channels (rei_json_state_t* state, rei_gltf_animation_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->channel_count = (u32) root_token->size;
  out->channels = malloc (sizeof *out->channels * out->channel_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_animation_channel_t* new_channel = &out->channels[i];
    const jsmntok_t* current_channel = state->current_token++;

    for (s32 j = 0; j < current_channel->size; ++j) {
      if (rei_json_string_eq (state, "sampler")) {
        rei_json_parse_u32 (state, &new_channel->sampler_index);
      } else if (rei_json_string_eq (state, "target")) {
        ++state->current_token;
	const jsmntok_t* current_target = state->current_token++;

	for (s32 k = 0; k < current_target->size; ++k) {
          if (rei_json_string_eq (state, "node")) {
            rei_json_parse_u32 (state, &new_channel->target_node_index);
	  } else if (rei_json_string_eq (state, "path")) {
            rei_string_view_t path;
	    rei_json_parse_string (state, &path);

	    if (!strncmp (path.src, "rotation", path.size)) {
              new_channel->path = REI_GLTF_CHANNEL_PATH_ROTATION;
	    } else if (!strncmp (path.src, "scale", path.size)) {
              new_channel->path = REI_GLTF_CHANNEL_PATH_SCALE;
	    } else if (!strncmp (path.src, "translation", path.size)) {
              new_channel->path = REI_GLTF_CHANNEL_PATH_TRANSLATION;
	    } else {
              new_channel->path = REI_GLTF_CHANNEL_PATH_WEIGHTS;
	    }
	  } else {
            rei_json_skip (state);
	  }
	}
      } else {
        rei_json_skip (state);
      }
    }

    REI_LOG_ERROR ("Animation channel %d: path %d, sampler %u, node %u", i, new_channel->path, new_channel->sampler_index, new_channel->target_node_index);

  }
}

static void _s_gltf_parse_animation_samplers (rei_json_state_t* state, rei_gltf_animation_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->sampler_count = (u32) root_token->size;
  out->samplers = malloc (sizeof *out->samplers * out->sampler_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_animation_sampler_t* new_sampler = &out->samplers[i];
    const jsmntok_t* current_sampler = state->current_token++;

    for (s32 j = 0; j < current_sampler->size; ++j) {
      if (rei_json_string_eq (state, "input", 5)) {
        rei_json_parse_u32 (state, &new_sampler->input);
      } else if (rei_json_string_eq (state, "interpolation", 13)) {
        rei_string_view_t interpolation;
        rei_json_parse_string (state, &interpolation);

	if (!strncmp (interpolation.src, "LINEAR", interpolation.size)) {
          new_sampler->interpolation = REI_GLTF_INTERPOLATION_LINEAR;
	} else {
          REI_LOG_ERROR ("Unknown animation sampler interpolation %.*s", interpolation.size, interpolation.src);
	}
      } else if (rei_json_string_eq (state, "output", 6)) {
        rei_json_parse_u32 (state, &new_sampler->output);
      }
    }
    REI_LOG_WARN ("Animation sampler %d: input %u, interpolation %d, output %u", i, new_sampler->input, new_sampler->interpolation, new_sampler->output);
  }
}

static void _s_gltf_parse_animations (rei_json_state_t* state, rei_gltf_t* out) {
  ++state->current_token;
  const jsmntok_t* root_token = state->current_token++;
  REI_ASSERT (root_token->type == JSMN_ARRAY);

  out->animation_count = (u32) root_token->size;
  out->animations = malloc (sizeof *out->animations * out->animation_count);

  for (s32 i = 0; i < root_token->size; ++i) {
    rei_gltf_animation_t* new_animation = &out->animations[i];
    const jsmntok_t* current_animation = state->current_token++;

    for (s32 j = 0; j < current_animation->size; ++j) {
      if (rei_json_string_eq (state, "channels", 8)) {
        _s_gltf_parse_animation_channels (state, new_animation);
      } else {
        _s_gltf_parse_animation_samplers (state, new_animation);
      }
    }
  }
}
#endif

rei_result_e rei_gltf_load (const char* relative_path, rei_gltf_t* out) {
  REI_LOG_INFO ("Loading GLTF model from " REI_ANSI_YELLOW "\"%s\"", relative_path);

  rei_file_t gltf;
  REI_CHECK (rei_read_file (relative_path, &gltf));

  rei_json_state_t json_state;
  REI_CHECK (rei_json_tokenize ((const char*) gltf.data, gltf.size, &json_state));

  const jsmntok_t* root_token = json_state.current_token++;
  REI_ASSERT (root_token->type == JSMN_OBJECT);

  out->animations = NULL;

  for (s32 i = 0; i < root_token->size; ++i) {
    if (rei_json_string_eq (&json_state, "nodes", 5)) {
      _s_gltf_parse_nodes (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "buffers", 7)) {
      _s_gltf_parse_buffers (relative_path, &json_state, out);
    } else if (rei_json_string_eq (&json_state, "bufferViews", 11)) {
      _s_gltf_parse_buffer_views (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "accessors", 9)) {
      _s_gltf_parse_accessors (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "samplers", 8)) {
      _s_gltf_parse_samplers (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "images", 6)) {
      _s_gltf_parse_images (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "textures", 8)) {
      _s_gltf_parse_textures (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "materials", 9)) {
      _s_gltf_parse_materials (&json_state, out);
    } else if (rei_json_string_eq (&json_state, "meshes", 6)) {
      _s_gltf_parse_meshes (&json_state, out);
#if 0
    } else if (rei_json_string_eq (&json_state, "animations", json_state.current_token)) {
      REI_LOG_STR_INFO ("Parsing animations...");
      _s_gltf_parse_animations (&json_state, out);
#endif
    } else {
      rei_json_skip (&json_state);
    }
  }

  free (json_state.json_tokens);
  rei_free_file (&gltf);

  return REI_RESULT_SUCCESS;
}

void rei_gltf_destroy (rei_gltf_t* gltf) {
  for (u32 i = 0; i < gltf->buffer_count; ++i) rei_free_file (&gltf->buffers[i]);
  free (gltf->buffers);

  if (gltf->animations) {
    for (u32 i = 0; i < gltf->animation_count; ++i) {
      rei_gltf_animation_t* current_animation = &gltf->animations[i];

      for (u32 j = 0; j < current_animation->channel_count; ++j) free (current_animation->channels);
      for (u32 j = 0; j < current_animation->sampler_count; ++j) free (current_animation->samplers);
    }

    free (gltf->animations);
  }

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
