#ifndef REI_MATH_INL
#define REI_MATH_INL

#include "rei_types.h"

#include <math.h>

#ifdef __linux__
#include <immintrin.h>
#else
#error "Unhandled platform..."
#endif

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
  __m128 rows[4] = {
    [0] = _mm_shuffle_ps (a, a, _MM_SHUFFLE (3, 0, 2, 1)),
    [1] = _mm_shuffle_ps (b, b, _MM_SHUFFLE (3, 1, 0, 2)),
    [2] = _mm_shuffle_ps (a, a, _MM_SHUFFLE (3, 1, 0, 2)),
    [3] = _mm_shuffle_ps (b, b, _MM_SHUFFLE (3, 0, 2, 1)),
  };

  return _mm_sub_ps (_mm_mul_ps (rows[0], rows[1]), _mm_mul_ps (rows[2], rows[3]));
}

static inline __m128 rei_vec3_load (const rei_vec3_t* vector) {
  return _mm_set_ps (0.f, vector->z, vector->y, vector->x);
}

static inline void rei_vec3_add (const rei_vec3_t* a, const rei_vec3_t* b, rei_vec3_t* out) {
  _mm_store_ps (&out->x, _mm_add_ps (rei_vec3_load (a), rei_vec3_load (b)));
}

static inline void rei_vec3_sub (const rei_vec3_t* a, const rei_vec3_t* b, rei_vec3_t* out) {
  _mm_store_ps (&out->x, _mm_sub_ps (rei_vec3_load (a), rei_vec3_load (b)));
}

static inline void rei_vec3_mul_scalar (const rei_vec3_t* vector, f32 scalar, rei_vec3_t* out) {
  _mm_store_ps (&out->x, _mm_mul_ps (_mm_set1_ps (scalar), rei_vec3_load (vector)));
}

static inline void rei_vec3_norm (rei_vec3_t* out) {
  _mm_store_ps (&out->x, rei_m128_norm (rei_vec3_load (out)));
}

static inline f32 rei_vec3_dot (const rei_vec3_t* a, const rei_vec3_t* b) {
  return _mm_cvtss_f32 (rei_m128_dot (rei_vec3_load (a), rei_vec3_load (b)));
}

static inline void rei_vec3_cross (const rei_vec3_t* a, const rei_vec3_t* b, rei_vec3_t* out) {
  _mm_store_ps (&out->x, rei_m128_cross (rei_vec3_load (a), rei_vec3_load (b)));
}

static inline __m128 rei_vec4_load (const rei_vec4_t* vector) {
  return _mm_load_ps (&vector->x);
}

static inline void rei_vec4_add (const rei_vec4_t* a, const rei_vec4_t* b, rei_vec4_t* out) {
  _mm_store_ps (&out->x, _mm_add_ps (rei_vec4_load (a), rei_vec4_load (b)));
}

static inline void rei_vec4_mul (const rei_vec4_t* a, const rei_vec4_t* b, rei_vec4_t* out) {
  _mm_store_ps (&out->x, _mm_mul_ps (rei_vec4_load (a), rei_vec4_load (b)));
}

static inline void rei_vec4_mul_scalar (const rei_vec4_t* vector, f32 scalar, rei_vec4_t* out) {
  _mm_store_ps (&out->x, _mm_mul_ps (_mm_set1_ps (scalar), rei_vec4_load (vector)));
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

static inline void rei_mat4_scale (rei_mat4_t* matrix, const rei_vec3_t* vector) {
  rei_vec4_t temp;
  rei_vec4_mul_scalar (&matrix->rows[0], vector->x, &temp);
  matrix->rows[0] = temp;
  rei_vec4_mul_scalar (&matrix->rows[1], vector->y, &temp);
  matrix->rows[1] = temp;
  rei_vec4_mul_scalar (&matrix->rows[2], vector->z, &temp);
  matrix->rows[2] = temp;
}

static inline void rei_mat4_translate (rei_mat4_t* matrix, const rei_vec3_t* vector) {
  // matrix->rows[3] =
  //   matrix->rows[0] * vector->x +
  //   matrix->rows[1] * vector->y +
  //   matrix->rows[2] * vector->z + matrix->rows[3];

  rei_vec4_t a, b, c;
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

  __m128 aRows[4] = {
    [0] = rei_vec4_load (&a->rows[0]),
    [1] = rei_vec4_load (&a->rows[1]),
    [2] = rei_vec4_load (&a->rows[2]),
    [3] = rei_vec4_load (&a->rows[3])
  };

  for (u32 i = 0; i < 4; ++i) {
    __m128 left = _mm_add_ps (
      _mm_mul_ps (aRows[0], _mm_set1_ps (b->rows[i].x)),
      _mm_mul_ps (aRows[1], _mm_set1_ps (b->rows[i].y))
    );

    __m128 right = _mm_add_ps (
      _mm_mul_ps (aRows[2], _mm_set1_ps (b->rows[i].z)),
      _mm_mul_ps (aRows[3], _mm_set1_ps (b->rows[i].w))
    );

    _mm_store_ps (&out->rows[i].x, _mm_add_ps (left, right));
  }
}

static inline void rei_look_at (const rei_vec3_t* eye, const rei_vec3_t* center, const rei_vec3_t* up, rei_mat4_t* out) {
  __m128 eye_m128 = rei_vec3_load (eye);
  __m128 z = rei_m128_norm (_mm_sub_ps (rei_vec3_load (center), eye_m128));
  __m128 x = rei_m128_norm (rei_m128_cross (z, rei_vec3_load (up)));
  __m128 y = rei_m128_cross (x, z);

  f32 dot_x_eye = _mm_cvtss_f32 (rei_m128_dot (x, eye_m128));
  f32 dot_y_eye = _mm_cvtss_f32 (rei_m128_dot (y, eye_m128));
  f32 dot_z_eye = _mm_cvtss_f32 (rei_m128_dot (z, eye_m128));

  z = rei_m128_negate (z);

  rei_vec3_t a, b, c;
  _mm_store_ps (&a.x, x);
  _mm_store_ps (&b.x, y);
  _mm_store_ps (&c.x, z);

  out->rows[0].x = a.x;
  out->rows[0].y = b.x;
  out->rows[0].z = c.x;
  out->rows[0].w = 0.f;

  out->rows[1].x = a.y;
  out->rows[1].y = b.y;
  out->rows[1].z = c.y;
  out->rows[1].w = 0.f;

  out->rows[2].x = a.z;
  out->rows[2].y = b.z;
  out->rows[2].z = c.z;
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
