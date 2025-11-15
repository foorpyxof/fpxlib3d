/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX3D_DEBUG_H
#define FPX3D_DEBUG_H

//
//  "debug.h"
//  Part of fpxlib3d (https://git.goodgirl.dev/foorpyxof/fpxlib3d)
//  Author: Erynn 'foorpyxof' Scholtes
//

#include <stdio.h>

#if defined(_WIN32) || defined(_WIN64)
#define LONG_FORMAT "ll"
#else
#define LONG_FORMAT "l"
#endif

#undef FPX3D_DEBUG
#define FPX3D_DEBUG(fmt, ...)
#undef FPX3D_WARN
#define FPX3D_WARN(fmt, ...)
#undef FPX3D_ERROR
#define FPX3D_ERROR(fmt, ...)
#undef FPX3D_TODO
#define FPX3D_TODO(fmt, ...)

#undef FPX3D_LINE_INFO
#define FPX3D_LINE_INFO(output)

#if (defined __FILE__ && defined __LINE__)

#undef FPX3D_LINE_INFO
#define FPX3D_LINE_INFO(output) sprintf(output, "%s:%d", __FILE__, __LINE__)

#endif // __FILE__ && __LINE__

#ifdef FPX3D_DEBUG_ENABLE

#undef FPX3D_DEBUG
#define FPX3D_DEBUG(fmt, ...)                                                  \
  fprintf(stderr, "\033[0;92mFPXLIB3D DEBUG:\033[0m " fmt                      \
                  "\033[0m\n" __VA_OPT__(, ) __VA_ARGS__)

#undef FPX3D_WARN
#define FPX3D_WARN(fmt, ...)                                                   \
  fprintf(stderr, "\033[0;93mFPXLIB3D WARN: \033[0m " fmt                      \
                  "\033[0m\n" __VA_OPT__(, ) __VA_ARGS__)

#undef FPX3D_TODO
#define FPX3D_TODO(fmt, ...)                                                   \
  {                                                                            \
    char _fpx_lineinfo_output_buffer[sizeof(__FILE__) + 16];                   \
    FPX3D_LINE_INFO(_fpx_lineinfo_output_buffer);                              \
    fprintf(stderr,                                                            \
            "\033[0;96mFPXLIB3D TODO: \033[0m " fmt                            \
            "\033[0m (at %s)\n" __VA_OPT__(, ) __VA_ARGS__,                    \
            _fpx_lineinfo_output_buffer);                                      \
  }

#endif // FPX3D_DEBUG_ENABLE

#if defined(FPX3D_DEBUG_ENABLE) || !defined(FPX3D_SILENT_ERROR)
#undef FPX3D_ERROR
#define FPX3D_ERROR(fmt, ...)                                                  \
  {                                                                            \
    char _fpx_lineinfo_output_buffer[sizeof(__FILE__) + 16];                   \
    FPX3D_LINE_INFO(_fpx_lineinfo_output_buffer);                              \
    fprintf(stderr,                                                            \
            "\033[0;91mFPXLIB3D ERROR:\033[0m " fmt                            \
            "\033[0m (at %s)\n" __VA_OPT__(, ) __VA_ARGS__,                    \
            _fpx_lineinfo_output_buffer);                                      \
  }
#endif // FPX3D_DEBUG_ENABLE || !FPX3D_SILENT_ERROR

#endif // FPX3D_DEBUG_H
