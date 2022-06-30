#ifndef REI_LOGGER_H
#define REI_LOGGER_H

#include "rei_types.h"

// List of ANSI colors.
#define REI_ANSI_RESET "\033[;0m"
#define REI_ANSI_RED "\033[1;31m"
#define REI_ANSI_BLUE "\033[1;34m"
#define REI_ANSI_CYAN "\033[1;36m"
#define REI_ANSI_BLACK "\033[1;30m"
#define REI_ANSI_WHITE "\033[1;37m"
#define REI_ANSI_GREEN "\033[1;32m"
#define REI_ANSI_YELLOW "\033[1;33m"
#define REI_ANSI_PURPLE "\033[1;35m"

#define REI_LOG_INFO(format, ...) rei_logger (REI_LOG_LEVEL_INFO, format, __VA_ARGS__)
#define REI_LOG_WARN(format, ...) rei_logger (REI_LOG_LEVEL_WARN, format, __VA_ARGS__)
#define REI_LOG_ERROR(format, ...) rei_logger (REI_LOG_LEVEL_ERROR, format, __VA_ARGS__)

// Special purpose macros for strings, lest to type REI_LOG_* ("%s", string) all the time.
#define REI_LOG_STR_INFO(string) REI_LOG_INFO ("%s", string)
#define REI_LOG_STR_WARN(string) REI_LOG_WARN ("%s", string)
#define REI_LOG_STR_ERROR(string) REI_LOG_ERROR ("%s", string)

typedef enum rei_log_level_e {
  REI_LOG_LEVEL_INFO,
  REI_LOG_LEVEL_WARN,
  REI_LOG_LEVEL_ERROR,
} rei_log_level_e;

void rei_logger (rei_log_level_e level, const char* restrict format, ...);

#endif /* REI_LOGGER_H */
