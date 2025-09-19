/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_COMMAND_H
#define FPX_VK_COMMAND_H

#include "../fpx3d.h"

#include "./typedefs.h"

struct _fpx3d_vk_command_pool {
  VkCommandPool pool;

  VkCommandBuffer *buffers;
  size_t bufferCount;

  Fpx3d_Vk_E_CommandPoolType type;
};

Fpx3d_E_Result fpx3d_vk_allocate_commandpools(Fpx3d_Vk_LogicalGpu *,
                                              size_t amount);
Fpx3d_E_Result fpx3d_create_commandpool_at(Fpx3d_Vk_LogicalGpu *, size_t index,
                                           Fpx3d_Vk_E_CommandPoolType type);
Fpx3d_Vk_CommandPool *fpx3d_vk_get_commandpool_at(Fpx3d_Vk_LogicalGpu *,
                                                  size_t index);
Fpx3d_E_Result fpx3d_vk_destroy_commandpool_at(Fpx3d_Vk_LogicalGpu *,
                                               size_t index);

Fpx3d_E_Result fpx3d_vk_allocate_commandbuffers_at_pool(Fpx3d_Vk_LogicalGpu *,
                                                        size_t cmd_pool_index,
                                                        size_t amount);
Fpx3d_E_Result fpx3d_vk_create_commandbuffer_at(Fpx3d_Vk_CommandPool *,
                                                size_t index,
                                                Fpx3d_Vk_LogicalGpu *);
VkCommandBuffer *fpx3d_vk_get_commandbuffer_at(Fpx3d_Vk_CommandPool *,
                                               size_t index);
// TODO: research render pass relationship to command buffer. is it always
// 1:1? so can i store them in the same struct?
Fpx3d_E_Result fpx3d_vk_record_drawing_commandbuffer(
    VkCommandBuffer *, Fpx3d_Vk_Pipeline *pipeline,
    Fpx3d_Vk_Swapchain *swapchain, size_t frame_index, Fpx3d_Vk_LogicalGpu *);
Fpx3d_E_Result fpx3d_vk_submit_commandbuffer(VkCommandBuffer *,
                                             Fpx3d_Vk_Context *,
                                             Fpx3d_Vk_LogicalGpu *,
                                             size_t frame_index,
                                             VkQueue *graphics_queue);

#endif // FPX_VK_COMMAND_H
