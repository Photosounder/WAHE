#include "wahelib.h"

#ifndef H_ROUZICLIB
  #include "rl_utils/fopen_utf8.c"
  #include "rl_utils/dynamic_loading.c"
  #include "rl_utils/misc.c"
  #include "rl_utils/threading.c"
  #include "rl_utils/generic_buffer.c"
  #include "rl_utils/io_override.c"
  #include "rl_utils/endian.c"
  #include "rl_utils/time.c"
#endif

#include "wahe/wahe_core.c"
#include "wahe/wahe_execution.c"
#include "wahe/wahe_parser.c"
#include "wasm/wasm_binary_parser.c"
