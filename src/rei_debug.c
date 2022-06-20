#include "rei_debug.h"

const char* rei_show_result (rei_result_e result) {
  #define SHOW_RESULT(name) case REI_RESULT_##name: return #name

  switch (result) {
    SHOW_RESULT (SUCCESS);
    SHOW_RESULT (FILE_DOES_NOT_EXIST);
    default: return "Unknown result...";
  }

  #undef SHOW_RESULT
}
