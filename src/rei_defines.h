#ifndef REI_DEFINES_H
#define REI_DEFINES_H

#include "rei_types.h"

#define REI_MIN(a, b) (((a) > (b)) ? (b) : (a))
#define REI_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define REI_ARRAY_SIZE(array) (sizeof array / sizeof *array)
#define REI_CLAMP(value, min, max) (REI_MAX (min, REI_MIN (value, max)))
#define REI_OFFSET_OF(structure, member) ((size_t) &(((structure*) NULL)->member))

#endif /* REI_DEFINES_H */
