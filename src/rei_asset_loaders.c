#include <memory.h>

#include "rei_file.h"
#include "rei_debug.h"
#include "rei_asset_loaders.h"

#include <png.h>

void rei_load_wav (const char* relativePath, rei_wav_t * out) {
  REI_LOG_INFO ("Loading a sound from " REI_ANSI_YELLOW "%s", relativePath);

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
static void read_png (png_structp png_reader, png_bytep out, size_t count) {
  rei_file_t* png_file = (rei_file_t*) png_get_io_ptr (png_reader);
  memcpy (out, png_file->data, count);
  png_file->data += count;
}

void rei_load_png (const char* relative_path, rei_png_t* out) {
  REI_LOG_INFO ("Loading an image from " REI_ANSI_YELLOW "%s", relative_path);

  rei_file_t png_file;
  REI_CHECK (rei_map_file (relative_path, &png_file));

  // Make sure that provided image is a valid PNG.
  REI_ASSERT (!png_sig_cmp (png_file.data, 0, 8));
  png_file.data += 8;

  png_structp png_reader = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop png_info = png_create_info_struct (png_reader);

  png_set_read_fn (png_reader, &png_file, read_png);
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
