#ifndef REI_WINDOW_H
#define REI_WINDOW_H

#include "rei_types.h"

#ifdef __linux__

#define REI_X11_KEY_W 25
#define REI_X11_KEY_A 38
#define REI_X11_KEY_S 39
#define REI_X11_KEY_D 40
#define REI_X11_MOUSE_LEFT 1
#define REI_X11_KEY_ESCAPE 9
#define REI_X11_MOUSE_RIGHT 3
#define REI_X11_MOUSE_WHEEL_UP 4
#define REI_X11_MOUSE_WHEEL_DOWN 5

typedef struct rei_xcb_window_t {
  u16 width;
  u16 height;
  u32 handle;
  struct xcb_connection_t* conn;
} rei_xcb_window_t;

void rei_create_xcb_window (u16 width, u16 height, b8 is_fullscreen, rei_xcb_window_t* out);
void rei_destroy_xcb_window (rei_xcb_window_t* window);

void rei_xcb_get_mouse_pos (const rei_xcb_window_t* window, f32* out);

#else
#error "Unhandled platform..."
#endif

#endif /* REI_WINDOW_H */
