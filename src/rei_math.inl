#ifndef REI_MATH_INL
#define REI_MATH_INL

#include "rei_types.h"

#include <math.h>

#warning "TODO handle SIMD instruction sets availability."

#define REI_RADIANS(__degrees) (__degrees * 0.01745329251994329576923690768489f)

// Set of functions that operate on/return a 128 bit simd register.
static inline __m128 rei_m128_negate (__m128 value) {
  return _mm_sub_ps (_mm_setzero_ps (), value);
}

static inline __m128 rei_m128_inverse_sqrt (__m128 value) {
  // 1 / sqrt (value)
  return _mm_div_ps (_mm_set1_ps (1.f), _mm_sqrt_ps (value));
}

static inline __m128 rei_m128_dot (__m128 a, __m128 b) {
  // a.x * b.x + a.y * b.y + a.z * b.z
  __m128 product = _mm_mul_ps (a, b);
  __m128 horizontal_sum = _mm_hadd_ps (product, product);
  return _mm_hadd_ps (horizontal_sum, horizontal_sum);
}

static inline __m128 rei_m128_norm (__m128 value) {
  // value * (1 / sqrt (dot (value, value)))
  return _mm_mul_ps (value, rei_m128_inverse_sqrt (rei_m128_dot (value, value)));
}

static inline __m128 rei_m128_cross (__m128 a, __m128 b) {
  // [ a.y * b.z - a.z * b.y,
  //   a.z * b.x - a.x * b.z,
  //   a.x * b.y - a.y * b.x ]

  // Before performing any arithmetic operations, we must
  // reshuffle contents of both registers so that their order
  // is equal to argument order of the formula above.
  // NOTE _MM_SHUFFLE's argument order is a layout we
  // want our register to have. E.g. _MM_SHUFFLE (3, 0, 2, 1)
  // used with _mm_shuffle_ps returns a register
  // of form (w, x, z, y) (in this case w is ingnored since we are operating on 3d vectors),
  // just like the first column of the formula (in reverse order).
  return _mm_sub_ps (
    _mm_mul_ps (
      _mm_shuffle_ps (a, a, _MM_SHUFFLE (3, 0, 2, 1)),
      _mm_shuffle_ps (b, b, _MM_SHUFFLE (3, 1, 0, 2))
    ),
    _mm_mul_ps (
      _mm_shuffle_ps (a, a, _MM_SHUFFLE (3, 1, 0, 2)),
      _mm_shuffle_ps (b, b, _MM_SHUFFLE (3, 0, 2, 1))
    )
  );
}

static inline void rei_vec3_add (const rei_vec3_u* a, const rei_vec3_u* b, rei_vec3_u* out) {
  out->simd_reg = _mm_add_ps (a->simd_reg, b->simd_reg);
}

static inline void rei_vec3_sub (const rei_vec3_u* a, const rei_vec3_u* b, rei_vec3_u* out) {
  out->simd_reg = _mm_sub_ps (a->simd_reg, b->simd_reg);
}

static inline void rei_vec3_mul_scalar (const rei_vec3_u* vector, f32 scalar, rei_vec3_u* out) {
  out->simd_reg = _mm_mul_ps (_mm_set1_ps (scalar), vector->simd_reg);
}

static inline void rei_vec3_norm (rei_vec3_u* out) {
  out->simd_reg = rei_m128_norm (out->simd_reg);
}

static inline f32 rei_vec3_dot (const rei_vec3_u* a, const rei_vec3_u* b) {
  return _mm_cvtss_f32 (rei_m128_dot (a->simd_reg, b->simd_reg));
}

static inline void rei_vec3_cross (const rei_vec3_u* a, const rei_vec3_u* b, rei_vec3_u* out) {
  out->simd_reg = rei_m128_cross (a->simd_reg, b->simd_reg);
}

static inline void rei_vec4_add (const rei_vec4_u* a, const rei_vec4_u* b, rei_vec4_u* out) {
  out->simd_reg = _mm_add_ps (a->simd_reg, b->simd_reg);
}

static inline void rei_vec4_mul (const rei_vec4_u* a, const rei_vec4_u* b, rei_vec4_u* out) {
  out->simd_reg = _mm_mul_ps (a->simd_reg, b->simd_reg);
}

static inline void rei_vec4_mul_scalar (const rei_vec4_u* vector, f32 scalar, rei_vec4_u* out) {
  out->simd_reg = _mm_mul_ps (_mm_set1_ps (scalar), vector->simd_reg);
}

static inline void rei_mat4_create_default (rei_mat4_t* out) {
  out->rows[0].x = 1.f;
  out->rows[0].y = 0.f;
  out->rows[0].z = 0.f;
  out->rows[0].w = 0.f;

  out->rows[1].x = 0.f;
  out->rows[1].y = 1.f;
  out->rows[1].z = 0.f;
  out->rows[1].w = 0.f;

  out->rows[2].x = 0.f;
  out->rows[2].y = 0.f;
  out->rows[2].z = 1.f;
  out->rows[2].w = 0.f;

  out->rows[3].x = 0.f;
  out->rows[3].y = 0.f;
  out->rows[3].z = 0.f;
  out->rows[3].w = 1.f;
}

