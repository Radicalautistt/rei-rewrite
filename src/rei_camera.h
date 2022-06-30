#ifndef REI_CAMERA_H
#define REI_CAMERA_H

#include "rei_types.h"

typedef struct rei_camera_t {
  f32 yaw, pitch, lastX, lastY;
  rei_vec3_t up, front, right, world_up, position;
  rei_mat4_t projection_matrix;
} rei_camera_t;

void rei_update_camera (rei_camera_t* camera);
void rei_create_camera (const rei_vec3_t* up, const rei_vec3_t* position, f32 aspect, f32 yaw, f32 pitch, rei_camera_t* out);

void rei_camera_get_view_projection (const rei_camera_t* camera, rei_mat4_t* out);

// Function for every camera movement direction. Yes, I am crazy.
void rei_move_camera_left (rei_camera_t* camera, f32 delta_time);
void rei_move_camera_right (rei_camera_t* camera, f32 delta_time);
void rei_move_camera_forward (rei_camera_t* camera, f32 delta_time);
void rei_move_camera_backward (rei_camera_t* camera, f32 delta_time);

#endif /* REI_CAMERA_H */
