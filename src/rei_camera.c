#include "rei_camera.h"
#include "rei_math.inl"

#define REI_CAMERA_ZOOM 45.f
#define REI_CAMERA_SPEED 40.f

void rei_update_camera (rei_camera_t* camera) {
  camera->front.x = cosf (rei_radians (camera->yaw)) * cosf (rei_radians (camera->pitch));
  camera->front.y = sinf (rei_radians (camera->pitch));
  camera->front.z = sinf (rei_radians (camera->yaw)) * cosf (rei_radians (camera->pitch));

  rei_vec3_norm (&camera->front);

  rei_vec3_cross (&camera->front, &camera->world_up, &camera->right);
  rei_vec3_norm (&camera->right);

  rei_vec3_cross (&camera->right, &camera->front, &camera->up);
  rei_vec3_norm (&camera->up);
}

void rei_create_camera (const rei_vec3_t* up, const rei_vec3_t* position, f32 aspect, f32 yaw, f32 pitch, rei_camera_t* out) {
  out->yaw = yaw;
  out->pitch = pitch;
  out->world_up = *up;
  out->position = *position;

  rei_update_camera (out);
  rei_perspective (rei_radians (REI_CAMERA_ZOOM), aspect, 0.1f, 1000.f, &out->projection_matrix);
}

void rei_move_camera_left (rei_camera_t* camera, f32 delta_time) {
  rei_vec3_t temp;
  rei_vec3_mul_scalar (&camera->right, REI_CAMERA_SPEED * delta_time, &temp);
  rei_vec3_sub (&camera->position, &temp, &camera->position);
}

void rei_move_camera_right (rei_camera_t* camera, f32 delta_time) {
  rei_vec3_t temp;
  rei_vec3_mul_scalar (&camera->right, REI_CAMERA_SPEED * delta_time, &temp);
  rei_vec3_add (&camera->position, &temp, &camera->position);
}

void rei_move_camera_forward (rei_camera_t* camera, f32 delta_time) {
  rei_vec3_t temp;
  rei_vec3_mul_scalar (&camera->front, REI_CAMERA_SPEED * delta_time, &temp);
  rei_vec3_add (&camera->position, &temp, &camera->position);
}

void rei_move_camera_backward (rei_camera_t* camera, f32 delta_time) {
  rei_vec3_t temp;
  rei_vec3_mul_scalar (&camera->front, REI_CAMERA_SPEED * delta_time, &temp);
  rei_vec3_sub (&camera->position, &temp, &camera->position);
}
