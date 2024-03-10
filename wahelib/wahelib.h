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

  #ifndef H_ROUZICLIB
    #ifndef fprintf_rl
      #define fprintf_rl fprintf
    #endif
    #include "rl_utils/windows_includes.h"
    #include "rl_utils/dirent.h"
    #include "rl_utils/misc.h"
    #include "rl_utils/threading.h"
    #include "rl_utils/generic_buffer.h"
    #include "rl_utils/io_override.h"
    #include "rl_utils/endian.h"
  #endif

  #ifdef WAHE_WASMTIME
    #ifdef _MSC_VER
      #define WASM_API_EXTERN	// Static linking

      #ifdef WASM_API_EXTERN
        #define WASI_API_EXTERN
        #pragma comment (lib, "wasmtime.lib")
        #pragma comment (lib, "Ws2_32.lib")
        #pragma comment (lib, "NtDll.lib")
        #pragma comment (lib, "Userenv.lib")
        #pragma comment (lib, "Bcrypt.lib")
      #else
        #pragma comment (lib, "wasmtime.dll.lib")
      #endif
    #endif

    #include <wasmtime.h>
  #endif

  #include "wahe/wahe_core.h"
  #include "wahe/wahe_execution.h"
  #include "wahe/wahe_parser.h"
  #include "wasm/wasm_binary_parser.h"

#ifdef __cplusplus
}
#endif

#endif // WAHELIB_H
