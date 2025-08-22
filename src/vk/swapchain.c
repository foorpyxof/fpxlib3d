/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

#include "debug.h"
#include "fpx3d.h"
#include "macros.h"
#include "vk/context.h"
#include "vk/image.h"
#include "vk/logical_gpu.h"
#include "vk/renderpass.h"
#include "vk/typedefs.h"
#include "vk/utility.h"
#include "volk/volk.h"

#include "vk/swapchain.h"

extern Fpx3d_E_Result __fpx3d_vk_new_image_view(Fpx3d_Vk_Image *,
                                                Fpx3d_Vk_LogicalGpu *,
                                                VkImageView *output);

// static declarations -------------------------------------------
static bool _surface_format_picker(VkPhysicalDevice dev, VkSurfaceKHR sfc,
                                   VkSurfaceFormatKHR *fmts, size_t fmt_count,
                                   VkSurfaceFormatKHR *output);
static bool _present_mode_picker(VkPhysicalDevice dev, VkSurfaceKHR sfc,
                                 VkPresentModeKHR *mds, size_t md_count,
                                 VkPresentModeKHR *output);
static VkExtent2D _new_window_extent(Fpx3d_Wnd_Context *wnd,
                                     VkSurfaceCapabilitiesKHR cap);
static Fpx3d_E_Result _retire_current_swapchain(Fpx3d_Vk_LogicalGpu *);
// end of static declarations ------------------------------------

Fpx3d_E_Result __fpx3d_vk_destroy_swapchain(Fpx3d_Vk_LogicalGpu *,
                                            Fpx3d_Vk_Swapchain *, bool force);

