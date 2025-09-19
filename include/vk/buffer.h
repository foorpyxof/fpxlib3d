/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_BUFFER_H
#define FPX_VK_BUFFER_H

#include <stdbool.h>

#include "./typedefs.h"

struct _fpx3d_vk_buffer {
  size_t objectCount;
  size_t stride;

  VkBuffer buffer;
  VkDeviceMemory memory;

  void *mapped_memory;

  VkSharingMode sharingMode;

  bool isValid;
};

#endif // FPX_VK_BUFFER_H
