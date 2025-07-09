#include <cglm/types.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

#define MAX(x, y) ((x > y) ? x : y)
#define CLAMP(v, x, y) ((v < x) ? x : (v > y) ? y : v)

#define FREE_SAFE(ptr)                                                         \
  if (NULL == ptr)                                                             \
  free(ptr)

#define NULL_CHECK(value, ret_code)                                            \
  if (NULL == value)                                                           \
  return ret_code

// const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

#ifdef DEBUG
#define FPX_VK_USE_VALIDATION_LAYERS
#endif

VkResult init_vulkan_context(vulkan_context *ctx) {
  memset(ctx, 0, sizeof(*ctx));

  return VK_SUCCESS;
}

uint8_t validation_layers_supported(const char **validation_layers,
                                    size_t layer_count) {
  if (layer_count == 0)
    return TRUE;

  uint32_t available = 0;
  VkLayerProperties *available_layers = NULL;

  vkEnumerateInstanceLayerProperties(&available, NULL);

  if (available < 1 || available < layer_count)
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

uint8_t device_extensions_supported(VkPhysicalDevice dev,
                                    const char **extensions,
                                    size_t extension_count) {
  if (extension_count == 0)
    return TRUE;

  uint32_t available = 0;
  VkExtensionProperties *available_extensions = NULL;

  vkEnumerateDeviceExtensionProperties(dev, NULL, &available, NULL);

  if (available < 1 || available < extension_count)
    return FALSE;

  available_extensions =
      (VkExtensionProperties *)calloc(available, sizeof(VkLayerProperties));

  if (NULL == available_extensions) {
    perror("calloc()");
    fprintf(stderr, "Error while checking for Vulkan validation layers.\n");
    return FALSE;
  }

  vkEnumerateDeviceExtensionProperties(dev, NULL, &available,
                                       available_extensions);

  for (int i = 0; i < extension_count; ++i) {
    uint8_t found = FALSE;

    for (int j = 0; j < available; ++j) {
      if (0 == strcmp(extensions[i], available_extensions[j].extensionName)) {
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

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  fprintf(stderr, "Validation YIPEE!\n");
  // we check for the queried validation layers, if need be.
  // if no layers are set to be queried, we just keep going
  if (NULL != val_layers &&
      !validation_layers_supported(val_layers,
                                   ctx->vk_context->validation_layers_count)) {

#ifdef DEBUG
    fprintf(stderr,
            "One or more requested instance layers were not available\n");
#endif

    return VK_ERROR_LAYER_NOT_PRESENT;
  }
#endif

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

VkResult destroy_vulkan_window(window_context *ctx) {
  vulkan_context *vk = ctx->vk_context;

  vkDestroySwapchainKHR(vk->logical_gpus[vk->current_swapchain.lgpu_index].gpu,
                        vk->current_swapchain.swapchain, NULL);
  vkDestroySurfaceKHR(vk->vk_instance, vk->vk_surface, NULL);
  vkDestroyInstance(vk->vk_instance, NULL);

  for (int i = 0; i < vk->lg_count; ++i) {
    vkDestroyDevice(vk->logical_gpus[i].gpu, NULL);
    memset(&vk->logical_gpus[i], 0, sizeof(vk->logical_gpus[i]));
  }

  glfwDestroyWindow(ctx->glfw_window);
  glfwTerminate();

  FREE_SAFE(vk->logical_gpus);

  vk->lg_count = 0;
  vk->lg_capacity = 0;

  memset(vk, 0, sizeof(*vk));

  return VK_SUCCESS;
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
    if (ctx->lgpu_extension_count > 0 &&
        FALSE == device_extensions_supported(gpus[i], ctx->lgpu_extensions,
                                             ctx->lgpu_extension_count))
      continue;

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

  // if (fam.queueCount < reqs->minimum_queues)
  //   return FALSE;

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
  struct queue_family_info info = {
      .index = -1, .family = {0}, .is_valid = FALSE};
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

  if (-1 < best_index && info.family.queueCount >= reqs->minimum_queues)
    info.is_valid = TRUE;

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

  struct queue_family_info render_qf = {0};
  struct queue_family_info presentation_qf = {0};

  struct queue_family_requirements reqs;
  memset(&reqs, 0, sizeof(reqs));

  reqs.type = RENDER;
  reqs.minimum_queues = render_queues;
  reqs.reqs.render.qf_flags = VK_QUEUE_GRAPHICS_BIT;
  render_qf = vulkan_queue_family(ctx, &reqs);

  memset(&reqs, 0, sizeof(reqs));

  reqs.type = PRESENTATION;
  reqs.minimum_queues = presentation_queues;
  reqs.reqs.present.gpu = ctx->physical_gpu;
  reqs.reqs.present.surface = ctx->vk_surface;
  presentation_qf = vulkan_queue_family(ctx, &reqs);

  if (0 > render_qf.index || 0 > presentation_qf.index)
    return -3;

  lgpu.render_queues = (VkQueue *)calloc(render_queues, sizeof(VkQueue));
  if (NULL == lgpu.render_queues) {
    perror("calloc()");
    return -4;
  }

  lgpu.presentation_queues =
      (VkQueue *)calloc(presentation_queues, sizeof(VkQueue));
  if (NULL == lgpu.presentation_queues) {
    perror("calloc()");
    FREE_SAFE(lgpu.render_queues);
    return -4;
  }

  VkDeviceQueueCreateInfo infos[2] = {0};

  VkDeviceQueueCreateInfo *r_info = &infos[0];
  lgpu.render_capacity = render_queues;
  lgpu.render_qf = render_qf.index;
  r_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  r_info->queueFamilyIndex = render_qf.index;
  r_info->queueCount = render_queues;
  r_info->pQueuePriorities = &priority;

  VkDeviceQueueCreateInfo *p_info = &infos[1];
  lgpu.presentation_capacity = presentation_queues;
  lgpu.presentation_qf = presentation_qf.index;
  p_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  p_info->queueFamilyIndex = presentation_qf.index;
  p_info->queueCount = presentation_queues;
  p_info->pQueuePriorities = &priority;

  VkDeviceCreateInfo d_info = {0};
  d_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  d_info.pQueueCreateInfos = infos;
  d_info.queueCreateInfoCount = sizeof(infos) / sizeof(*infos);
  d_info.pEnabledFeatures = features;

  d_info.enabledExtensionCount = ctx->lgpu_extension_count;
  d_info.ppEnabledExtensionNames = ctx->lgpu_extensions;

// no longer required, but used for
// compatibility with older impl. of Vulkan
#ifdef FPX_VK_USE_VALIDATION_LAYERS
  d_info.enabledLayerCount = ctx->validation_layers_count;
  d_info.ppEnabledLayerNames = ctx->validation_layers;
#else
  d_info.enabledLayerCount = 0;
#endif

  {
    lgpu.render_queues =
        (VkQueue *)calloc(lgpu.render_capacity, sizeof(VkQueue));
    if (NULL == lgpu.render_queues) {
      perror("calloc()");
      return -4;
    }

    lgpu.presentation_queues =
        (VkQueue *)calloc(lgpu.presentation_capacity, sizeof(VkQueue));
    if (NULL == lgpu.presentation_queues) {
      perror("calloc()");
      FREE_SAFE(lgpu.render_queues);
      return -4;
    }
  }

  VkResult res = vkCreateDevice(ctx->physical_gpu, &d_info, NULL, &lgpu.gpu);

#ifdef DEBUG
  fprintf(stderr, "vkCreateDevice() failed: error code %d\n", res);
#endif

  if (VK_SUCCESS != res) {
    FREE_SAFE(lgpu.render_queues);
    FREE_SAFE(lgpu.presentation_queues);

    return -5;
  }

  ctx->logical_gpus[ctx->lg_count] = lgpu;

  return ctx->lg_count++;
}

int new_vulkan_queue(vulkan_context *vk_ctx, struct logical_gpu_info *lgpu,
                     enum queue_family_type type) {
  size_t *capacity = NULL, *count = NULL;
  VkQueue *queues = NULL;
  int *qf_index = NULL;

  switch (type) {
  case RENDER:
    capacity = &lgpu->render_capacity;
    count = &lgpu->render_count;
    queues = lgpu->render_queues;
    qf_index = &lgpu->render_qf;
    break;

  case PRESENTATION:
    capacity = &lgpu->presentation_capacity;
    count = &lgpu->presentation_count;
    queues = lgpu->presentation_queues;
    qf_index = &lgpu->presentation_qf;
    break;

  default:
    return -1;
  }

  if (*count >= *capacity)
    return -2;

  vkGetDeviceQueue(lgpu->gpu, *qf_index, *count, &queues[*count]);

  return (*count)++;
}

static VkSurfaceFormatKHR *surface_format_picker(VkPhysicalDevice dev,
                                                 VkSurfaceKHR sfc,
                                                 VkSurfaceFormatKHR *fmts,
                                                 size_t fmts_count,
                                                 VkSurfaceFormatKHR *output) {
  uint32_t format_count = 0;
  VkSurfaceFormatKHR *formats = NULL;

  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, sfc, &format_count, NULL);

  if (0 == format_count)
    return NULL;

  formats =
      (VkSurfaceFormatKHR *)calloc(format_count, sizeof(VkSurfaceFormatKHR));

  if (NULL == formats) {
    perror("calloc()");
    return NULL;
  }

  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, sfc, &format_count, formats);

  if (0 == fmts_count) {
    *output = formats[0];
    FREE_SAFE(formats);
    return output;
  }

  for (int i = 0; i < fmts_count; ++i) {
    for (int j = 0; j < format_count; ++j) {
      if (fmts[i].format == formats[j].format &&
          fmts[i].colorSpace == formats[j].colorSpace) {
        FREE_SAFE(formats);
        *output = fmts[i];
        return output;
      }
    }
  }

  FREE_SAFE(formats);
  return NULL;
}

static VkPresentModeKHR *present_mode_picker(VkPhysicalDevice dev,
                                             VkSurfaceKHR sfc,
                                             VkPresentModeKHR *md,
                                             size_t md_count,
                                             VkPresentModeKHR *output) {
  uint32_t modes_count = 0;
  VkPresentModeKHR *modes = NULL;

  vkGetPhysicalDeviceSurfacePresentModesKHR(dev, sfc, &modes_count, NULL);

  if (0 == modes_count)
    return NULL;

  modes = (VkPresentModeKHR *)calloc(modes_count, sizeof(VkPresentModeKHR));

  if (NULL == modes) {
    perror("calloc()");
    return NULL;
  }

  vkGetPhysicalDeviceSurfacePresentModesKHR(dev, sfc, &modes_count, modes);

  if (0 == md_count) {
    *output = modes[0];
    FREE_SAFE(modes);
    return output;
  }

  for (int i = 0; i < md_count; ++i) {
    for (int j = 0; j < modes_count; ++j) {
      if (md[i] == modes[j]) {
        FREE_SAFE(modes);
        *output = md[i];
        return output;
      }
    }
  }

  FREE_SAFE(modes);
  return NULL;
}

struct swapchain_details
swapchain_compatibility(VkPhysicalDevice dev, VkSurfaceKHR sfc,
                        const struct swapchain_requirements *reqs) {
  struct swapchain_details details = {0};

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, sfc,
                                            &details.surface_capabilities);

  const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  if (FALSE == device_extensions_supported(dev, &ext, 1)) {
    return details;
  }

  details.swapchains_available = TRUE;

  if (reqs->surface_formats_count > 0) {
    VkSurfaceFormatKHR fmt;
    VkSurfaceFormatKHR *output = surface_format_picker(
        dev, sfc, reqs->surface_formats, reqs->surface_formats_count, &fmt);

    if (NULL != output) {
      details.surface_format = fmt;
      details.surface_format_valid = TRUE;
    }
  } else
    details.surface_format_valid = TRUE;

  if (reqs->present_modes_count > 0) {
    VkPresentModeKHR md;
    VkPresentModeKHR *output = present_mode_picker(
        dev, sfc, reqs->present_modes, reqs->present_modes_count, &md);

    if (NULL != output) {
      details.present_mode = md;
      details.present_mode_valid = TRUE;
    }
  } else
    details.present_mode_valid = TRUE;

  return details;
}

static VkExtent2D
swapchain_extent(window_context *ctx,
                 const VkSurfaceCapabilitiesKHR *capabilities) {
  if (capabilities->currentExtent.width != UINT32_MAX)
    return capabilities->currentExtent;

  VkExtent2D retval = {0};
  int width, height;

  glfwGetFramebufferSize(ctx->glfw_window, &width, &height);

  retval.height = CLAMP((uint32_t)height, capabilities->minImageExtent.height,
                        capabilities->maxImageExtent.height);
  retval.width = CLAMP((uint32_t)width, capabilities->minImageExtent.width,
                       capabilities->maxImageExtent.width);

  return retval;
}

int create_vulkan_swap_chain(window_context *ctx,
                             const struct swapchain_details *details,
                             size_t lgpu_index) {
  if (!(details->surface_format_valid && details->present_mode_valid))
    return -1;

  const VkSurfaceCapabilitiesKHR *cap = &details->surface_capabilities;

  VkSwapchainCreateInfoKHR s_info = {0};
  VkExtent2D extent = swapchain_extent(ctx, cap);

  struct logical_gpu_info *lgpu = &ctx->vk_context->logical_gpus[lgpu_index];
  struct active_swapchain *current_chain = &ctx->vk_context->current_swapchain;

  {
    uint32_t image_count = cap->minImageCount + 1;
    if (cap->maxImageCount > 0 && image_count > cap->maxImageCount)
      image_count = cap->maxImageCount;

    s_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    s_info.surface = ctx->vk_context->vk_surface;

    s_info.minImageCount = image_count;

    s_info.imageFormat = details->surface_format.format;
    s_info.imageColorSpace = details->surface_format.colorSpace;

    s_info.imageExtent = extent;
    s_info.imageArrayLayers = 1;
    s_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (lgpu->render_qf != lgpu->presentation_qf) {
      uint32_t qf_indices[2] = {lgpu->render_qf, lgpu->presentation_qf};

      s_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      s_info.queueFamilyIndexCount = 2;
      s_info.pQueueFamilyIndices = qf_indices;
    } else {
      s_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      s_info.queueFamilyIndexCount = 0;
      s_info.pQueueFamilyIndices = NULL;
    }

    s_info.preTransform = cap->currentTransform;
    s_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    s_info.presentMode = details->present_mode;
    s_info.clipped = VK_TRUE;

    s_info.oldSwapchain = current_chain->swapchain;
  }
  VkSwapchainKHR new_swapchain = {0};

  VkResult success =
      vkCreateSwapchainKHR(lgpu->gpu, &s_info, NULL, &new_swapchain);

  if (VK_SUCCESS != success) {
#ifdef DEBUG
    fprintf(stderr, "Error while creating swapchain. Code: %d.\n", success);
#endif
    return -2;
  }

  uint32_t image_count = 0;
  VkImage *images = NULL;
  vkGetSwapchainImagesKHR(lgpu->gpu, new_swapchain, &image_count, NULL);

  images = (VkImage *)calloc(image_count, sizeof(VkImage));
  if (NULL == images) {
    perror("calloc()");

    vkDestroySwapchainKHR(lgpu->gpu, new_swapchain, NULL);

    current_chain->lgpu_index = -1;
    current_chain->swapchain = VK_NULL_HANDLE;

    return -3;
  }

  vkGetSwapchainImagesKHR(lgpu->gpu, new_swapchain, &image_count, images);

  if (0 != current_chain->image_count) {
    FREE_SAFE(current_chain->images);
  }

  if (0 != current_chain->view_count) {
    FREE_SAFE(current_chain->views);
  }

  size_t view_count = image_count;
  VkImageView *views = (VkImageView *)calloc(view_count, sizeof(VkImageView));

  if (NULL == views) {
    perror("calloc()");

    vkDestroySwapchainKHR(lgpu->gpu, new_swapchain, NULL);

    current_chain->lgpu_index = -1;
    current_chain->swapchain = VK_NULL_HANDLE;

    FREE_SAFE(images);
    return -3;
  }

  for (int i = 0; i < view_count; ++i) {
    VkImageViewCreateInfo v_info = {0};

    v_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    v_info.image = images[i];
    v_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v_info.format = details->surface_format.format;

    v_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    v_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    v_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    v_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    v_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    v_info.subresourceRange.baseMipLevel = 0;
    v_info.subresourceRange.levelCount = 1;
    v_info.subresourceRange.baseArrayLayer = 0;
    v_info.subresourceRange.layerCount = 1;

    VkResult success = vkCreateImageView(lgpu->gpu, &v_info, NULL, &views[i]);

    if (VK_SUCCESS != success) {
      fprintf(stderr, "Error while setting up Vulkan swap chain. Code: %d.\n",
              success);
      return -4;
    }
  }

  current_chain->lgpu_index = lgpu_index;
  current_chain->swapchain = new_swapchain;
  current_chain->images = images;
  current_chain->image_count = image_count;
  current_chain->image_format = details->surface_format.format;

  return VK_SUCCESS;
}
