#ifndef REI_ASSET_LOADERS_H
#define REI_ASSET_LOADERS_H

#include "rei_types.h"

typedef struct rei_wav_t {
  u16 bits_per_sample;
  u16 channel_count;
  s32 sample_rate;
  s32 size;
  u32 __padding_yay;
  u8* data;
} rei_wav_t;

typedef struct rei_png_t {
  u32 width;
  u32 height;
  u8* pixels;
} rei_png_t;

void rei_load_wav (const char* relative_path, rei_wav_t* out);
void rei_load_png (const char* relative_path, rei_png_t* out);

#endif /* REI_ASSET_LOADERS_H */
