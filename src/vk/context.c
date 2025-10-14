/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "fpx3d.h"
#include "macros.h"
#include "vk/logical_gpu.h"
#include "vk/typedefs.h"
#include "vk/utility.h"

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif // VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"

#define VOLK_IMPLEMENTATION
#include "volk/volk.h"

#include "vk/context.h"

extern void __fpx3d_vk_destroy_lgpu(Fpx3d_Vk_Context *, Fpx3d_Vk_LogicalGpu *);

Fpx3d_E_Result fpx3d_vk_init_context(Fpx3d_Vk_Context *ctx,
                                     Fpx3d_Wnd_Context *wnd) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  VkResult volk_found = volkInitialize();
  if (VK_SUCCESS != volk_found)
    return FPX3D_VK_ERROR;

  ctx->windowContext = wnd;

  // user can change this
  ctx->constants.maxFramesInFlight = 1;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_create_instance(Fpx3d_Vk_Context *ctx) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  Fpx3d_Wnd_Context *wnd = ctx->windowContext;

  NULL_CHECK(wnd, FPX3D_WND_INVALID_DETAILS_ERROR);

  if (VK_STRUCTURE_TYPE_APPLICATION_INFO != ctx->appInfo.sType)
    return FPX3D_VK_APPINFO_ERROR;

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  const char *val_layer_ext = "VK_LAYER_KHRONOS_validation";

  bool vl_supported = true;

  if (false == fpx3d_vk_instance_layers_supported(&val_layer_ext, 1)) {

    FPX3D_WARN("Validation layers are not available. Proceeding without. "
               "Have you installed the SDK?");

    vl_supported = false;
  }
#endif

  // we check for the queried instance layers, if need be.
  // if no layers are set to be queried, we just keep going

  if (NULL != ctx->instanceLayers && 0 < ctx->instanceLayerCount) {
    if (false == fpx3d_vk_instance_layers_supported(ctx->instanceLayers,
                                                    ctx->instanceLayerCount)) {
      FPX3D_WARN("Requested instance layers not available. Aborting "
                 "instance+window creation.");
      return FPX3D_VK_BAD_INSTANCE_LAYERS;
    }
  }

  VkInstanceCreateInfo inst_info = {0};

  inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  inst_info.pApplicationInfo = &ctx->appInfo;
  ctx->appInfo.pEngineName = "FPXLIB3D_VK";
  ctx->appInfo.engineVersion = VK_MAKE_VERSION(0, 3, 0);
  ctx->appInfo.apiVersion = VK_API_VERSION_1_0;

  size_t layer_count = ctx->instanceLayerCount;
  size_t alloc_count = layer_count;
  size_t layer_iter = 0;

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  if (vl_supported)
    ++alloc_count;
#endif

  const char **total_layers =
      (const char **)malloc(sizeof(const char **) * alloc_count);

  for (; layer_iter < layer_count; ++layer_iter) {
    total_layers[layer_iter] = ctx->instanceLayers[layer_iter];
  }

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  if (vl_supported) {
    ++layer_count;
    total_layers[layer_iter] = val_layer_ext;
  }
#endif

  inst_info.enabledLayerCount = layer_count;
  inst_info.ppEnabledLayerNames = total_layers;

  inst_info.enabledExtensionCount = ctx->instanceExtensionCount;
  inst_info.ppEnabledExtensionNames = ctx->instanceExtensions;

  VkResult res = vkCreateInstance(&inst_info, NULL, &ctx->vkInstance);

  FREE_SAFE(total_layers);

  if (VK_SUCCESS != res)
    return FPX3D_VK_INSTANCE_CREATE_ERROR;

  volkLoadInstance(ctx->vkInstance);

  if (VK_SUCCESS != res)
    return FPX3D_VK_SURFACE_CREATE_ERROR;

  FPX3D_DEBUG("Successfully created Vulkan instance");

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_destroy_instance(Fpx3d_Vk_Context *ctx,
                                         void (*destruction_callback)(void *)) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  Fpx3d_Wnd_Context *wnd = ctx->windowContext;

  NULL_CHECK(wnd, FPX3D_VK_BAD_WINDOW_CONTEXT_ERROR);

  if (NULL != destruction_callback)
    destruction_callback(ctx->customPointer);

  if (NULL != ctx->logicalGpus) {
    for (size_t i = 0; i < ctx->logicalGpuCapacity; ++i) {
      __fpx3d_vk_destroy_lgpu(ctx, &ctx->logicalGpus[i]);
    }

    FREE_SAFE(ctx->logicalGpus);
  }

  ctx->logicalGpuCapacity = 0;

  if (VK_NULL_HANDLE != ctx->vkInstance)
    vkDestroySurfaceKHR(ctx->vkInstance, ctx->vkSurface, NULL);

  if (VK_NULL_HANDLE != ctx->vkSurface)
    vkDestroyInstance(ctx->vkInstance, NULL);

  memset(ctx, 0, sizeof(*ctx));

  return FPX3D_SUCCESS;
}

