#ifndef REI_DEBUG_H
#define REI_DEBUG_H

#include <stdlib.h>
#include <immintrin.h>

#include "rei_logger.h"

#ifdef NDEBUG
#  define REI_ASSERT(condition) (void) 0
#else
#  define REI_ASSERT(condition)                                                \
     if (condition) {                                                          \
       (void) 0;                                                               \
     } else {                                                                  \
       REI_LOG_ERROR (                                                         \
         "%s:%d REI Assertion " REI_ANSI_YELLOW "[%s]" REI_ANSI_RED " failed", \
         __FILE__,                                                             \
         __LINE__,                                                             \
         #condition                                                            \
       );	                                                               \
                                                                               \
       __builtin_trap ();                                                      \
     }
#endif

#ifdef NDEBUG
#  define REI_CHECK(call) call
#else
#  define REI_CHECK(call) do {                                                                       \
     rei_result_e error = call;                                                                      \
     if (error) {                                                                                    \
       REI_LOG_ERROR (                                                                               \
         "%s:%d REI error " REI_ANSI_YELLOW "[%s]" REI_ANSI_RED " occured in " REI_ANSI_YELLOW "%s", \
         __FILE__,                                                                                   \
         __LINE__,                                                                                   \
         rei_show_result (error),                                                                    \
         __FUNCTION__                                                                                \
       );                                                                                            \
                                                                                                     \
       exit (EXIT_FAILURE);                                                                          \
     }                                                                                               \
   } while (0)
#endif

#define REI_COUNT_CYCLES(proc) do {       \
  u64 s = __rdtsc ();                     \
  (void) proc;                            \
  u64 e = __rdtsc () - s;                 \
                                          \
  REI_LOG_INFO (                          \
    REI_ANSI_YELLOW "%s"                  \
    REI_ANSI_GREEN " took "               \
    REI_ANSI_YELLOW "%lu"                 \
    REI_ANSI_GREEN " cycles to compute.", \
    #proc,                                \
    e                                     \
  );                                      \
} while (0)

// Stringify rei_result_e for debugging purposes.
const char* rei_show_result (rei_result_e result);

#endif /* REI_DEBUG_H */
