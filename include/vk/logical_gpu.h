/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_LOGICAL_GPU_H
#define FPX_VK_LOGICAL_GPU_H

#include "fpx3d.h"
#include "window.h"

#include "vk/command.h"
#include "vk/queues.h"
#include "vk/swapchain.h"
#include "vk/typedefs.h"

struct _fpx3d_vk_lgpu {
  VkDevice handle;
  VkPhysicalDeviceFeatures features;

  Fpx3d_Vk_Swapchain currentSwapchain;

  Fpx3d_Vk_Swapchain *oldSwapchainsList;
  Fpx3d_Vk_Swapchain *newestOldSwapchain;

  Fpx3d_Vk_CommandPool *commandPools;
  size_t commandPoolCapacity;

  Fpx3d_Vk_Pipeline *pipelines;
  size_t pipelineCapacity;

  Fpx3d_Vk_RenderPass *renderPasses;
  size_t renderPassCapacity;

  struct fpx3d_vulkan_queues graphicsQueues;
  struct fpx3d_vulkan_queues presentQueues;
  struct fpx3d_vulkan_queues transferQueues;

  size_t queueFamilyCount;

  // don't alter the in-flight metadata in your own usage of this library,
  // unless you understand how to use it to your advantage. Check the
  // implementation in `vk.c` for details on how many in-flight fences and
  // command-buffers are already allocated
  //
  // also:
  // https://vulkan-tutorial.com/Drawing_a_triangle/Drawing/Frames_in_flight
  Fpx3d_Vk_CommandPool inFlightCommandPool;
  VkFence *inFlightFences;

  uint16_t frameCounter;
};

Fpx3d_E_Result fpx3d_vk_allocate_logicalgpus(Fpx3d_Vk_Context *, size_t amount);
Fpx3d_E_Result fpx3d_vk_create_logicalgpu_at(Fpx3d_Vk_Context *, size_t index,
                                             VkPhysicalDeviceFeatures,
                                             size_t graphics_queues,
                                             size_t present_queues,
                                             size_t transfer_queues);
Fpx3d_Vk_LogicalGpu *fpx3d_vk_get_logicalgpu_at(Fpx3d_Vk_Context *,
                                                size_t index);
Fpx3d_E_Result fpx3d_vk_destroy_logicalgpu_at(Fpx3d_Vk_Context *, size_t index);

#endif // FPX_VK_LOGICAL_GPU_H