Fpx3d_Wnd_Context *fpx3d_vk_get_windowcontext(Fpx3d_Vk_Context *vk_ctx) {
  NULL_CHECK(vk_ctx, NULL);

  return vk_ctx->windowContext;
}

Fpx3d_E_Result fpx3d_vk_set_custom_pointer(Fpx3d_Vk_Context *ctx, void *ptr) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  ctx->customPointer = ptr;

  return FPX3D_SUCCESS;
}

void *fpx3d_vk_get_custom_pointer(Fpx3d_Vk_Context *ctx) {
  NULL_CHECK(ctx, NULL);

  return ctx->customPointer;
}

struct _scored_gpu {
  VkPhysicalDevice gpu;
  int score;
};

Fpx3d_E_Result fpx3d_vk_select_gpu(
    Fpx3d_Vk_Context *ctx,
    int (*scoring_function)(Fpx3d_Vk_Context *, Fpx3d_Vk_PhysicalDevice)) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(scoring_function, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx->vkInstance, FPX3D_VK_BAD_VULKAN_INSTANCE_ERROR);

  VkPhysicalDevice *gpus = NULL;
  struct _scored_gpu *scored_gpus = NULL;

  uint32_t gpus_available = 0;

  {
    int success =
        vkEnumeratePhysicalDevices(ctx->vkInstance, &gpus_available, NULL);

    if (0 == gpus_available || VK_SUCCESS != success) {
      FPX3D_DEBUG("No Vulkan compatible GPU's were found!");
      return FPX3D_VK_NO_VULKAN_GPU_ERROR;
    }
  }

  // memory allocation for temporary buffers
  {
    gpus = (VkPhysicalDevice *)calloc(gpus_available, sizeof(VkPhysicalDevice));
    if (NULL == gpus) {
      perror("calloc()");
      return FPX3D_MEMORY_ERROR;
    }

    scored_gpus = (struct _scored_gpu *)calloc(gpus_available,
                                               sizeof(struct _scored_gpu));
    if (NULL == scored_gpus) {
      perror("calloc()");
      FREE_SAFE(gpus);
      return FPX3D_MEMORY_ERROR;
    }
  }

  vkEnumeratePhysicalDevices(ctx->vkInstance, &gpus_available, gpus);

  // auto sort the GPU's based on score, high to low
  for (ssize_t i = 0; i < gpus_available; ++i) {
    if (ctx->lgpuExtensionCount > 0 &&
        false == fpx3d_vk_device_extensions_supported(
                     gpus[i], ctx->lgpuExtensions, ctx->lgpuExtensionCount))
      continue;

    Fpx3d_Vk_PhysicalDevice dev = {0};
    vkGetPhysicalDeviceProperties(gpus[i], &dev.properties);
    vkGetPhysicalDeviceFeatures(gpus[i], &dev.features);
    dev.handle = gpus[i];

    FPX3D_DEBUG("Found GPU #%" LONG_FORMAT "d - \"%s\"", i,
                dev.properties.deviceName);

    int score = scoring_function(ctx, dev);

    // yipee magic
    for (ssize_t j = i; j >= 0; --j) {
      if (scored_gpus[j].score < score && 0 < i && j < (gpus_available - 1))
        memcpy(&scored_gpus[j + 1], &scored_gpus[j], sizeof(*scored_gpus));

      if (0 == j || scored_gpus[j - 1].score >= score) {
        scored_gpus[j].gpu = gpus[i];
        scored_gpus[j].score = score;

        break;
      }
    }

    for (int j = i; j >= 0; --j) {
    }
  }

  int retval = 0;
  if (1 > scored_gpus[0].score)
    retval = FPX3D_VK_NO_SUITABLE_VULKAN_GPU_ERROR;
  else
    ctx->physicalGpu = scored_gpus[0].gpu;

  FREE_SAFE(gpus);
  FREE_SAFE(scored_gpus);

  VkPhysicalDeviceProperties dev_props = {0};
  vkGetPhysicalDeviceProperties(ctx->physicalGpu, &dev_props);

  ctx->constants.bufferAlignment =
      MAX(dev_props.limits.minUniformBufferOffsetAlignment,
          dev_props.limits.minStorageBufferOffsetAlignment);

  FPX3D_DEBUG("Successfully picked a GPU to use");
  {
    char print_string[128] = {0};
    size_t amount_formatted =
        snprintf(print_string, sizeof(print_string) - 1,
                 " Using Vulkan GPU \"%s\"", dev_props.deviceName);
    char dash_bar[128] = {0};
    snprintf(dash_bar, MIN(sizeof(dash_bar) - 1, amount_formatted + 2),
             "-----------------------------------------------------------------"
             "-------------------------------------------------------------");
    fprintf(stderr, "%s\n%s\n%s\n", dash_bar, print_string, dash_bar);
  }

  return retval;
}
