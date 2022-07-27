#ifndef REI_CAMERA_H
#define REI_CAMERA_H

#include "rei_types.h"

typedef struct rei_camera_t {
  f32 yaw, pitch;
  rei_vec3_t up, front, right;
} rei_camera_t;

typedef struct rei_camera_position_t {
  f32 last_x, last_y;
  rei_vec3_t data;
} rei_camera_position_t;

void rei_camera_create (f32 up_x, f32 up_y, f32 up_z, f32 yaw, f32 pitch, rei_camera_t* out);
rei_mat4_t* rei_camera_create_projection (f32 aspect);

void rei_camera_get_view_projection (
  const rei_camera_t* camera,
  const rei_camera_position_t* position,
  const rei_mat4_t* projection,
  rei_mat4_t* out
);

// Function for every camera movement direction. Yes, I am crazy.
void rei_camera_move_left (rei_camera_t* camera, rei_camera_position_t* position, f32 delta_time);
void rei_camera_move_right (rei_camera_t* camera, rei_camera_position_t* position, f32 delta_time);
void rei_camera_move_forward (rei_camera_t* camera, rei_camera_position_t* position, f32 delta_time);
void rei_camera_move_backward (rei_camera_t* camera, rei_camera_position_t* position, f32 delta_time);

#endif /* REI_CAMERA_H */
