/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "fpx3d.h"
#include "macros.h"

#include "model/model.h"

// vecX is an array of X floats
Fpx3d_E_Result fpx3d_set_vertex_position(Fpx3d_Model_Vertex *vert, vec3 pos) {
  NULL_CHECK(vert, FPX3D_ARGS_ERROR);
  NULL_CHECK(pos, FPX3D_ARGS_ERROR);

  memcpy(vert->position, pos, sizeof(vec3));

  return FPX3D_SUCCESS;
}

// vecX is an array of X floats
Fpx3d_E_Result fpx3d_set_vertex_color(Fpx3d_Model_Vertex *vert, vec3 color) {
  NULL_CHECK(vert, FPX3D_ARGS_ERROR);
  NULL_CHECK(color, FPX3D_ARGS_ERROR);

  memcpy(vert->color, color, sizeof(vec3));

  return FPX3D_SUCCESS;
}
