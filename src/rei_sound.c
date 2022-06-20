#include "rei_sound.h"
#include "rei_debug.h"

#include <AL/alc.h>

const char* rei_show_openal_error (s32 error) {
  #define SHOW_ERROR(name) case AL_##name: return #name

  switch (error) {
    /** Invalid name paramater passed to AL call. */
    SHOW_ERROR (INVALID_NAME);
    /** Invalid enum parameter passed to AL call. */
    SHOW_ERROR (INVALID_ENUM);
    /** Invalid value parameter passed to AL call. */
    SHOW_ERROR (INVALID_VALUE);
    /** Illegal AL call. */
    SHOW_ERROR (INVALID_OPERATION);
    /** Not enough memory. */
    SHOW_ERROR (OUT_OF_MEMORY);
    default: return "Unknown";
  }

  #undef SHOW_ERROR
}

static const char* show_openalc_error (s32 error) {
  #define SHOW_ERROR(name) case ALC_##name: return #name

  switch (error) {
    /** Invalid device handle. */
    SHOW_ERROR (INVALID_DEVICE);
    /** Invalid context handle. */
    SHOW_ERROR (INVALID_CONTEXT);
    /** Invalid enum parameter passed to AL call. */
    SHOW_ERROR (INVALID_ENUM);
    /** Invalid value parameter passed to ALC call. */
    SHOW_ERROR (INVALID_VALUE);
    /** Not enough memory. */
    SHOW_ERROR (OUT_OF_MEMORY);
    default: return "Unknown";
  }

  #undef SHOW_ERROR
}

void rei_create_openal_ctxt (rei_openal_ctxt_t* out) {
  out->device = alcOpenDevice ("OpenAL soft");
  REI_ASSERT (out->device);
  out->handle = alcCreateContext (out->device, NULL);

  s32 result = alcGetError (out->device);
  if (result != ALC_NO_ERROR) {
     REI_LOG_ERROR (
       "%s:%d REI OpenAL context error " REI_ANSI_YELLOW "%s",
       __FILE__,
       __LINE__,
       show_openalc_error (result)
     );

     exit (EXIT_FAILURE);
  }

  REI_ASSERT (alcMakeContextCurrent (out->handle));
}

void rei_destroy_openal_ctxt (rei_openal_ctxt_t* context) {
  alcDestroyContext (context->handle);
  alcCloseDevice (context->device);
}
