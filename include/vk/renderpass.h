/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_RENDERPASS_H
#define FPX_VK_RENDERPASS_H

#include "fpx3d.h"

#include "typedefs.h"

struct _fpx3d_vk_render_pass {
  VkRenderPass handle;
};

Fpx3d_E_Result fpx3d_vk_allocate_renderpasses(Fpx3d_Vk_LogicalGpu *,
                                              size_t count);
Fpx3d_E_Result fpx3d_vk_create_renderpass_at(Fpx3d_Vk_LogicalGpu *,
                                             size_t index, Fpx3d_Vk_Context *);
Fpx3d_Vk_RenderPass *fpx3d_vk_get_renderpass_at(Fpx3d_Vk_LogicalGpu *,
                                                size_t index);
Fpx3d_E_Result fpx3d_vk_destroy_renderpass_at(Fpx3d_Vk_LogicalGpu *,
                                              size_t index);

#endif // FPX_VK_RENDERPASS_H
