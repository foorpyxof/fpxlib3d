/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpx3d.h"

Fpx3d_E_Result __fpx3d_realloc_array(void **arr, size_t obj_size, size_t amount,
                                     size_t *old_capacity) {
  if (1 > amount)
    return FPX3D_ARGS_ERROR;

  void *data = realloc(*arr, obj_size * amount);
  if (NULL == data) {
    perror("realloc()");
    return FPX3D_MEMORY_ERROR;
  }

  if (amount > *old_capacity) {
    memset((uint8_t *)data + *old_capacity, 0,
           (amount - *old_capacity) * obj_size);
  }

  *arr = data;
  *old_capacity = amount;

  return FPX3D_SUCCESS;
}
