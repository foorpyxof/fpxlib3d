#include <cglm/types.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cglm/mat4.h>
#include <cglm/vec4.h>

#define WINDOW_WIDTH 500
#define WINDOW_HEIGHT 500

#define LOGICAL_GPU_COUNT 1

#define TRUE 1
#define FALSE 0

#include "vk.h"

#define FREE_SAFE(ptr)                                                         \
  if (NULL == ptr)                                                             \
    free(ptr);

#define NULL_CHECK(value, ret_code)                                            \
  if (NULL == value)                                                           \
    return ret_code;

// const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

#ifdef DEBUG
#define FPX_VK_USE_VALIDATION_LAYERS 1
#else
#define FPX_VK_USE_VALIDATION_LAYERS 0
#endif

int init_vulkan_context(vulkan_context *ctx) {
  memset(ctx, 0, sizeof(*ctx));

  return 0;
}

uint8_t validation_layers_supported(const char **validation_layers,
                                    size_t layer_count) {
  uint32_t available = 0;
  VkLayerProperties *available_layers = NULL;

  vkEnumerateInstanceLayerProperties(&available, NULL);

  if (available < 1)
    return FALSE;

  available_layers =
      (VkLayerProperties *)calloc(available, sizeof(VkLayerProperties));

  if (NULL == available_layers) {
    perror("calloc()");
    fprintf(stderr, "Error while checking for Vulkan validation layers.\n");
    return FALSE;
  }

  vkEnumerateInstanceLayerProperties(&available, available_layers);

  for (int i = 0; i < layer_count; ++i) {
    uint8_t found = FALSE;

    for (int j = 0; j < available; ++j) {
      if (0 == strcmp(validation_layers[i], available_layers[j].layerName)) {
        found = TRUE;
        break;
      }
    }

    if (FALSE == found)
      return FALSE;
  }

  return TRUE;
}

int create_vulkan_window(window_context *ctx) {
  if (VK_STRUCTURE_TYPE_APPLICATION_INFO != ctx->vk_context->app_info.sType ||
      NULL == ctx->window_title || 1 > ctx->window_dimensions[0] ||
      1 > ctx->window_dimensions[1])
    return -99;

  const char **val_layers = ctx->vk_context->validation_layers;

  // we check for the queried validation layers, if need be.
  // if no layers are set to be queried, we just keep going
  if (NULL != val_layers && FPX_VK_USE_VALIDATION_LAYERS &&
      !validation_layers_supported(val_layers,
                                   ctx->vk_context->validation_layers_count)) {

#if defined(DEBUG) && defined(FPX_VK_USE_VALIDATION_LAYERS)
    fprintf(stderr,
            "One or more requested instance layers were not available\n");
#endif

    return VK_ERROR_LAYER_NOT_PRESENT;
  }

  VkInstanceCreateInfo c_info = {0};

  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  ctx->glfw_window =
      glfwCreateWindow(ctx->window_dimensions[0], ctx->window_dimensions[1],
                       ctx->window_title, NULL, NULL);

  c_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  c_info.pApplicationInfo = &ctx->vk_context->app_info;

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  if (NULL != val_layers) {
    c_info.enabledLayerCount = ctx->vk_context->validation_layers_count;
    c_info.ppEnabledLayerNames = val_layers;
  } else {
#endif
    c_info.enabledLayerCount = 0;
#ifdef FPX_VK_USE_VALIDATION_LAYERS
  }
#endif

  uint32_t glfw_extensions_count = 0;
  const char **glfw_extensions = NULL;

  glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);

#ifdef DEBUG
  fprintf(stderr, "%u extensions supported.\n", glfw_extensions_count);
#endif

  c_info.enabledExtensionCount = glfw_extensions_count;
  c_info.ppEnabledExtensionNames = glfw_extensions;

  VkResult res = vkCreateInstance(&c_info, NULL, &ctx->vk_context->vk_instance);
  if (VK_SUCCESS != res)
    return res;

  res = glfwCreateWindowSurface(ctx->vk_context->vk_instance, ctx->glfw_window,
                                NULL, &ctx->vk_context->vk_surface);

  return res;
}

struct scored_gpu {
  VkPhysicalDevice gpu;
  int score;
};

int choose_vulkan_gpu(vulkan_context *ctx,
                      int (*scoring_function)(vulkan_context *,
                                              VkPhysicalDevice)) {
  VkPhysicalDevice *gpus = NULL;
  struct scored_gpu *scored_gpus = NULL;

  uint32_t gpus_available = 0;

  {
    int success =
        vkEnumeratePhysicalDevices(ctx->vk_instance, &gpus_available, NULL);

    if (0 == gpus_available) {
      fprintf(stderr, "No Vulkan compatible GPU's were found!\n");
      return -1;
    }
  }

  // memory allocation for temporary buffers
  {
    gpus = (VkPhysicalDevice *)calloc(gpus_available, sizeof(VkPhysicalDevice));
    if (NULL == gpus) {
      perror("calloc()");
      return -2;
    }

    scored_gpus =
        (struct scored_gpu *)calloc(gpus_available, sizeof(struct scored_gpu));
    if (NULL == scored_gpus) {
      perror("calloc()");
      FREE_SAFE(gpus);
      return -2;
    }
  }

  vkEnumeratePhysicalDevices(ctx->vk_instance, &gpus_available, gpus);

  // auto sort the GPU's based on score, high to low
  for (int i = 0; i < gpus_available; ++i) {
    int score = scoring_function(ctx, gpus[i]);

    for (int j = i; j >= 0; --j) {
      if (scored_gpus[j].score < score && 0 < i)
        memcpy(&scored_gpus[j], &scored_gpus[j + 1], sizeof(*scored_gpus));

      if (0 == j || scored_gpus[j - 1].score >= score) {
        scored_gpus[j].gpu = gpus[i];
        scored_gpus[j].score = score;
      }
    }
  }

  int retval = 0;
  if (1 > scored_gpus[0].score)
    retval = -3;
  else
    ctx->physical_gpu = scored_gpus[0].gpu;

  FREE_SAFE(gpus);
  FREE_SAFE(scored_gpus);

  return retval;
}

