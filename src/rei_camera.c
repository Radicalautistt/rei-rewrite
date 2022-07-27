#include "rei_camera.h"
#include "rei_math.inl"

#define REI_CAMERA_ZOOM 45.f
#define REI_CAMERA_SPEED 40.f

static void _s_update_camera (rei_camera_t* camera) {
  const f32 pitch_cos = cosf (REI_RADIANS (camera->pitch));

  camera->front.x = cosf (REI_RADIANS (camera->yaw)) * pitch_cos;
  camera->front.y = sinf (REI_RADIANS (camera->pitch));
  camera->front.z = sinf (REI_RADIANS (camera->yaw)) * pitch_cos;

  rei_vec3_norm (&camera->front);

  rei_vec3_cross (&camera->front, &camera->up, &camera->right);
  rei_vec3_norm (&camera->right);

  rei_vec3_cross (&camera->right, &camera->front, &camera->up);
  rei_vec3_norm (&camera->up);
}

void rei_camera_create (f32 up_x, f32 up_y, f32 up_z, f32 yaw, f32 pitch, rei_camera_t* out) {
  out->yaw = yaw;
  out->pitch = pitch;
  out->up.x = up_x;
  out->up.y = up_y;
  out->up.z = up_z;

  _s_update_camera (out);
}

rei_mat4_t* rei_camera_create_projection (f32 aspect) {
  rei_mat4_t* out = malloc (sizeof *out);

  rei_perspective (REI_RADIANS (REI_CAMERA_ZOOM), aspect, 0.1f, 1000.f, out);

  return out;
}

void rei_camera_get_view_projection (
  const rei_camera_t* camera,
  const rei_camera_position_t* position,
  const rei_mat4_t* projection,
  rei_mat4_t* out) {

  rei_vec3_u center;
  rei_mat4_t view;

  rei_vec3_add (&position->data, &camera->front, &center);
  rei_look_at (&position->data, &center, &camera->up, &view);
  rei_mat4_mul (projection, &view, out);
}

void rei_camera_move_left (rei_camera_t* camera, rei_camera_position_t* position, f32 delta_time) {
  rei_vec3_u temp;
  rei_vec3_mul_scalar (&camera->right, REI_CAMERA_SPEED * delta_time, &temp);
  rei_vec3_sub (&position->data, &temp, &position->data);
}

void rei_camera_move_right (rei_camera_t* camera, rei_camera_position_t* position, f32 delta_time) {
  rei_vec3_u temp;
  rei_vec3_mul_scalar (&camera->right, REI_CAMERA_SPEED * delta_time, &temp);
  rei_vec3_add (&position->data, &temp, &position->data);
}

void rei_camera_move_forward (rei_camera_t* camera, rei_camera_position_t* position, f32 delta_time) {
  rei_vec3_u temp;
  rei_vec3_mul_scalar (&camera->front, REI_CAMERA_SPEED * delta_time, &temp);
  rei_vec3_add (&position->data, &temp, &position->data);
}

void rei_camera_move_backward (rei_camera_t* camera, rei_camera_position_t* position, f32 delta_time) {
  rei_vec3_u temp;
  rei_vec3_mul_scalar (&camera->front, REI_CAMERA_SPEED * delta_time, &temp);
  rei_vec3_sub (&position->data, &temp, &position->data);
}
