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
#include "vk/logical_gpu.h"
#include "vk/pipeline.h"
#include "volk/volk.h"

#include "vk/utility.h"

bool fpx3d_vk_instance_layers_supported(const char **layers,
                                        size_t layer_count) {
  if (1 > layer_count)
    return true;

  uint32_t available = 0;
  VkLayerProperties *available_layers = NULL;

  vkEnumerateInstanceLayerProperties(&available, NULL);

  if (available < layer_count)
    return false;

  available_layers =
      (VkLayerProperties *)calloc(available, sizeof(VkLayerProperties));

  if (NULL == available_layers) {
    perror("calloc()");
    FPX3D_ERROR("Error while checking for Vulkan validation layers");
    return false;
  }

  vkEnumerateInstanceLayerProperties(&available, available_layers);

  for (size_t i = 0; i < layer_count; ++i) {
    uint8_t found = false;

    for (uint32_t j = 0; j < available; ++j) {
      if (0 == strcmp(layers[i], available_layers[j].layerName)) {
        found = true;
        break;
      }
    }

    if (false == found)
      return false;
  }

  return true;
}

bool fpx3d_vk_device_extensions_supported(VkPhysicalDevice dev,
                                          const char **extensions,
                                          size_t extension_count) {
  NULL_CHECK(dev, false);
  NULL_CHECK(extensions, true);

  if (extension_count == 0)
    return true;

  uint32_t available = 0;

  vkEnumerateDeviceExtensionProperties(dev, NULL, &available, NULL);

  if (available < 1 || available < extension_count)
    return false;

  VkExtensionProperties available_extensions[available];

  vkEnumerateDeviceExtensionProperties(dev, NULL, &available,
                                       available_extensions);

  for (size_t i = 0; i < extension_count; ++i) {
    uint8_t found = false;

    for (uint32_t j = 0; j < available; ++j) {
      if (0 == strcmp(extensions[i], available_extensions[j].extensionName)) {
        found = true;
        break;
      }
    }

    if (false == found)
      return false;
  }

  return true;
}

bool fpx3d_vk_are_swapchains_supported(VkPhysicalDevice dev) {
  const char *ext_name = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

  return fpx3d_vk_device_extensions_supported(dev, &ext_name, 1);
}

Fpx3d_E_Result
fpx3d_vk_draw_frame(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu,
                    Fpx3d_Vk_Pipeline *pipelines, size_t pipeline_count,
                    VkQueue *graphics_queue, VkQueue *present_queue) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(pipelines, FPX3D_ARGS_ERROR);
  NULL_CHECK(graphics_queue, FPX3D_ARGS_ERROR);
  NULL_CHECK(present_queue, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(ctx->windowContext, FPX3D_WND_BAD_WINDOW_HANDLE_ERROR);

  uint32_t image_index = UINT32_MAX;

  vkWaitForFences(lgpu->handle, 1, &lgpu->inFlightFences[lgpu->frameCounter],
                  VK_TRUE, UINT64_MAX);

  {
    VkResult success = vkAcquireNextImageKHR(
        lgpu->handle, lgpu->currentSwapchain.swapchain, UINT64_MAX,
        lgpu->currentSwapchain.acquireSemaphore, VK_NULL_HANDLE, &image_index);

    // TODO: find another way for refreshing the window. AMD doesn't like to
    // throw ERROR_OUT_OF_DATE, but nVidia does it all the time. so rely on the
    // windowing system's resize event instead
    if (VK_ERROR_OUT_OF_DATE_KHR == success) {
      return fpx3d_vk_refresh_current_swapchain(ctx, lgpu);
    } else if (VK_SUCCESS != success && VK_SUBOPTIMAL_KHR != success) {
      FPX3D_WARN("Could not retrieve next image in swap chain");
      return FPX3D_VK_ERROR;
    }
  }

  vkResetFences(lgpu->handle, 1, &lgpu->inFlightFences[lgpu->frameCounter]);

  if (UINT32_MAX == image_index) {
    // uhhhh
    FPX3D_WARN("Failed to retrieve swapchain image index");
    return FPX3D_VK_ERROR;
  }

  for (size_t i = 0; i < pipeline_count; ++i) {

    vkResetCommandBuffer(lgpu->inFlightCommandPool.buffers[lgpu->frameCounter],
                         0);

    {
      Fpx3d_E_Result success = fpx3d_vk_record_drawing_commandbuffer(
          &lgpu->inFlightCommandPool.buffers[lgpu->frameCounter], &pipelines[i],
          &lgpu->currentSwapchain, image_index, lgpu);

      if (FPX3D_SUCCESS != success)
        return success;
    }

    if (FPX3D_SUCCESS !=
        fpx3d_vk_submit_commandbuffer(
            &lgpu->inFlightCommandPool.buffers[lgpu->frameCounter], ctx, lgpu,
            image_index, graphics_queue))
      return FPX3D_VK_ERROR;
  }

  {
    VkSemaphore temp =
        lgpu->currentSwapchain.frames[image_index].writeAvailable;

    lgpu->currentSwapchain.frames[image_index].writeAvailable =
        lgpu->currentSwapchain.acquireSemaphore;

    lgpu->currentSwapchain.acquireSemaphore = temp;
  }

  {
    Fpx3d_E_Result success = fpx3d_vk_present_swapchain_frame_at(
        &lgpu->currentSwapchain, image_index, present_queue);

    if (FPX3D_VK_FRAME_OUT_OF_DATE_ERROR == success ||
        FPX3D_VK_FRAME_SUBOPTIMAL_ERROR == success) {
      fpx3d_vk_refresh_current_swapchain(ctx, lgpu);

      return FPX3D_SUCCESS;
    } else if (FPX3D_SUCCESS != success) {
      FPX3D_WARN("Could not present image");
      return FPX3D_VK_ERROR;
    }
  }

  return FPX3D_SUCCESS;
}
