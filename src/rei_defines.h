#ifndef REI_DEFINES_H
#define REI_DEFINES_H

// Count of render frames in flight.
#define REI_VK_FRAME_COUNT 2u

#define REI_MIN(__a, __b) (((__a) > (__b)) ? (__b) : (__a))
#define REI_MAX(__a, __b) (((__a) > (__b)) ? (__a) : (__b))
#define REI_ARRAY_SIZE(__array) (sizeof __array / sizeof *__array)
#define REI_CLAMP(__value, __min, __max) (REI_MAX (__min, REI_MIN (__value, __max)))
#define REI_OFFSET_OF(__struct, __member) ((size_t) &(((__struct*) NULL)->__member))

#define REI_FORCE_INLINE __attribute__ ((always_inline))
#define REI_ALIGN_AS(__value) __attribute__ ((aligned (__value)))

#define REI_PRAGMA(__pragma) _Pragma (#__pragma)

#define REI_IGNORE_WARN_START(__warn) \
  REI_PRAGMA (GCC diagnostic push)    \
  REI_PRAGMA (GCC diagnostic ignored #__warn)

#define REI_IGNORE_WARN_STOP REI_PRAGMA (GCC diagnostic pop)

#endif /* REI_DEFINES_H */
