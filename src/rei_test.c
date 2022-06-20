#include "rei_window.h"
#include "unistd.h"

int main (void) {
  #ifdef __linux__
  rei_xcb_window_t window;
  rei_create_xcb_window (720, 480, REI_FALSE, &window);

  sleep (1);

  rei_destroy_xcb_window (&window);
  #else
  #error "Unhandled platform..."
  #endif
}
