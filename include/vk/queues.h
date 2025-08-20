/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_QUEUES_H
#define FPX_VK_QUEUES_H

#include <sys/types.h>

#include "fpx3d.h"

#include "typedefs.h"

struct _fpx3d_vk_qf {
  ssize_t qfIndex;
  size_t firstQueueIndex;
  VkQueueFamilyProperties properties;
  Fpx3d_Vk_E_QueueType type;
  bool isValid;
};

struct _fpx3d_vk_qf_req {
  union {
    struct {
      uint32_t requiredFlags;
    } graphics;

    struct {
      VkSurfaceKHR surface;
      VkPhysicalDevice gpu;
    } present;
  };

  size_t minimumQueues;

  // supports up to 64 qf indices. should be fine
  uint64_t indexBlacklistBits;

  Fpx3d_Vk_E_QueueType type;
};

struct fpx3d_vulkan_queues {
  VkQueue *queues;
  size_t count;
  size_t nextToUse;

  size_t offsetInFamily;
  int queueFamilyIndex;
};

struct fpx3d_vk_qf_holder {
  Fpx3d_Vk_QueueFamily g_family, p_family, t_family;
};

// index <= 63
Fpx3d_E_Result
__fpx3d_vk_blacklist_queuefamily_index(Fpx3d_Vk_QueueFamilyRequirements *,
                                       size_t index);

VkQueue *fpx3d_vk_get_queue_at(Fpx3d_Vk_LogicalGpu *, size_t index,
                               Fpx3d_Vk_E_QueueType);

#endif // FPX_VK_QUEUES_H
