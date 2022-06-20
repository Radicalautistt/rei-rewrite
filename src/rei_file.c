#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#else
#error "Unhandled platform..."
#endif

#include "rei_file.h"

rei_result_e rei_map_file (const char* const relative_path, rei_file_t* out) {
  out->desc = (s64) open (relative_path, O_RDONLY);

  if (out->desc == -1) return REI_RESULT_FILE_DOES_NOT_EXIST;

  struct stat file_stats;
  fstat (out->desc, &file_stats);

  out->size = (u64) file_stats.st_size;
  out->data = mmap (NULL, out->size, PROT_READ, MAP_PRIVATE, out->desc, 0);

  return REI_RESULT_SUCCESS;
}

void rei_unmap_file (rei_file_t* file) {
  munmap (file->data, file->size);
  close (file->desc);
}
