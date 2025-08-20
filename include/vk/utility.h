/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_UTILITY_H
#define FPX_VK_UTILITY_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "fpx3d.h"

#include "vk/typedefs.h"

struct fpx3d_vulkan_pool_queue_pair {
  VkCommandPool *pool;
  VkQueue *queue;

  Fpx3d_Vk_E_QueueType type;
};

bool fpx3d_vk_instance_layers_supported(const char **layers,
                                        size_t layer_count);
bool fpx3d_vk_device_extensions_supported(VkPhysicalDevice,
                                          const char **extensions,
                                          size_t extension_count);

// TODO: make some functions to aid in finding what GPU is best.
// e.g., functions to check if certain features exist on a VkPhysicalDevice
/*
 * Helper functions for inside the `scoring_function` for picking a GPU
 */
bool fpx3d_vk_are_swapchains_supported(VkPhysicalDevice);
/*
 * End of helper functions.
 */

// draw_frame handles the aquiring of a swapchain-frame, recording and
// submitting of a command buffer, and presenting the frame
Fpx3d_E_Result fpx3d_vk_draw_frame(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *,
                                   Fpx3d_Vk_Pipeline *pipelines,
                                   size_t pipeline_count,
                                   VkQueue *graphics_queue,
                                   VkQueue *present_queue);

#endif // FPX_VK_UTILITY_H
