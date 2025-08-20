/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_MACROS_H
#define FPX_MACROS_H

#include <stdlib.h>

#define ABS(x) ((x < 0) ? (x * -1) : (x))
#define MAX(x, y) ((x > y) ? x : y)
#define MIN(x, y) ((x < y) ? x : y)
#define CLAMP(v, x, y) ((v < x) ? x : (v > y) ? y : v)
#define CONDITIONAL(cond, then, else) ((cond) ? (then) : (else))

#define FPX3D_ONFAIL(result, result_storage, code)                             \
  {                                                                            \
    Fpx3d_E_Result result_storage = result;                                    \
    if (FPX3D_SUCCESS != result_storage) {                                     \
      code                                                                     \
    }                                                                          \
  }

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

#define FREE_SAFE(ptr)                                                         \
  {                                                                            \
    if (NULL != ptr) {                                                         \
      free(ptr);                                                               \
      ptr = NULL;                                                              \
    }                                                                          \
  }

#define NULL_CHECK(value, ret_code)                                            \
  if (NULL == (value))                                                         \
  return ret_code

#define UNUSED(var)                                                            \
  {                                                                            \
    char _fpx_lineinfo_output_buffer[sizeof(__FILE__) + 16];                   \
    FPX3D_LINE_INFO(_fpx_lineinfo_output_buffer);                              \
    FPX3D_DEBUG("Variable %s is unused (at: %s)", #var,                        \
                _fpx_lineinfo_output_buffer);                                  \
    if (var) {                                                                 \
    }                                                                          \
  }

#define ALIGN_UP(num, alignment)                                               \
  CONDITIONAL(num + (alignment - (num % alignment)) % alignment == 0,          \
              num + (alignment - (num % alignment)) - alignment,               \
              num + (alignment - (num % alignment)))

#endif // FPX_MACROS_H
