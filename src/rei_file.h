#ifndef REI_FILE_H
#define REI_FILE_H

#include "rei_types.h"

typedef struct rei_file_t {
  s32 fd;
  u32 size;
  void* data;
} rei_file_t;

// Read file by mapping its contents into out->data. No allocations required, but it has to be unmapped (rei_free_file) later.
rei_result_e rei_read_file (const char* const relative_path, rei_file_t* out);

void rei_write_file (const char* const relative_path, const void* data, u64 size);
void rei_free_file (rei_file_t* file);

#endif /* REI_FILE_H */
