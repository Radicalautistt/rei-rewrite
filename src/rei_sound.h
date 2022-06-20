#ifndef REI_SOUND_H
#define REI_SOUND_H

#include "rei_types.h"

#include <AL/al.h>

typedef struct ALCdevice ALCdevice;
typedef struct ALCcontext ALCcontext;

#ifdef NDEBUG
#  define REI_OPENAL_CHECK(call) call
#else
#  define REI_OPENAL_CHECK(call) do {                                                                         \
     call;                                                                                            \
     i32 result = alGetError ();                                                                      \
     if (result != AL_NO_ERROR) {                                                                     \
       REI_LOG_ERROR (                                                                                \
         "%s:%d OpenAL error " REI_ANSI_YELLOW "%s" REI_ANSI_RED " occured in " REI_ANSI_YELLOW "%s", \
	 __FILE__,                                                                                    \
	 __LINE__,                                                                                    \
	 rei_show_openal_error (result),                                                              \
	 __FUNCTION__                                                                                 \
       );                                                                                             \
	                                                                                              \
       exit (EXIT_FAILURE);                                                                           \
     }                                                                                                \
   } while (0)
#endif

typedef struct rei_openal_ctxt_t {
  ALCdevice* device;
  ALCcontext* handle;
} rei_openal_ctxt_t ;

const char* rei_show_openal_error (s32 error);

void rei_create_openal_ctxt (rei_openal_ctxt_t * out);
void rei_destroy_openal_ctxt (rei_openal_ctxt_t * context);

// TODO Find a way to handle 24 bits per sample (I'm not sure OpenAL actually does...)
static inline s32 rei_get_openal_format (u16 channelCount, u16 bitsPerSample) {
  return channelCount == 1 ?
    bitsPerSample == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16 :
    bitsPerSample == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
}
#endif /* REI_SOUND_H */
