#include <stdio.h>
#include <memory.h>
#include <string.h>

#ifdef __linux__
#  include <dirent.h>
#else
#  error "Unhandled platform..."
#endif

#include "rei_asset.h"
#include "rei_debug.h"
#include "rei_parse.h"
#include "rei_asset_loaders.h"

#include <lz4/lib/lz4.h>

#define _S_RTEXTURE_JSON_MAX_SIZE 128u

rei_result_e rei_texture_compress (const char* const relative_path) {
  rei_image_t src_image;

  char out_path[128] = {0};
  strcpy (out_path, relative_path);

  char* ext = strrchr (out_path, '.');

  if (ext++) {
    if (!strcmp (ext, "jpg") || !strcmp (ext, "jpeg")) {
      rei_load_jpeg (relative_path, &src_image);
    } else if (!strcmp (ext, "png")) {
      rei_load_png (relative_path, &src_image);
    } else {
      return REI_RESULT_UNSUPPORTED_FILE_TYPE;
    }
  }

  // Compress image.
  const s32 image_size = (s32) (src_image.width * src_image.height * src_image.component_count);
  const s32 compressed_bound = LZ4_compressBound (image_size);

  char* compressed_data = malloc ((u64) compressed_bound);
  const char* image_data = (const char*) src_image.pixels;
  const s32 compressed_size = LZ4_compress_default (image_data, compressed_data, image_size, compressed_bound);

  // Shrink allocated buffer if necessary.
  if (compressed_size < compressed_bound) {
    char* temp = compressed_data;
    compressed_data = malloc ((u64) compressed_size);
    memcpy (compressed_data, temp, (u64) compressed_size);
    free (temp);
  }

  // Create JSON metadata.
  char json_metadata[_S_RTEXTURE_JSON_MAX_SIZE] = {0};
  char json_metadata_fmt[] = "{\"width\":%u,\"height\":%u,\"component_count\":%u,\"compressed_size\":%u}";

  sprintf (json_metadata, json_metadata_fmt, src_image.width, src_image.height, src_image.component_count, compressed_size);
  const u32 json_metadata_size = (u32) strlen (json_metadata);

  strcpy (ext, "rtex");

  FILE* out_file = fopen (out_path, "wb");
  fwrite (&json_metadata_size, sizeof (u32), 1, out_file);
  fwrite (json_metadata, 1, json_metadata_size, out_file);
  fwrite (compressed_data, 1, (u64) compressed_size, out_file);

  free (compressed_data);
  fclose (out_file);

  return REI_RESULT_SUCCESS;
}

rei_result_e rei_compress_texture_dir (const char* const relative_path) {
  DIR* dir = opendir (relative_path);

  struct dirent* current;
  while ((current = readdir (dir))) {
    if (current->d_type != 4) {
      char full_path[128] = {0};
      strcpy (full_path, relative_path);

      // Make sure that it's not compressed already.
      const char* ext = strrchr (current->d_name, '.');
      if (ext++) {
        if (strcmp (ext, "rtex") && strcmp (ext, "gltf") && strcmp (ext, "bin")) {
          strcpy (full_path + strlen (relative_path), current->d_name);
          REI_CHECK (rei_texture_compress (full_path));
        }
      }
    }
  }

  return REI_RESULT_SUCCESS;
}

rei_result_e rei_texture_load (const char* const relative_path, rei_texture_t* out) {
  // Make sure that we are dealing with an RTEX file.
  REI_ASSERT (!strcmp (strrchr (relative_path, '.'), ".rtex"));

  REI_CHECK (rei_map_file (relative_path, &out->mapped_file));
  const char* file_data = out->mapped_file.data;

  // Parse JSON metadata.
  const u32 json_size = *((const u32*) file_data);
  file_data += sizeof (u32);

  char json[_S_RTEXTURE_JSON_MAX_SIZE];
  memcpy (json, file_data, json_size);
  json[json_size] = '\0';

  rei_json_state_t json_state;
  REI_CHECK (rei_json_tokenize (json, json_size, &json_state));

  const jsmntok_t* root_token = json_state.current_token++;
  REI_ASSERT (root_token->type == JSMN_OBJECT);

  for (s32 i = 0; i < root_token->size; ++i) {
    if (rei_json_string_eq (&json_state, "width", 5)) {
      rei_json_parse_u32 (&json_state, &out->width);
    } else if (rei_json_string_eq (&json_state, "height", 6)) {
      rei_json_parse_u32 (&json_state, &out->height);
    } else if (rei_json_string_eq (&json_state, "component_count", 15)) {
      rei_json_parse_u32 (&json_state, &out->component_count);
    } else if (rei_json_string_eq (&json_state, "compressed_size", 15)) {
      rei_json_parse_u32 (&json_state, &out->compressed_size);
    }
  }

  free (json_state.json_tokens);
  file_data += json_size;

  out->compressed_data = file_data;

  return REI_RESULT_SUCCESS;
}

void rei_texture_destroy (rei_texture_t* texture) {
  rei_unmap_file (&texture->mapped_file);
}
