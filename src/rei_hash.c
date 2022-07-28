#include "rei_hash.h"

// Basically a stolen sample implementation from wikipedia (I have no idea what it's doing).
u32 rei_murmur_hash (const u8* key, u64 length, u32 seed) {
  const u32 c1 = 0xcc9e2d51;
  const u32 c2 = 0x1b873593;
  const u32 r1 = 15;
  const u32 r2 = 13;
  const u32 m = 5;
  const u32 n = 0xe6546b64;

  u32 hash = seed;

  for (u64 i = length >> 2; i; --i) {
    u32 k = *((u32*) key);
    key += sizeof (u32);

    k *= c1;
    // what the heck.
    k = (k << r1) | (k >> 17);
    k *= c2;

    hash ^= k;
    hash = (hash << r2) | (hash >> 19);
    hash = (hash * m) + n;
  }

  u32 k = 0;
  for (u64 i = length & 3; i; --i) {
    k <<= 8;
    k |= key[i - 1];
  }

  k *= c1;
  // what the heck.
  k = (k << r1) | (k >> 17);
  k *= c2;

  hash ^= k;
  hash ^= (u32) length;

  hash ^= hash >> 16;
  hash *= 0x85ebca6b;
  hash ^= hash >> 13;
  hash *= 0xc2b2ae35;
  hash ^= hash >> 16;

  return hash;
}
