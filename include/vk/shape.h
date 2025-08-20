/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_SHAPE_H
#define FPX_VK_SHAPE_H

#include <stdbool.h>

#include "fpx3d.h"

#include "vk/buffer.h"
#include "vk/typedefs.h"

struct _fpx3d_vk_shapebuffer {
  Fpx3d_Vk_Buffer vertexBuffer;

  // if `isValid` bool within the indexBuffer is set to `false`, we assume we
  // want to use the vertices as-is, instead of ordering them using an index
  // buffer
  Fpx3d_Vk_Buffer indexBuffer;
}; // added to the Pipeline struct after that Pipeline has
   // already been created

struct _fpx3d_vk_shape {
  Fpx3d_Vk_ShapeBuffer *shapeBuffer;

  struct {
    Fpx3d_Vk_DescriptorSet *inFlightDescriptorSets;
    void *rawBufferData;
  } bindings;

  bool isValid;
};

Fpx3d_E_Result fpx3d_vk_create_shapebuffer(Fpx3d_Vk_Context *,
                                           Fpx3d_Vk_LogicalGpu *,
                                           Fpx3d_Vk_VertexBundle *,
                                           Fpx3d_Vk_ShapeBuffer *output);
Fpx3d_E_Result fpx3d_vk_destroy_shapebuffer(Fpx3d_Vk_LogicalGpu *,
                                            Fpx3d_Vk_ShapeBuffer *);

Fpx3d_Vk_Shape fpx3d_vk_create_shape(Fpx3d_Vk_ShapeBuffer *buffer);
Fpx3d_E_Result fpx3d_vk_destroy_shape(Fpx3d_Vk_Shape *, Fpx3d_Vk_Context *,
                                      Fpx3d_Vk_LogicalGpu *);

Fpx3d_Vk_Shape fpx3d_vk_duplicate_shape(Fpx3d_Vk_Shape *, Fpx3d_Vk_Context *,
                                        Fpx3d_Vk_LogicalGpu *);

#endif // FPX_VK_SHAPE_H
