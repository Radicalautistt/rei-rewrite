#ifndef REI_HASH_H
#define REI_HASH_H

#include "rei_types.h"

// Murmur3 hash.
REI_CONST u32 rei_murmur_hash (const u8* key, u64 length, u32 seed);

#endif /* REI_HASH_H */
