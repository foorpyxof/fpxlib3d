/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include "macros.h"
#include "vk/logical_gpu.h"
#include "vk/typedefs.h"

#include "vk/queues.h"

extern struct fpx3d_vulkan_queues *
__fpx3d_vk_get_queues_ptr_by_type(Fpx3d_Vk_LogicalGpu *, Fpx3d_Vk_E_QueueType);

Fpx3d_E_Result
__fpx3d_vk_blacklist_queuefamily_index(Fpx3d_Vk_QueueFamilyRequirements *,
                                       size_t index);

Fpx3d_E_Result
__fpx3d_vk_blacklist_queuefamily_index(Fpx3d_Vk_QueueFamilyRequirements *reqs,
                                       size_t index) {
  NULL_CHECK(reqs, FPX3D_ARGS_ERROR);

  if (63 < index)
    return FPX3D_ARGS_ERROR;

  reqs->indexBlacklistBits |= (1 << index);

  return FPX3D_SUCCESS;
}

VkQueue *fpx3d_vk_get_queue_at(Fpx3d_Vk_LogicalGpu *lgpu, size_t index,
                               Fpx3d_Vk_E_QueueType q_type) {
  NULL_CHECK(lgpu, NULL);
  NULL_CHECK(lgpu->handle, NULL);

  struct fpx3d_vulkan_queues *queues =
      __fpx3d_vk_get_queues_ptr_by_type(lgpu, q_type);

  NULL_CHECK(queues, NULL);

  if (queues->count <= index)
    return NULL;

  return &queues->queues[index];
}
