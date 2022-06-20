#include <string.h>
#include <malloc.h>

#include "rei_window.h"

#ifdef __linux__

#include <xcb/xcb.h>

// TODO Create floating window if !is_fullscreen.
void rei_create_xcb_window (u16 width, u16 height, b8 is_fullscreen, rei_xcb_window_t* out) {
  (void) is_fullscreen;

  out->conn = xcb_connect (NULL, NULL);
  out->handle = xcb_generate_id (out->conn);
  const xcb_screen_t* root_screen = xcb_setup_roots_iterator (xcb_get_setup (out->conn)).data;

  xcb_create_window (
    out->conn,
    XCB_COPY_FROM_PARENT,
    out->handle,
    root_screen->root,
    0,
    0,
    width,
    height,
    0,
    XCB_WINDOW_CLASS_INPUT_OUTPUT,
    root_screen->root_visual,
    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
    (u32[]) {root_screen->white_pixel, XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE}
  );

  xcb_change_property (
    out->conn,
    XCB_PROP_MODE_REPLACE,
    out->handle,
    XCB_ATOM_WM_NAME,
    XCB_ATOM_STRING,
    8,
    11,
    "interstice"
  );

  xcb_map_window (out->conn, out->handle);
  xcb_flush (out->conn);
}

void rei_destroy_xcb_window (rei_xcb_window_t* window) {
  xcb_destroy_window (window->conn, window->handle);
  xcb_disconnect (window->conn);
}

void rei_xcb_get_mouse_pos (const rei_xcb_window_t* window, f32* out) {
  xcb_query_pointer_cookie_t cookie = xcb_query_pointer_unchecked (window->conn, window->handle);
  xcb_query_pointer_reply_t* reply = xcb_query_pointer_reply (window->conn, cookie, NULL);

  out[0] = (f32) reply->win_x;
  out[1] = (f32) reply->win_y;

  free (reply);
}

#else
#error "Unhandled platform..."
#endif
