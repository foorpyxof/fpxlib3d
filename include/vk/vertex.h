/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_VERTEX_H
#define FPX_VK_VERTEX_H

#include <stdint.h>
#include <sys/types.h>

#include "../fpx3d.h"
#include "../model/model.h"

#include "./typedefs.h"

#include "../../modules/cglm/include/cglm/types.h"

struct _fpx3d_vk_vertex_bundle {
  // size of the Vertex data in bytes (for a single vertex).
  // If you use a struct, just do sizeof([your struct])
  size_t vertexDataSize;

  void *vertices;
  size_t vertexCount;
  size_t vertexCapacity;

  uint32_t *indices;
  size_t indexCount; // if 0, use vertices as is
};

struct _fpx3d_vk_vertex_binding {
  Fpx3d_Vk_VertexAttribute *attributes;
  size_t attributeCount;

  size_t sizePerVertex;
};

// these will be used in pipeline creation
struct _fpx3d_vk_vertex_attr {
  enum {
    FPX3D_VK_FORMAT_INVALID = 0,

    VEC2_16BIT_SFLOAT = 1,
    VEC3_16BIT_SFLOAT = 2,
    VEC4_16BIT_SFLOAT = 3,

    VEC2_32BIT_SFLOAT = 4,
    VEC3_32BIT_SFLOAT = 5,
    VEC4_32BIT_SFLOAT = 6,

    VEC2_64BIT_SFLOAT = 7,
    VEC3_64BIT_SFLOAT = 8,
    VEC4_64BIT_SFLOAT = 9,

    FPX3D_VK_FORMAT_MAXVALUE,
  } format;

  size_t dataOffsetBytes;
};

// will *only* zero-initialize if this is an initial allocation,
// not when reallocating using this function
Fpx3d_E_Result fpx3d_vk_allocate_vertices(Fpx3d_Vk_VertexBundle *,
                                          size_t amount,
                                          size_t single_vertex_size);
Fpx3d_E_Result fpx3d_vk_append_vertices(Fpx3d_Vk_VertexBundle *, void *vertices,
                                        size_t amount);
Fpx3d_E_Result fpx3d_vk_set_indices(Fpx3d_Vk_VertexBundle *, uint32_t *indices,
                                    size_t amount);

// also frees indices, if these were allocated
Fpx3d_E_Result fpx3d_vk_free_vertices(Fpx3d_Vk_VertexBundle *);

#endif // FPX_VK_VERTEX_H
