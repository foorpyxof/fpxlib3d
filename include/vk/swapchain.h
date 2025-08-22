/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_SWAPCHAIN_H
#define FPX_VK_SWAPCHAIN_H

#include "fpx3d.h"

#include "vk/image.h"
#include "vk/typedefs.h"

struct _fpx3d_vk_sc_frame {
  VkImage image;
  VkImageView view;
  VkFramebuffer framebuffer;

  VkSemaphore writeAvailable;
  VkSemaphore renderFinished;

  VkFence idleFence;
};

struct _fpx3d_vk_sc_prop {
  VkSurfaceCapabilitiesKHR surfaceCapabilities;

  bool surfaceFormatValid;
  bool presentModeValid;

  VkSurfaceFormatKHR surfaceFormat;
  VkPresentModeKHR presentMode;
};

struct _fpx3d_vk_sc_req {
  VkSurfaceFormatKHR *surfaceFormats;
  size_t surfaceFormatsCount;

  VkPresentModeKHR *presentModes;
  size_t presentModesCount;
};

struct _fpx3d_vk_sc {
  VkSwapchainKHR swapchain;
  VkExtent2D swapchainExtent;

  Fpx3d_Vk_SwapchainRequirements requirements;
  Fpx3d_Vk_SwapchainProperties properties;

  VkSemaphore acquireSemaphore;

  Fpx3d_Vk_RenderPass *renderPassReference;

  Fpx3d_Vk_SwapchainFrame *frames;
  size_t frameCount;

  Fpx3d_Vk_Image depthImage;

  Fpx3d_Vk_Swapchain *nextInList;
};

Fpx3d_E_Result
fpx3d_vk_set_required_surfaceformats(Fpx3d_Vk_SwapchainRequirements *,
                                     VkSurfaceFormatKHR *formats, size_t count);
Fpx3d_E_Result
fpx3d_vk_set_required_presentmodes(Fpx3d_Vk_SwapchainRequirements *,
                                   VkPresentModeKHR *modes, size_t count);

Fpx3d_Vk_SwapchainProperties
fpx3d_vk_create_swapchain_properties(Fpx3d_Vk_Context *ctx,
                                     VkPhysicalDevice dev,
                                     Fpx3d_Vk_SwapchainRequirements reqs);

Fpx3d_E_Result fpx3d_vk_create_swapchain(Fpx3d_Vk_Context *,
                                         Fpx3d_Vk_LogicalGpu *,
                                         Fpx3d_Vk_SwapchainRequirements);
Fpx3d_Vk_Swapchain *fpx3d_vk_get_current_swapchain(Fpx3d_Vk_LogicalGpu *);
Fpx3d_E_Result fpx3d_vk_destroy_current_swapchain(Fpx3d_Vk_LogicalGpu *);
Fpx3d_E_Result fpx3d_vk_refresh_current_swapchain(Fpx3d_Vk_Context *,
                                                  Fpx3d_Vk_LogicalGpu *);

Fpx3d_E_Result fpx3d_vk_set_swapchain_depth_image(Fpx3d_Vk_Swapchain *,
                                                  Fpx3d_Vk_Image);

// TODO: also create() and destroy()? only if necessary tho
Fpx3d_Vk_SwapchainFrame *fpx3d_vk_get_swapchain_frame_at(Fpx3d_Vk_Swapchain *,
                                                         size_t index);
Fpx3d_E_Result fpx3d_vk_present_swapchain_frame_at(Fpx3d_Vk_Swapchain *,
                                                   size_t index,
                                                   VkQueue *present_queue);

// TODO: also create() and destroy()? only if necessary tho
Fpx3d_E_Result fpx3d_vk_create_framebuffers(Fpx3d_Vk_Swapchain *,
                                            Fpx3d_Vk_Context *,
                                            Fpx3d_Vk_LogicalGpu *,
                                            Fpx3d_Vk_RenderPass *render_pass);

#endif // FPX_VK_SWAPCHAIN_H