Fpx3d_E_Result
fpx3d_vk_set_required_surfaceformats(Fpx3d_Vk_SwapchainRequirements *reqs,
                                     VkSurfaceFormatKHR *formats,
                                     size_t count) {
  NULL_CHECK(reqs, FPX3D_ARGS_ERROR);
  NULL_CHECK(formats, FPX3D_ARGS_ERROR);

  if (1 > count)
    return FPX3D_SUCCESS;

  {
    reqs->surfaceFormats = formats;
    reqs->surfaceFormatsCount = count;
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result
fpx3d_vk_set_required_presentmodes(Fpx3d_Vk_SwapchainRequirements *reqs,
                                   VkPresentModeKHR *modes, size_t count) {
  NULL_CHECK(reqs, FPX3D_ARGS_ERROR);
  NULL_CHECK(modes, FPX3D_ARGS_ERROR);

  if (1 > count)
    return FPX3D_SUCCESS;

  {
    reqs->presentModes = modes;
    reqs->presentModesCount = count;
  }

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_SwapchainProperties
fpx3d_vk_create_swapchain_properties(Fpx3d_Vk_Context *ctx,
                                     VkPhysicalDevice dev,
                                     Fpx3d_Vk_SwapchainRequirements reqs) {
  Fpx3d_Vk_SwapchainProperties props = {0};

  const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  bool are_supported = fpx3d_vk_device_extensions_supported(dev, &ext, 1);

  if (false == are_supported)
    return props;

  props.surfaceFormatValid =
      _surface_format_picker(dev, ctx->vkSurface, reqs.surfaceFormats,
                             reqs.surfaceFormatsCount, &props.surfaceFormat);

  props.presentModeValid =
      _present_mode_picker(dev, ctx->vkSurface, reqs.presentModes,
                           reqs.presentModesCount, &props.presentMode);

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, ctx->vkSurface,
                                            &props.surfaceCapabilities);

  return props;
}

Fpx3d_E_Result
fpx3d_vk_create_swapchain(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu,
                          Fpx3d_Vk_SwapchainRequirements sc_reqs) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);
  NULL_CHECK(ctx->vkSurface, FPX3D_VK_BAD_VULKAN_SURFACE_ERROR);
  NULL_CHECK(ctx->windowContext, FPX3D_VK_BAD_WINDOW_CONTEXT_ERROR);

  NULL_CHECK(lgpu->graphicsQueues.queues, FPX3D_NO_CAPACITY_ERROR);
  NULL_CHECK(lgpu->presentQueues.queues, FPX3D_NO_CAPACITY_ERROR);

  Fpx3d_Vk_SwapchainProperties props =
      fpx3d_vk_create_swapchain_properties(ctx, ctx->physicalGpu, sc_reqs);

  if (0 == lgpu->graphicsQueues.count || 0 == lgpu->graphicsQueues.count)
    return FPX3D_NO_CAPACITY_ERROR;

  if (false == props.presentModeValid || false == props.surfaceFormatValid)
    return FPX3D_VK_INVALID_SWAPCHAIN_PROPERTIES_ERROR;

  const VkSurfaceCapabilitiesKHR cap = props.surfaceCapabilities;

  VkSwapchainCreateInfoKHR s_info = {0};
  VkExtent2D extent = _new_window_extent(ctx->windowContext, cap);

  uint32_t qf_indices[2] = {lgpu->graphicsQueues.queueFamilyIndex,
                            lgpu->presentQueues.queueFamilyIndex};

  {
    uint32_t image_count = cap.minImageCount + 1;
    if (cap.maxImageCount > 0 && image_count > cap.maxImageCount)
      image_count = cap.maxImageCount;

    s_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    s_info.surface = ctx->vkSurface;

    s_info.minImageCount = image_count;

    s_info.imageFormat = props.surfaceFormat.format;
    s_info.imageColorSpace = props.surfaceFormat.colorSpace;

    s_info.imageExtent = extent;
    s_info.imageArrayLayers = 1;
    s_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (lgpu->graphicsQueues.queueFamilyIndex !=
        lgpu->presentQueues.queueFamilyIndex) {
      s_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      s_info.queueFamilyIndexCount = 2;
      s_info.pQueueFamilyIndices = qf_indices;
    } else {
      s_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      s_info.queueFamilyIndexCount = 0;
      s_info.pQueueFamilyIndices = NULL;
    }

    s_info.preTransform = cap.currentTransform;
    s_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    s_info.presentMode = props.presentMode;
    s_info.clipped = VK_TRUE;

    s_info.oldSwapchain = lgpu->currentSwapchain.swapchain;
  }

  VkSwapchainKHR new_swapchain = {0};

  VkResult success =
      vkCreateSwapchainKHR(lgpu->handle, &s_info, NULL, &new_swapchain);

  if (VK_SUCCESS != success) {
    FPX3D_ERROR("Error while creating a new swapchain. Code: %d.", success);
    return FPX3D_VK_SWAPCHAIN_CREATE_ERROR;
  }

  // whatever happens here, if there was an old_swapchain, it is now
  // retired.
  if (VK_NULL_HANDLE != lgpu->currentSwapchain.swapchain)
    if (FPX3D_SUCCESS != _retire_current_swapchain(lgpu)) {
      __fpx3d_vk_destroy_swapchain(lgpu, &lgpu->currentSwapchain, true);
      return FPX3D_VK_ERROR;
    }

  uint32_t frame_count = 0;
  VkImage *images = NULL;
  vkGetSwapchainImagesKHR(lgpu->handle, new_swapchain, &frame_count, NULL);

  images = (VkImage *)calloc(frame_count, sizeof(VkImage));
  if (NULL == images) {
    perror("calloc()");

    vkDestroySwapchainKHR(lgpu->handle, new_swapchain, NULL);

    return FPX3D_MEMORY_ERROR;
  }

  VkImageView *views = (VkImageView *)calloc(frame_count, sizeof(VkImageView));

  if (NULL == views) {
    perror("calloc()");

    vkDestroySwapchainKHR(lgpu->handle, new_swapchain, NULL);

    FREE_SAFE(images);
    return FPX3D_MEMORY_ERROR;
  }

  vkGetSwapchainImagesKHR(lgpu->handle, new_swapchain, &frame_count, images);

#define DEINITIALIZE_SWAPCHAIN                                                 \
  for (size_t _iter = 0; _iter < frame_count; ++_iter) {                       \
    if (VK_NULL_HANDLE != views[_iter])                                        \
      vkDestroyImageView(lgpu->handle, views[_iter], NULL);                    \
    FREE_SAFE(images[_iter]);                                                  \
    FREE_SAFE(views[_iter]);                                                   \
    vkDestroySwapchainKHR(lgpu->handle, new_swapchain, NULL);                  \
  }

  for (uint32_t i = 0; i < frame_count; ++i) {
    Fpx3d_Vk_Image temp = {
        .image = images[i],
        .imageFormat = props.surfaceFormat.format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};

    VkImageView new_view = {0};
    FPX3D_ONFAIL(
        __fpx3d_vk_new_image_view(&temp, lgpu, &new_view), success,

        FPX3D_ERROR("Error while creating Vulkan Swapchain image views.");
        DEINITIALIZE_SWAPCHAIN; return success;);

    views[i] = new_view;
  }

  Fpx3d_Vk_SwapchainFrame *frames = (Fpx3d_Vk_SwapchainFrame *)calloc(
      frame_count, sizeof(Fpx3d_Vk_SwapchainFrame));
  if (NULL == frames) {
    perror("calloc()");

    DEINITIALIZE_SWAPCHAIN;

    return FPX3D_MEMORY_ERROR;
  }

  {
    VkSemaphoreCreateInfo sema_info = {0};
    sema_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo f_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                .flags = VK_FENCE_CREATE_SIGNALED_BIT};

    for (uint32_t i = 0; i < frame_count; ++i) {
      if (VK_SUCCESS != vkCreateSemaphore(lgpu->handle, &sema_info, NULL,
                                          &frames[i].writeAvailable) ||
          VK_SUCCESS != vkCreateSemaphore(lgpu->handle, &sema_info, NULL,
                                          &frames[i].renderFinished)) {

        if (VK_NULL_HANDLE == frames[i].writeAvailable)
          vkDestroySemaphore(lgpu->handle, frames[i].writeAvailable, NULL);

        if (VK_NULL_HANDLE == frames[i].renderFinished)
          vkDestroySemaphore(lgpu->handle, frames[i].renderFinished, NULL);

        FREE_SAFE(frames);

        DEINITIALIZE_SWAPCHAIN;

        return FPX3D_VK_ERROR;
      }

      vkCreateFence(lgpu->handle, &f_info, NULL, &frames[i].idleFence);

      frames[i].image = images[i];
      frames[i].view = views[i];
    }

    VkSemaphore sema = VK_NULL_HANDLE;

    if (VK_NULL_HANDLE == lgpu->currentSwapchain.acquireSemaphore) {
      if (VK_SUCCESS !=
          vkCreateSemaphore(lgpu->handle, &sema_info, NULL, &sema)) {
        FREE_SAFE(frames);

        DEINITIALIZE_SWAPCHAIN;

        return FPX3D_VK_ERROR;
      }

      lgpu->currentSwapchain.acquireSemaphore = sema;
    }
  }