static inline void rei_mat4_scale (rei_mat4_t* matrix, const rei_vec3_u* vector) {
  rei_vec4_mul_scalar (&matrix->rows[0], vector->x, &matrix->rows[0]);
  rei_vec4_mul_scalar (&matrix->rows[1], vector->y, &matrix->rows[1]);
  rei_vec4_mul_scalar (&matrix->rows[2], vector->z, &matrix->rows[2]);
}

static inline void rei_mat4_translate (rei_mat4_t* matrix, const rei_vec3_u* vector) {
  // matrix->rows[3] =
  //   matrix->rows[0] * vector->x +
  //   matrix->rows[1] * vector->y +
  //   matrix->rows[2] * vector->z + matrix->rows[3];

  rei_vec4_u a, b, c;
  rei_vec4_mul_scalar (&matrix->rows[0], vector->x, &a);
  rei_vec4_mul_scalar (&matrix->rows[1], vector->y, &b);
  rei_vec4_mul_scalar (&matrix->rows[2], vector->z, &c);

  rei_vec4_add (&a, &b, &b);
  rei_vec4_add (&b, &c, &c);
  rei_vec4_add (&c, &matrix->rows[3], &matrix->rows[3]);
}

static inline void rei_mat4_mul (const rei_mat4_t* a, const rei_mat4_t* b, rei_mat4_t* out) {
  // out[0] = a[0] * b[0].x + a[1] * b[0].y + a[2] * b[0].z + a[3] * b[0].w;
  // out[1] = a[0] * b[1].x + a[1] * b[1].y + a[2] * b[1].z + a[3] * b[1].w;
  // out[2] = a[0] * b[2].x + a[1] * b[2].y + a[2] * b[2].z + a[3] * b[2].w;
  // out[3] = a[0] * b[3].x + a[1] * b[3].y + a[2] * b[3].z + a[3] * b[3].w;

  for (u32 i = 0; i < 4; ++i) {
    out->rows[i].simd_reg = _mm_add_ps (
      _mm_add_ps (
        _mm_mul_ps (a->rows[0].simd_reg, _mm_set1_ps (b->rows[i].x)),
        _mm_mul_ps (a->rows[1].simd_reg, _mm_set1_ps (b->rows[i].y))
      ),
      _mm_add_ps (
        _mm_mul_ps (a->rows[2].simd_reg, _mm_set1_ps (b->rows[i].z)),
        _mm_mul_ps (a->rows[3].simd_reg, _mm_set1_ps (b->rows[i].w))
      )
    );
  }
}

static inline void rei_look_at (const rei_vec3_u* eye, const rei_vec3_u* center, const rei_vec3_u* up, rei_mat4_t* out) {
  rei_vec3_u z = {.simd_reg = rei_m128_norm (_mm_sub_ps (center->simd_reg, eye->simd_reg))};
  rei_vec3_u x = {.simd_reg = rei_m128_norm (rei_m128_cross (z.simd_reg, up->simd_reg))};
  rei_vec3_u y = {.simd_reg = rei_m128_cross (x.simd_reg, z.simd_reg)};

  f32 dot_x_eye = _mm_cvtss_f32 (rei_m128_dot (x.simd_reg, eye->simd_reg));
  f32 dot_y_eye = _mm_cvtss_f32 (rei_m128_dot (y.simd_reg, eye->simd_reg));
  f32 dot_z_eye = _mm_cvtss_f32 (rei_m128_dot (z.simd_reg, eye->simd_reg));

  z.simd_reg = rei_m128_negate (z.simd_reg);

  out->rows[0].x = x.x;
  out->rows[0].y = y.x;
  out->rows[0].w = 0.f;

  out->rows[1].x = x.y;
  out->rows[1].y = y.y;
  out->rows[1].z = z.y;
  out->rows[1].w = 0.f;

  out->rows[2].x = x.z;
  out->rows[2].y = y.z;
  out->rows[2].z = z.z;
  out->rows[2].w = 0.f;

  out->rows[3].x = -dot_x_eye;
  out->rows[3].y = -dot_y_eye;
  out->rows[3].z = dot_z_eye;
  out->rows[3].w = 1.f;
}

static inline void rei_perspective (f32 fov, f32 aspect, f32 z_near, f32 z_far, rei_mat4_t* out) {
  f32 z_length = z_far- z_near;
  f32 focal_length = 1.f / tanf (fov / 2.f);

  out->rows[0].x = focal_length/ aspect;
  out->rows[0].y = 0.f;
  out->rows[0].z = 0.f;
  out->rows[0].w = 0.f;

  out->rows[1].x = 0.f;
  out->rows[1].y = -focal_length;
  out->rows[1].z = 0.f;
  out->rows[1].w = 0.f;

  out->rows[2].x = 0.f;
  out->rows[2].y = 0.f;
  out->rows[2].z = -(z_far + z_near) / z_length;
  out->rows[2].w = -1.f;

  out->rows[3].x = 0.f;
  out->rows[3].y = 0.f;
  out->rows[3].z = -(2 * z_far * z_near) / z_length;
  out->rows[3].w = 0.f;
}

#endif // REI_MATH_INL
