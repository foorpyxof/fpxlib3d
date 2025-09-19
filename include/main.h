/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_MAIN_H
#define FPX_MAIN_H

#include "./vk.h"

typedef struct cube {
  Fpx3d_Vk_Shape sides[6];
} cube;

struct model_descriptor {
  mat4 model;
};
struct vp_descriptor {
  mat4 view;
  mat4 projection;
};

#endif // FPX_MAIN_H