#undef DEINITIALIZE_SWAPCHAIN

  lgpu->currentSwapchain.requirements = sc_reqs;
  lgpu->currentSwapchain.properties = props;
  lgpu->currentSwapchain.swapchain = new_swapchain;
  lgpu->currentSwapchain.swapchainExtent = extent;
  lgpu->currentSwapchain.frames = frames;
  lgpu->currentSwapchain.frameCount = frame_count;

  // TODO: depth
  // Fpx3d_Vk_ImageDimensions depth_dimensions = {.width = extent.width,
  //                                              .height = extent.height};
  // lgpu->currentSwapchain.depthImage =
  //     fpx3d_vk_create_depth_image(ctx, lgpu, depth_dimensions);

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_Swapchain *fpx3d_vk_get_current_swapchain(Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(lgpu, NULL);

  return &lgpu->currentSwapchain;
}

Fpx3d_E_Result fpx3d_vk_destroy_current_swapchain(Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  return __fpx3d_vk_destroy_swapchain(lgpu, &lgpu->currentSwapchain, false);
}

Fpx3d_E_Result fpx3d_vk_refresh_current_swapchain(Fpx3d_Vk_Context *ctx,
                                                  Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx->windowContext, FPX3D_VK_BAD_WINDOW_CONTEXT_ERROR);
  NULL_CHECK(ctx->windowContext->glfwWindow, FPX3D_WND_BAD_WINDOW_HANDLE_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE == lgpu->currentSwapchain.swapchain)
    return FPX3D_VK_SWAPCHAIN_INVALID_ERROR;

  int width = 0, height = 0;
  glfwGetFramebufferSize(ctx->windowContext->glfwWindow, &width, &height);

  while (0 == width || 0 == height) {
    glfwGetFramebufferSize(ctx->windowContext->glfwWindow, &width, &height);
    glfwWaitEvents();
  }

  vkDeviceWaitIdle(lgpu->handle);

  Fpx3d_Vk_Swapchain *old_chain = fpx3d_vk_get_current_swapchain(lgpu);
  Fpx3d_Vk_RenderPass *old_pass = old_chain->renderPassReference;

  fpx3d_vk_create_swapchain(ctx, lgpu, lgpu->currentSwapchain.requirements);
  Fpx3d_Vk_Swapchain *new_chain = fpx3d_vk_get_current_swapchain(lgpu);

  VkExtent2D h_w = _new_window_extent(
      ctx->windowContext, new_chain->properties.surfaceCapabilities);
  Fpx3d_Vk_ImageDimensions depth_dimensions = {.width = h_w.width,
                                               .height = h_w.height};

  fpx3d_vk_create_depth_image(ctx, lgpu, depth_dimensions);
  fpx3d_vk_create_framebuffers(new_chain, ctx, lgpu, old_pass);

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_set_swapchain_depth_image(Fpx3d_Vk_Swapchain *sc,
                                                  Fpx3d_Vk_Image image) {
  NULL_CHECK(sc, FPX3D_ARGS_ERROR);
  if (false == image.isValid)
    return FPX3D_ARGS_ERROR;

  if (sc->swapchainExtent.width != image.dimensions.width ||
      sc->swapchainExtent.height != image.dimensions.height)
    return FPX3D_ARGS_ERROR;

  sc->depthImage = image;

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_SwapchainFrame *fpx3d_vk_get_swapchain_frame_at(Fpx3d_Vk_Swapchain *sc,
                                                         size_t index) {
  NULL_CHECK(sc, NULL);
  NULL_CHECK(sc->frames, NULL);

  if (sc->frameCount <= index)
    return NULL;

  return &sc->frames[index];
}

Fpx3d_E_Result fpx3d_vk_present_swapchain_frame_at(Fpx3d_Vk_Swapchain *sc,
                                                   size_t index,
                                                   VkQueue *present_queue) {
  NULL_CHECK(sc, FPX3D_ARGS_ERROR);
  NULL_CHECK(present_queue, FPX3D_ARGS_ERROR);

  NULL_CHECK(sc->frames, FPX3D_VK_NULLPTR_ERROR);
  NULL_CHECK(sc->swapchain, FPX3D_VK_SWAPCHAIN_INVALID_ERROR);

  if (sc->frameCount <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  uint32_t idx = (uint32_t)index;

  VkPresentInfoKHR p_info = {0};
  p_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  p_info.waitSemaphoreCount = 1;
  p_info.pWaitSemaphores = &sc->frames[index].renderFinished;

  p_info.swapchainCount = 1;
  p_info.pSwapchains = &sc->swapchain;
  p_info.pImageIndices = &idx;

  p_info.pResults = NULL;

  {
    VkResult success = vkQueuePresentKHR(*present_queue, &p_info);

    if (VK_ERROR_OUT_OF_DATE_KHR == success) {
      return FPX3D_VK_FRAME_OUT_OF_DATE_ERROR;
    } else if (VK_SUBOPTIMAL_KHR == success) {
      return FPX3D_VK_FRAME_SUBOPTIMAL_ERROR;
    }
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_create_framebuffers(Fpx3d_Vk_Swapchain *sc,
                                            Fpx3d_Vk_Context *ctx,
                                            Fpx3d_Vk_LogicalGpu *lgpu,
                                            Fpx3d_Vk_RenderPass *render_pass) {
  NULL_CHECK(sc, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(render_pass, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(sc->frames, FPX3D_VK_NULLPTR_ERROR);

  VkImageView attachments[2] = {0};

  if (render_pass->depth) {
    Fpx3d_Vk_ImageDimensions dims = {.height = sc->swapchainExtent.height,
                                     .width = sc->swapchainExtent.width};

    sc->depthImage = fpx3d_vk_create_depth_image(ctx, lgpu, dims);

    if (false == sc->depthImage.isValid) {
      return FPX3D_VK_ERROR;
    }

    attachments[1] = sc->depthImage.imageView;
  }

  for (size_t i = 0; i < sc->frameCount; ++i) {
    VkFramebuffer new_fb = VK_NULL_HANDLE;

    attachments[0] = sc->frames[i].view;

    VkFramebufferCreateInfo fb_info = {0};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = render_pass->handle;
    fb_info.attachmentCount = CONDITIONAL(render_pass->depth, 2, 1);
    fb_info.pAttachments = attachments;
    fb_info.width = sc->swapchainExtent.width;
    fb_info.height = sc->swapchainExtent.height;
    fb_info.layers = 1;

    {
      if (VK_SUCCESS !=
          vkCreateFramebuffer(lgpu->handle, &fb_info, NULL, &new_fb))
        return FPX3D_VK_ERROR;
    }

    sc->frames[i].framebuffer = new_fb;
  }

  sc->renderPassReference = render_pass;

  return FPX3D_SUCCESS;
}

// STATIC FUNCTIONS -------------------------------------------------
static bool _surface_format_picker(VkPhysicalDevice dev, VkSurfaceKHR sfc,
                                   VkSurfaceFormatKHR *fmts, size_t fmt_count,
                                   VkSurfaceFormatKHR *output) {
  uint32_t formats_available = 0;
  VkSurfaceFormatKHR *formats = NULL;

  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, sfc, &formats_available, NULL);

  if (0 == formats_available)
    return false;

  formats = (VkSurfaceFormatKHR *)calloc(formats_available,
                                         sizeof(VkSurfaceFormatKHR));

  if (NULL == formats) {
    perror("calloc()");
    return false;
  }

  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, sfc, &formats_available, formats);

  if (0 == fmt_count) {
    *output = formats[0];
    FREE_SAFE(formats);
    return true;
  }

  for (size_t i = 0; i < fmt_count; ++i) {
    for (uint32_t j = 0; j < formats_available; ++j) {
      if (fmts[i].format == formats[j].format &&
          fmts[i].colorSpace == formats[j].colorSpace) {
        FREE_SAFE(formats);
        *output = fmts[i];
        return true;
      }
    }
  }

  FREE_SAFE(formats);
  return false;
}

static bool _present_mode_picker(VkPhysicalDevice dev, VkSurfaceKHR sfc,
                                 VkPresentModeKHR *mds, size_t md_count,
                                 VkPresentModeKHR *output) {
  uint32_t modes_available = 0;
  VkPresentModeKHR *modes = NULL;

  vkGetPhysicalDeviceSurfacePresentModesKHR(dev, sfc, &modes_available, NULL);

  if (0 == modes_available)
    return false;

  modes = (VkPresentModeKHR *)calloc(modes_available, sizeof(VkPresentModeKHR));

  if (NULL == modes) {
    perror("calloc()");
    return false;
  }

  vkGetPhysicalDeviceSurfacePresentModesKHR(dev, sfc, &modes_available, modes);

  if (0 == md_count) {
    *output = modes[0];
    FREE_SAFE(modes);
    return true;
  }

  for (size_t i = 0; i < md_count; ++i) {
    for (uint32_t j = 0; j < modes_available; ++j) {
      if (mds[i] == modes[j]) {
        FREE_SAFE(modes);
        *output = mds[i];
        return true;
      }
    }
  }

  FREE_SAFE(modes);
  return false;
}

static VkExtent2D _new_window_extent(Fpx3d_Wnd_Context *wnd,
                                     VkSurfaceCapabilitiesKHR cap) {
  VkExtent2D retval = {0};
  NULL_CHECK(wnd, retval);

  if (cap.currentExtent.width != UINT32_MAX)
    return cap.currentExtent;

  int width, height;

  glfwGetFramebufferSize(wnd->glfwWindow, &width, &height);

  retval.height = CLAMP((uint32_t)height, cap.minImageExtent.height,
                        cap.maxImageExtent.height);
  retval.width = CLAMP((uint32_t)width, cap.minImageExtent.width,
                       cap.maxImageExtent.width);

  return retval;
}

static Fpx3d_E_Result _retire_current_swapchain(Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);

  Fpx3d_Vk_Swapchain **the_pointer = NULL;

  if (NULL == lgpu->oldSwapchainsList) {
    the_pointer = &lgpu->oldSwapchainsList;
  } else if (NULL == lgpu->newestOldSwapchain) {
    for (the_pointer = &lgpu->oldSwapchainsList[0].nextInList;
         NULL != *the_pointer; the_pointer = &(*the_pointer)->nextInList)
      ;
  } else {
    the_pointer = &lgpu->newestOldSwapchain->nextInList;
  }

  *the_pointer = (Fpx3d_Vk_Swapchain *)calloc(1, sizeof(Fpx3d_Vk_Swapchain));

  if (NULL == *the_pointer) {
    perror("calloc()");
    return FPX3D_MEMORY_ERROR;
  }

  memcpy(*the_pointer, &lgpu->currentSwapchain, sizeof(Fpx3d_Vk_Swapchain));
  memset(&lgpu->currentSwapchain, 0, sizeof(lgpu->currentSwapchain));

  lgpu->newestOldSwapchain = *the_pointer;
  (*the_pointer)->nextInList = NULL;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result __fpx3d_vk_destroy_swapchain(Fpx3d_Vk_LogicalGpu *lgpu,
                                            Fpx3d_Vk_Swapchain *sc,
                                            bool force) {
  NULL_CHECK(sc, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (false == force)
    for (size_t i = 0; i < sc->frameCount; ++i) {
      // check states of fences on the frames:
      // if all signaled, all good.
      // Otherwise if force == false, return FPX3D_RESOURCE_BUSY_ERROR;
      if (VK_SUCCESS != vkGetFenceStatus(lgpu->handle, sc->frames[i].idleFence))
        return FPX3D_RESOURCE_BUSY_ERROR;
    }

  for (size_t i = 0; i < sc->frameCount; ++i) {
    if (VK_NULL_HANDLE != sc->frames[i].idleFence)
      vkDestroyFence(lgpu->handle, sc->frames[i].idleFence, NULL);
  }

  for (size_t i = 0; i < sc->frameCount; ++i) {
    if (VK_NULL_HANDLE != sc->frames[i].framebuffer)
      vkDestroyFramebuffer(lgpu->handle, sc->frames[i].framebuffer, NULL);

    if (VK_NULL_HANDLE != sc->frames[i].view)
      vkDestroyImageView(lgpu->handle, sc->frames[i].view, NULL);

    if (VK_NULL_HANDLE != sc->frames[i].writeAvailable)
      vkDestroySemaphore(lgpu->handle, sc->frames[i].writeAvailable, NULL);

    if (VK_NULL_HANDLE != sc->frames[i].renderFinished)
      vkDestroySemaphore(lgpu->handle, sc->frames[i].renderFinished, NULL);
  }
  FREE_SAFE(sc->frames);

  fpx3d_vk_destroy_image(&sc->depthImage, lgpu);

  vkDestroySemaphore(lgpu->handle, sc->acquireSemaphore, NULL);

  if (VK_NULL_HANDLE != sc->swapchain)
    vkDestroySwapchainKHR(lgpu->handle, sc->swapchain, NULL);

  fpx3d_vk_destroy_image(&sc->depthImage, lgpu);

  memset(sc, 0, sizeof(*sc));

  return FPX3D_SUCCESS;
}
// END OF STATIC FUNCTIONS ------------------------------------------
