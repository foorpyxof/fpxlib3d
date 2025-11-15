/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "fpx3d.h"
#include "macros.h"

#include "vk/vertex.h"

extern Fpx3d_E_Result __fpx3d_realloc_array(void **arr_ptr, size_t obj_size,
                                            size_t amount,
                                            size_t *old_capacity);

Fpx3d_E_Result fpx3d_vk_allocate_vertices(Fpx3d_Vk_VertexBundle *bundle,
                                          size_t amount,
                                          size_t single_vertex_size) {
  if (1 > amount || 1 > single_vertex_size)
    return FPX3D_SUCCESS;

  NULL_CHECK(bundle, FPX3D_ARGS_ERROR);

  bundle->vertexDataSize = single_vertex_size;

  return __fpx3d_realloc_array((void **)&bundle->vertices, single_vertex_size,
                               amount, &bundle->vertexCapacity);
}

Fpx3d_E_Result fpx3d_vk_append_vertices(Fpx3d_Vk_VertexBundle *bundle,
                                        void *vertices, size_t amount) {
  if (1 > amount)
    return FPX3D_SUCCESS;

  NULL_CHECK(bundle, FPX3D_ARGS_ERROR);
  NULL_CHECK(vertices, FPX3D_ARGS_ERROR);

  if (1 > bundle->vertexDataSize)
    return FPX3D_SUCCESS;

  if (bundle->vertexCount + amount > bundle->vertexCapacity)
    return FPX3D_GENERIC_ERROR;

  NULL_CHECK(bundle->vertices, FPX3D_NULLPTR_ERROR);

  memcpy((uint8_t *)bundle->vertices +
             (bundle->vertexCount * bundle->vertexDataSize),
         vertices, amount * bundle->vertexDataSize);

  bundle->vertexCount += amount;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_set_indices(Fpx3d_Vk_VertexBundle *bundle,
                                    uint32_t *indices, size_t amount) {
  if (1 > amount)
    return FPX3D_SUCCESS;

  NULL_CHECK(bundle, FPX3D_ARGS_ERROR);
  NULL_CHECK(indices, FPX3D_ARGS_ERROR);

  uint32_t *ind = (uint32_t *)calloc(amount, sizeof(*indices));
  if (NULL == ind) {
    perror("calloc()");
    return FPX3D_MEMORY_ERROR;
  }

  memcpy(ind, indices, amount * sizeof(*indices));

  FREE_SAFE(bundle->indices);
  bundle->indices = ind;
  bundle->indexCount = amount;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_free_vertices(Fpx3d_Vk_VertexBundle *bundle) {
  NULL_CHECK(bundle, FPX3D_ARGS_ERROR);

  FREE_SAFE(bundle->vertices);
  FREE_SAFE(bundle->indices);

  memset(bundle, 0, sizeof(*bundle));

  return FPX3D_SUCCESS;
}
