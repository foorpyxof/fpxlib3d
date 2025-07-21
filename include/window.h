/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_WINDOW_H
#define FPX_WINDOW_H

#include <GLFW/glfw3.h>
#include <stdbool.h>

typedef struct _fpx3d_wnd_context Fpx3d_Wnd_Context;

struct _fpx3d_wnd_context {
  GLFWwindow *glfwWindow;

  uint16_t windowDimensions[2];
  const char *windowTitle;

  bool resized;
};

#endif // FPX_WINDOW_H
