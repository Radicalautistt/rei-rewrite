#include <stdio.h>
#include <stdarg.h>

#include "rei_logger.h"

void rei_logger (rei_log_level_e level, const char* restrict format, ...) {
  static const char colors[][10] = {REI_ANSI_GREEN, REI_ANSI_YELLOW, REI_ANSI_RED};
  static const char names[][11] = {"[INFO]    ", "[WARNING] ", "[ERROR]   "};

  printf ("%s%s", colors[level], names[level]);

  va_list args;
  va_start (args, format);

  vprintf (format, args);
  puts (REI_ANSI_RESET);

  va_end (args);
}