static uint8_t qf_meets_requirements(VkQueueFamilyProperties fam,
                                     struct queue_family_requirements *reqs,
                                     int qf_index) {
  union req_union *req = &reqs->reqs;

  if (fam.queueCount < reqs->minimum_queues)
    return FALSE;

  switch (reqs->type) {
  case PRESENTATION:
    if (NULL == req->present.surface)
      break;

    VkBool32 present_support = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(
        req->present.gpu, qf_index, req->present.surface, &present_support);

    if (true == present_support)
      return TRUE;

    break;

  case RENDER:
    if (req->render.qf_flags == (req->render.qf_flags & fam.queueFlags))
      return TRUE;

    break;

  default:
    break;
  }

  return FALSE;
}

struct queue_family_info
vulkan_queue_family(vulkan_context *ctx,
                    struct queue_family_requirements *reqs) {
  struct queue_family_info info = {.index = -1, .family = {0}};
  VkQueueFamilyProperties *props = NULL;
  uint32_t qf_count = 0;

  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_gpu, &qf_count, NULL);

  if (0 == qf_count) {
    info.index = -1;
    return info;
  }

  props = (VkQueueFamilyProperties *)calloc(qf_count,
                                            sizeof(VkQueueFamilyProperties));

  if (NULL == props) {
    perror("calloc()");
    info.index = -2;
    return info;
  }

  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_gpu, &qf_count, props);

  int64_t best_index = -1;
  for (int64_t i = 0; i < qf_count; ++i) {
    VkQueueFamilyProperties *prop = &props[i];

    if (qf_meets_requirements(*prop, reqs, i)) {
      if (best_index < 0) {
        best_index = i;
        continue;
      }

      if (prop->queueCount > props[best_index].queueCount) {
        best_index = i;
      }
    }
  }

  info.index = best_index;
  info.family = props[best_index];

  FREE_SAFE(props);

  return info;
}

int new_logical_vulkan_gpu(vulkan_context *ctx, float priority,
                           VkPhysicalDeviceFeatures *features,
                           int render_queues, int presentation_queues) {
  if (ctx->lg_count >= ctx->lg_capacity)
    return -1;

  int logical_gpu_index = ctx->lg_count;
  struct logical_gpu_info lgpu = ctx->logical_gpus[logical_gpu_index];

  uint32_t qf_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_gpu, &qf_count, NULL);

  if (0 == qf_count)
    return -2;

  struct queue_family_requirements reqs;
  memset(&reqs, 0, sizeof(reqs));

  reqs.type = RENDER;
  reqs.minimum_queues = render_queues + presentation_queues;
  reqs.reqs.render.qf_flags = VK_QUEUE_GRAPHICS_BIT;
  struct queue_family_info render_qf = vulkan_queue_family(ctx, &reqs);
  if (0 > render_qf.index)
    return -1;

  memset(&reqs, 0, sizeof(reqs));

  reqs.type = PRESENTATION;
  reqs.minimum_queues = render_queues + presentation_queues;
  reqs.reqs.present.gpu = ctx->physical_gpu;
  reqs.reqs.present.surface = ctx->vk_surface;
  struct queue_family_info presentation_qf = vulkan_queue_family(ctx, &reqs);
  if (0 > presentation_qf.index)
    return -1;

  lgpu.render_queues =
      (struct queue_info *)calloc(render_queues, sizeof(struct queue_info));
  if (NULL == lgpu.render_queues) {
    perror("calloc()");
    return -2;
  }

  lgpu.presentation_queues = (struct queue_info *)calloc(
      presentation_queues, sizeof(struct queue_info *));
  if (NULL == lgpu.presentation_queues) {
    perror("calloc()");
    FREE_SAFE(lgpu.render_queues);
    return -2;
  }

  VkDeviceQueueCreateInfo infos[2];

  VkDeviceQueueCreateInfo *r_info = &infos[0];
  lgpu.render_count = render_queues;
  r_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  r_info->queueFamilyIndex = render_qf.index;
  r_info->queueCount = render_queues;
  r_info->pQueuePriorities = &priority;

  VkDeviceQueueCreateInfo *p_info = &infos[1];
  lgpu.presentation_count = presentation_queues;
  p_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  p_info->queueFamilyIndex = presentation_qf.index;
  p_info->queueCount = presentation_queues;
  p_info->pQueuePriorities = &priority;

  VkDeviceCreateInfo d_info = {0};
  d_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  d_info.pQueueCreateInfos = infos;
  d_info.queueCreateInfoCount = sizeof(infos) / sizeof(*infos);
  d_info.pEnabledFeatures = features;

// no longer required, but used for
// compatibility with older impl. of Vulkan
#ifdef FPX_VK_USE_VALIDATION_LAYERS
  d_info.enabledLayerCount = ctx->validation_layers_count;
  d_info.ppEnabledLayerNames = ctx->validation_layers;
#else
  d_info.enabledLayerCount = 0;
#endif

  VkResult res = vkCreateDevice(ctx->physical_gpu, &d_info, NULL, &lgpu.gpu);

  if (VK_SUCCESS != res)
    return -3;

  ctx->logical_gpus[ctx->lg_count] = lgpu;

  return ctx->lg_count++;
}
