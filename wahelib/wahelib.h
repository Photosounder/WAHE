#ifndef WAHELIB_H
#define WAHELIB_H

#ifdef __cplusplus
extern "C" {
#endif

  #include <stdio.h>
  #include <stdlib.h>
  #include <stdint.h>
  #include <stdarg.h>
  #include <math.h>
  #include <inttypes.h>

  #ifdef WAHE_WASMTIME
    #include <wasmtime.h>

    #ifdef _MSC_VER
      #pragma comment (lib, "wasmtime.dll.lib")
    #endif
  #endif

  #ifndef H_ROUZICLIB
    #ifndef fprintf_rl
      #define fprintf_rl fprintf
    #endif
    #include "rl_utils/windows_includes.h"
    #include "rl_utils/misc.h"
    #include "rl_utils/generic_buffer.h"
    #include "rl_utils/threading.h"
  #endif

  #include "wahe/wahe_core.h"
  #include "wahe/wahe_execution.h"
  #include "wahe/wahe_parser.h"

#ifdef __cplusplus
}
#endif

#endif // WAHELIB_H
