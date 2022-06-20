#ifndef REI_FILE_H
#define REI_FILE_H

#include "rei_types.h"

typedef struct rei_file_t {
  s64 desc;
  u64 size;
  void* data;
} rei_file_t;

rei_result_e rei_map_file (const char* const relative_path, rei_file_t* out);
void rei_unmap_file (rei_file_t* file);

#endif /* REI_FILE_H */
