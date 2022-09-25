#ifdef __linux__
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/stat.h>
#  include <sys/mman.h>
#else
#  error "Unhandled platform..."
#endif

#include <memory.h>

#include "rei_file.h"

rei_result_e rei_read_file (const char* const relative_path, rei_file_t* out) {
  out->fd = open (relative_path, O_RDONLY);
  if (out->fd == -1) return REI_RESULT_FILE_DOES_NOT_EXIST;

  struct stat file_stats;
  fstat (out->fd, &file_stats);

  out->size = (u32) file_stats.st_size;
  out->data = mmap (NULL, out->size, PROT_READ, MAP_PRIVATE, out->fd, 0);

  return REI_RESULT_SUCCESS;
}

void rei_write_file (const char* const relative_path, const void* data, u64 size) {
 const s32 fd = open (relative_path, O_RDWR | O_CREAT, (mode_t) 0600);

  lseek (fd, (off_t) size, SEEK_SET);
  write (fd, "", 1);

  void* mapped = mmap (NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);

  memcpy (mapped, data, size);
  msync (mapped, size, MS_SYNC);

  munmap (mapped, size);
  close (fd);
}

void rei_free_file (rei_file_t* file) {
  munmap (file->data, file->size);
  close (file->fd);
}
