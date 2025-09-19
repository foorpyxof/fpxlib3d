/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_CONTEXT_H
#define FPX_VK_CONTEXT_H

#include "../fpx3d.h"
#include "../window/window.h"

#include "./typedefs.h"

struct _fpx3d_vk_context {
  Fpx3d_Wnd_Context *windowContext;

  // this pointer will be passed into callbacks
  void *customPointer;

  VkPhysicalDevice physicalGpu;

  Fpx3d_Vk_LogicalGpu *logicalGpus;
  size_t logicalGpuCapacity;

  const char **lgpuExtensions;
  size_t lgpuExtensionCount;

  const char **instanceLayers;
  size_t instanceLayerCount;

  const char **instanceExtensions;
  size_t instanceExtensionCount;

  VkInstance vkInstance;
  VkSurfaceKHR vkSurface;

  VkApplicationInfo appInfo;

  struct {
    size_t maxFramesInFlight;
    size_t bufferAlignment;
  } constants;
};

Fpx3d_E_Result fpx3d_vk_init_context(Fpx3d_Vk_Context *, Fpx3d_Wnd_Context *);

// don't pass VK_LAYER_KHRONOS_validation in the context. this is automatically
// enabled in the `debug` build of the library, and enabling it in your call to
// create a window here will only cause issues. Fix pending
Fpx3d_E_Result fpx3d_vk_create_instance(Fpx3d_Vk_Context *);
Fpx3d_E_Result fpx3d_vk_destroy_instance(Fpx3d_Vk_Context *,
                                         void (*destruction_callback)(void *));
Fpx3d_Wnd_Context *fpx3d_vk_get_windowcontext(Fpx3d_Vk_Context *);

Fpx3d_E_Result fpx3d_vk_set_custom_pointer(Fpx3d_Vk_Context *, void *);
void *fpx3d_vk_get_custom_pointer(Fpx3d_Vk_Context *);

Fpx3d_E_Result fpx3d_vk_select_gpu(Fpx3d_Vk_Context *,
                                   int (*scoring_function)(Fpx3d_Vk_Context *,
                                                           VkPhysicalDevice));

#endif // FPX_VK_CONTEXT_H
