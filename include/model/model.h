/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX3D_MODEL_MODEL_H
#define FPX3D_MODEL_MODEL_H

#include <sys/types.h>

#include "../fpx3d.h"
#include "typedefs.h"

#include "../../modules/cglm/include/cglm/types.h"

struct _fpx3d_model_model {
  size_t vertexCount;
  Fpx3d_Model_Vertex *vertices;

  size_t indexCount;
  size_t *indices;
};

struct _fpx3d_model_vertex {
  vec3 position;
  vec3 color;
  vec2 textureCoordinate;
};

Fpx3d_E_Result fpx3d_set_vertex_position(Fpx3d_Model_Vertex *, vec3 pos);
Fpx3d_E_Result fpx3d_set_vertex_color(Fpx3d_Model_Vertex *, vec3 color);

#endif // FPX3D_MODEL_MODEL_H
