#ifndef REI_TYPES_H
#define REI_TYPES_H

#include "rei_defines.h"

#ifdef __linux__
  #include <immintrin.h>
#else
  #error "Unhandled platform..."
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned u32;
typedef unsigned long u64;

typedef signed char s8;
typedef signed short s16;
typedef signed s32;
typedef signed long s64;

typedef float f32;
typedef double f64;

typedef u8 b8;
typedef u32 b32;

#define REI_TRUE 1u
#define REI_FALSE 0u

#define REI_U32_MAX (~0u)

typedef enum rei_result_e {
  REI_RESULT_SUCCESS,
  REI_RESULT_INVALID_JSON,
  REI_RESULT_INVALID_FILE_PATH,
  REI_RESULT_FILE_DOES_NOT_EXIST,
  REI_RESULT_UNSUPPORTED_FILE_TYPE
} rei_result_e;

typedef struct rei_vec2_t {f32 x, y;} rei_vec2_t;

REI_IGNORE_WARN_START (-Wpadded)

typedef union REI_ALIGN_AS (16) rei_vec3_u {
  struct {f32 x, y, z;};
  __m128 simd_reg;
} rei_vec3_u;

REI_IGNORE_WARN_STOP

typedef union rei_vec4_u {
  struct {f32 x, y, z, w;};
  __m128 simd_reg;
} rei_vec4_u;

typedef struct rei_mat4_t {rei_vec4_u rows[4];} rei_mat4_t;

typedef struct rei_vertex_t {
  f32 x, y, z;
  f32 nx, ny, nz;
  f32 u, v;
} rei_vertex_t;

typedef struct rei_image_t {
  u32 width;
  u32 height;
  u32 component_count;
  u32 __padding;
  u8* pixels;
} rei_image_t;

typedef struct rei_string_view_t {
  u64 size;
  const char* src;
} rei_string_view_t;

#endif /* REI_TYPES_H */
