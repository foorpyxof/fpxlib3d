/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_WINDOW_H
#define FPX_WINDOW_H

#include "fpx3d.h"
#include "macros.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct _fpx3d_wnd_context Fpx3d_Wnd_Context;
typedef struct fpx3d_wnd_dimensions (*Fpx3d_Wnd_WindowSizeCallback)(
    void *window);

struct fpx3d_wnd_dimensions {
  uint16_t width, height;
};

struct _fpx3d_wnd_context {
  Fpx3d_Wnd_WindowSizeCallback sizeCallback;
  void *pointer;
};

void fpx3d_wnd_set_size_callback(Fpx3d_Wnd_Context *,
                                 Fpx3d_Wnd_WindowSizeCallback callback);

void fpx3d_wnd_set_window_pointer(Fpx3d_Wnd_Context *, void *window);

#endif // FPX_WINDOW_H
