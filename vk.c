#include <cglm/types.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <vk_video/vulkan_video_codec_av1std.h>
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
#define CONDITIONAL(cond, then, else) ((cond) ? (then) : (else))

#define FREE_SAFE(ptr)                                                         \
  {                                                                            \
    if (NULL != ptr) {                                                         \
      free(ptr);                                                               \
      ptr = NULL;                                                              \
    }                                                                          \
  }

#define NULL_CHECK(value, ret_code)                                            \
  if (NULL == value)                                                           \
  return ret_code

// const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

#ifdef DEBUG
#define FPX_VK_USE_VALIDATION_LAYERS
#endif

#ifndef MAX_IN_FLIGHT
#define MAX_IN_FLIGHT 2
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

static void clean_swapchain(struct logical_gpu_info *lgpu) {
  struct active_swapchain *sc = &lgpu->current_swapchain;

  if (NULL != sc->frames)
    for (int i = 0; i < sc->frame_count; ++i) {
      struct swapchain_frame *f = &sc->frames[i];
      if (VK_NULL_HANDLE != sc->frames[i].framebuffer)
        vkDestroyFramebuffer(lgpu->gpu, sc->frames[i].framebuffer, NULL);

      if (VK_NULL_HANDLE != f->view)
        vkDestroyImageView(lgpu->gpu, f->view, NULL);

      if (VK_NULL_HANDLE != f->write_available) {
        vkDestroySemaphore(lgpu->gpu, f->write_available, NULL);
      }
      if (VK_NULL_HANDLE != f->render_finished) {
        vkDestroySemaphore(lgpu->gpu, f->render_finished, NULL);
      }
    }

  FREE_SAFE(sc->frames);
  sc->frame_count = 0;

  return;
}

static void destroy_lgpu(struct logical_gpu_info *lgpu) {
  struct active_swapchain *sc = &lgpu->current_swapchain;

  vkDeviceWaitIdle(lgpu->gpu);

  if (VK_NULL_HANDLE != lgpu->command_pool)
    vkDestroyCommandPool(lgpu->gpu, lgpu->command_pool, NULL);

  clean_swapchain(lgpu);

  if (VK_NULL_HANDLE != sc->swapchain)
    vkDestroySwapchainKHR(lgpu->gpu, sc->swapchain, NULL);

  if (NULL != lgpu->render_passes)
    for (int i = 0; i < lgpu->render_pass_capacity; ++i) {
      if (VK_NULL_HANDLE != lgpu->render_passes[i])
        vkDestroyRenderPass(lgpu->gpu, lgpu->render_passes[i], NULL);
    }

  FREE_SAFE(lgpu->render_passes);

  for (int i = 0; i < lgpu->pipeline_capacity; ++i) {
    struct pipeline *p = &lgpu->pipelines[i];

    if (NULL != p->pipeline)
      vkDestroyPipeline(lgpu->gpu, p->pipeline, NULL);

    if (NULL != p->layout)
      vkDestroyPipelineLayout(lgpu->gpu, p->layout, NULL);
  }

  if (VK_NULL_HANDLE != lgpu->current_swapchain.aquire_semaphore) {
    vkDestroySemaphore(lgpu->gpu, lgpu->current_swapchain.aquire_semaphore,
                       NULL);
  }

  // fences
  if (VK_NULL_HANDLE != sc->in_flight_fence) {
    vkDestroyFence(lgpu->gpu, sc->in_flight_fence, NULL);
  }

  vkDestroyDevice(lgpu->gpu, NULL);
  memset(lgpu, 0, sizeof(*lgpu));
}

VkResult destroy_vulkan_window(window_context *ctx) {
  vulkan_context *vk = ctx->vk_context;

  for (int i = 0; i < vk->lg_count; ++i) {
    destroy_lgpu(&vk->logical_gpus[i]);
  }

  glfwDestroyWindow(ctx->glfw_window);
  glfwTerminate();

  FREE_SAFE(vk->logical_gpus);

  vk->lg_count = 0;
  vk->lg_capacity = 0;

  vkDestroySurfaceKHR(vk->vk_instance, vk->vk_surface, NULL);
  vkDestroyInstance(vk->vk_instance, NULL);

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
                           VkPhysicalDeviceFeatures features, int render_queues,
                           int presentation_queues) {
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

  VkDeviceQueueCreateInfo infos[2] = {0};
  size_t infos_count = 1;

  lgpu.render_capacity = render_queues;
  lgpu.render_qf = render_qf.index;
  lgpu.presentation_capacity = presentation_queues;
  lgpu.presentation_qf = presentation_qf.index;

  VkDeviceQueueCreateInfo *r_info = &infos[0];
  r_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  r_info->queueFamilyIndex = render_qf.index;
  r_info->queueCount =
      CONDITIONAL(render_qf.index == presentation_qf.index,
                  MAX(render_queues, presentation_queues), render_queues);
  r_info->pQueuePriorities = &priority;

  if (render_qf.index != presentation_qf.index) {
    infos_count = 2;

    VkDeviceQueueCreateInfo *p_info = &infos[1];
    p_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    p_info->queueFamilyIndex = presentation_qf.index;
    p_info->queueCount = presentation_queues;
    p_info->pQueuePriorities = &priority;
  }

  VkDeviceCreateInfo d_info = {0};
  d_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  d_info.pQueueCreateInfos = infos;
  d_info.queueCreateInfoCount = infos_count;
  d_info.pEnabledFeatures = &features;

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

  if (VK_SUCCESS != res) {
#ifdef DEBUG
    fprintf(stderr, "vkCreateDevice() failed: error code %d\n", res);
#endif

    FREE_SAFE(lgpu.render_queues);
    FREE_SAFE(lgpu.presentation_queues);

    return -5;
  }

  lgpu.features = features;
  ctx->logical_gpus[ctx->lg_count] = lgpu;

  return ctx->lg_count++;
}

int new_vulkan_queue(vulkan_context *vk_ctx, struct indices *indices,
                     enum queue_family_type type) {
  size_t *capacity = NULL, *count = NULL;
  VkQueue *queues = NULL;
  int *qf_index = NULL;

  struct logical_gpu_info *lgpu = &vk_ctx->logical_gpus[indices->logical_gpu];

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

void save_swapchain_requirements(vulkan_context *ctx,
                                 const struct swapchain_requirements *reqs) {
  ctx->swapchain_requirements = *reqs;

  return;
}

struct swapchain_details swapchain_compatibility(vulkan_context *ctx,
                                                 VkPhysicalDevice dev) {
  struct swapchain_details details = {0};
  struct swapchain_requirements *reqs = &ctx->swapchain_requirements;

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, ctx->vk_surface,
                                            &details.surface_capabilities);

  const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  if (FALSE == device_extensions_supported(dev, &ext, 1)) {
    return details;
  }

  details.swapchains_available = TRUE;

  if (reqs->surface_formats_count > 0) {
    VkSurfaceFormatKHR fmt;
    VkSurfaceFormatKHR *output =
        surface_format_picker(dev, ctx->vk_surface, reqs->surface_formats,
                              reqs->surface_formats_count, &fmt);

    if (NULL != output) {
      details.surface_format = fmt;
      details.surface_format_valid = TRUE;
    }
  } else
    details.surface_format_valid = TRUE;

  if (reqs->present_modes_count > 0) {
    VkPresentModeKHR md;
    VkPresentModeKHR *output =
        present_mode_picker(dev, ctx->vk_surface, reqs->present_modes,
                            reqs->present_modes_count, &md);

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
                             struct indices *indices) {
  if (!(details->surface_format_valid && details->present_mode_valid))
    return -1;

  const VkSurfaceCapabilitiesKHR *cap = &details->surface_capabilities;

  VkSwapchainCreateInfoKHR s_info = {0};
  VkExtent2D extent = swapchain_extent(ctx, cap);

  struct logical_gpu_info *lgpu =
      &ctx->vk_context->logical_gpus[indices->logical_gpu];
  struct active_swapchain *current_chain = &lgpu->current_swapchain;

  VkSwapchainKHR old_swapchain = current_chain->swapchain;

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

    s_info.oldSwapchain = old_swapchain;
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

  uint32_t frame_count = 0;
  VkImage *images = NULL;
  vkGetSwapchainImagesKHR(lgpu->gpu, new_swapchain, &frame_count, NULL);

  images = (VkImage *)calloc(frame_count, sizeof(VkImage));
  if (NULL == images) {
    perror("calloc()");

    vkDestroySwapchainKHR(lgpu->gpu, new_swapchain, NULL);

    current_chain->swapchain = VK_NULL_HANDLE;

    return -3;
  }

  VkImageView *views = (VkImageView *)calloc(frame_count, sizeof(VkImageView));

  if (NULL == views) {
    perror("calloc()");

    vkDestroySwapchainKHR(lgpu->gpu, new_swapchain, NULL);

    current_chain->swapchain = VK_NULL_HANDLE;

    FREE_SAFE(images);
    return -3;
  }

  vkGetSwapchainImagesKHR(lgpu->gpu, new_swapchain, &frame_count, images);

  for (int i = 0; i < frame_count; ++i) {
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

  if (0 != current_chain->frame_count) {
    FREE_SAFE(current_chain->frames);
  }

  struct swapchain_frame *frames = (struct swapchain_frame *)calloc(
      frame_count, sizeof(struct swapchain_frame));
  if (NULL == frames) {
    perror("calloc()");

    FREE_SAFE(images);
    FREE_SAFE(views);

    return -3;
  }

  VkSemaphore sema = VK_NULL_HANDLE;

  {
    VkSemaphoreCreateInfo sema_info = {0};
    sema_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (int i = 0; i < frame_count; ++i) {
      if (VK_SUCCESS != vkCreateSemaphore(lgpu->gpu, &sema_info, NULL,
                                          &frames[i].write_available) ||
          VK_SUCCESS != vkCreateSemaphore(lgpu->gpu, &sema_info, NULL,
                                          &frames[i].render_finished)) {
        FREE_SAFE(images);
        FREE_SAFE(views);
        FREE_SAFE(frames);

        return -5;
      }

      frames[i].image = images[i];
      frames[i].view = views[i];
    }

    if (VK_NULL_HANDLE == lgpu->current_swapchain.aquire_semaphore) {
      if (VK_SUCCESS != vkCreateSemaphore(lgpu->gpu, &sema_info, NULL, &sema)) {
        FREE_SAFE(images);
        FREE_SAFE(views);
        FREE_SAFE(frames);

        return -5;
      }

      current_chain->aquire_semaphore = sema;
    }
  }

  {
    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkFence fence = current_chain->in_flight_fence;

    if (VK_NULL_HANDLE == current_chain->in_flight_fence)
      if (VK_SUCCESS != vkCreateFence(lgpu->gpu, &fence_info, NULL, &fence)) {
        FREE_SAFE(images);
        FREE_SAFE(views);
        FREE_SAFE(frames);

        return -5;
      }

    current_chain->in_flight_fence = fence;
  }

  if (VK_NULL_HANDLE != old_swapchain)
    vkDestroySwapchainKHR(lgpu->gpu, old_swapchain, NULL);

  current_chain->image_format = details->surface_format.format;
  current_chain->swapchain = new_swapchain;
  current_chain->swapchain_extent = extent;
  current_chain->frames = frames;
  current_chain->frame_count = frame_count;

  return VK_SUCCESS;
}

int refresh_vulkan_swap_chain(window_context *w_ctx,
                              const struct swapchain_details *details,
                              struct indices *indices) {
  struct logical_gpu_info *lgpu =
      &w_ctx->vk_context->logical_gpus[indices->logical_gpu];

  // first we clean it up
  vkDeviceWaitIdle(lgpu->gpu);
  clean_swapchain(lgpu);

  // then we recreate it
  create_vulkan_swap_chain(w_ctx, details, indices);
  create_framebuffers(w_ctx->vk_context, indices);

  return 0;
}

struct spirv_file read_spirv(const char *filename, enum shader_stage stage) {
  struct spirv_file file = {0};

  FILE *fp = fopen(filename, "r");
  if (NULL == fp) {
    perror("fopen()");
    return file;
  }

  if (0 > fseek(fp, 0, SEEK_END)) {
    perror("fseek()");
    fclose(fp);
    return file;
  }

  int size = ftell(fp);
  if (0 > size) {
    perror("ftell()");
    fclose(fp);
    return file;
  }

#ifdef DEBUG
  fprintf(stderr, "Read %d bytes from file \"%s\"\n", size, filename);
#endif

  rewind(fp);

  // align to 4 bytes (sizeof uint32_t) because the shader module reads it using
  // a uint32_t pointer for some reason
  file.buffer =
      (uint8_t *)malloc(size + (sizeof(uint32_t) - (size % sizeof(uint32_t))));
  if (NULL == file.buffer) {
    perror("malloc()");
    fclose(fp);
    return file;
  }

  if (size > fread(file.buffer, 1, size, fp)) {
#ifdef DEBUG
    fprintf(stderr, "Read too little from SPIR-V file");
#endif
    FREE_SAFE(file.buffer);
    fclose(fp);
    return file;
  }

  file.filesize = size;
  file.stage = stage;

  fclose(fp);

  return file;
}

void free_spirv_file(struct spirv_file *spirv) {
  FREE_SAFE(spirv->buffer);
  memset(spirv, 0, sizeof(*spirv));
}

static VkShaderModule *select_module_stage(struct shader_set *set,
                                           enum shader_stage stage) {

  VkShaderModule *module = NULL;

  switch (stage) {
  // case INPUT:
  //   module = &set->input;
  //   break;
  case VERTEX:
    module = &set->vertex;
    break;
  case TESSELATION:
    module = &set->tesselation;
    break;
  case GEOMETRY:
    module = &set->geometry;
    break;
  // case RASTERIZATION:
  //   module = &set->rasterization;
  //   break;
  case FRAGMENT:
    module = &set->fragment;
    break;
    // case BLENDING:
    //   module = &set->blending;
    //   break;

  default:
    // error
    break;
  }

  return module;
}

int fill_shader_module(vulkan_context *ctx, struct indices *indices,
                       const struct spirv_file spirv, struct shader_set *set) {
  VkShaderModule *module = NULL;
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  module = select_module_stage(set, spirv.stage);

  if (NULL == module)
    return -1;

  VkShaderModuleCreateInfo m_info = {0};

  VkShaderModule temp = {0};

  m_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  m_info.codeSize = spirv.filesize;
  m_info.pCode = (uint32_t *)spirv.buffer;

  int success = vkCreateShaderModule(lgpu->gpu, &m_info, NULL, &temp);

  if (VK_SUCCESS != success)
    return -2;

  *module = temp;

  return VK_SUCCESS;
}

VkRenderPass create_render_pass(vulkan_context *ctx, struct indices *indices) {
  // TODO: make modular. currently hardcoded to follow tutorial
  // maybe have the programmer pass an array of color attachments,
  // or maybe subpasses

  VkRenderPass pass = VK_NULL_HANDLE;
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  VkAttachmentDescription color_attachment = {0};
  color_attachment.format = lgpu->current_swapchain.image_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT; // TODO: multisampling

  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference a_ref = {0};
  a_ref.attachment = 0;
  a_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {0};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &a_ref;

  VkSubpassDependency s_dep = {0};
  s_dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  s_dep.dstSubpass = 0;

  s_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  s_dep.srcAccessMask = 0;

  s_dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  s_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo r_info = {0};
  r_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  r_info.attachmentCount = 1;
  r_info.pAttachments = &color_attachment;
  r_info.subpassCount = 1;
  r_info.pSubpasses = &subpass;
  r_info.dependencyCount = 1;
  r_info.pDependencies = &s_dep;

  {
    int success = vkCreateRenderPass(
        ctx->logical_gpus[indices->logical_gpu].gpu, &r_info, NULL, &pass);

    if (VK_SUCCESS != success) {
      return VK_NULL_HANDLE;
    }
  }

  return pass;
}

int allocate_render_passes(vulkan_context *ctx, struct indices *indices,
                           size_t amount) {
  if (1 > amount)
    return -1;

  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  lgpu->render_passes = (VkRenderPass *)realloc(lgpu->render_passes,
                                                amount * sizeof(VkRenderPass));

  if (NULL == lgpu->render_passes) {
    perror("realloc()");
    return -2;
  }

  memset(&lgpu->render_passes[lgpu->render_pass_capacity], 0,
         sizeof(lgpu->render_passes[0]) *
             (amount - lgpu->render_pass_capacity));

  lgpu->render_pass_capacity = amount;

  return 0;
}

int add_render_pass(vulkan_context *ctx, struct indices *indices,
                    VkRenderPass pass) {
  ctx->logical_gpus[indices->logical_gpu].render_passes[indices->render_pass] =
      pass;

  return 0;
}

int allocate_pipelines(vulkan_context *ctx, struct indices *indices,
                       size_t amount) {
  if (1 > amount)
    return -1;

  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  lgpu->pipelines = (struct pipeline *)realloc(
      lgpu->pipelines, amount * sizeof(struct pipeline));

  if (NULL == lgpu->pipelines) {
    perror("realloc()");
    return -2;
  }

  memset(&lgpu->pipelines[lgpu->pipeline_capacity], 0,
         sizeof(lgpu->pipelines[0]) * (amount - lgpu->pipeline_capacity));

  lgpu->pipeline_capacity = amount;

  return 0;
}

int create_graphics_pipeline(vulkan_context *ctx, struct indices *indices,
                             const struct spirv_file *spirvs,
                             size_t spirv_count, struct shader_set *set,
                             size_t vertices) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];
  struct pipeline *p = &lgpu->pipelines[indices->pipeline];

  // populate shader_set struct
  for (int i = 0; i < spirv_count; ++i) {
    VkShaderModule *module = select_module_stage(set, spirvs[i].stage);
    if (VK_NULL_HANDLE != *module)
      continue;

    int success = fill_shader_module(ctx, indices, spirvs[i], set);

    if (0 != success) {
      // error happened
      return -2;
    }
  }

  VkPipelineShaderStageCreateInfo stage_infos[2] = {0};

#define PIPELINE_STAGE(mod, stage_bit)                                         \
  {                                                                            \
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,              \
    .stage = stage_bit, .module = mod, .pName = "main",                        \
  }

  {
    VkPipelineShaderStageCreateInfo s_info =
        PIPELINE_STAGE(set->vertex, VK_SHADER_STAGE_VERTEX_BIT);

    stage_infos[0] = s_info;
  }

  {
    VkPipelineShaderStageCreateInfo s_info =
        PIPELINE_STAGE(set->fragment, VK_SHADER_STAGE_FRAGMENT_BIT);

    stage_infos[1] = s_info;
  }
#undef PIPELINE_STAGE

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo d_info = {0};
  VkPipelineVertexInputStateCreateInfo v_info = {0};
  VkPipelineInputAssemblyStateCreateInfo a_info = {0};
  VkPipelineViewportStateCreateInfo vs_info = {0};
  VkPipelineRasterizationStateCreateInfo r_info = {0};
  VkPipelineMultisampleStateCreateInfo ms_info = {0};
  VkPipelineColorBlendAttachmentState cb_state = {0};
  VkPipelineColorBlendStateCreateInfo cb_info = {0};
  VkGraphicsPipelineCreateInfo p_info = {0};

  VkViewport viewport = {0};
  VkRect2D scissor = {0};

  VkPipelineLayout p_layout = VK_NULL_HANDLE;
  VkPipeline output_pipeline = VK_NULL_HANDLE;

  {
    d_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    d_info.dynamicStateCount = sizeof(dynamic_states) / sizeof(*dynamic_states);
    d_info.pDynamicStates = dynamic_states;
  }

  {
    v_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    v_info.vertexBindingDescriptionCount = 0;
    v_info.pVertexBindingDescriptions = NULL;
    v_info.vertexAttributeDescriptionCount = 0;
    v_info.pVertexAttributeDescriptions = NULL;
  }

  {
    a_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    a_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    a_info.primitiveRestartEnable = VK_FALSE;
  }

  {
    viewport.x = 0.0f;
    viewport.y = 0.0f;

    viewport.width = lgpu->current_swapchain.swapchain_extent.width;
    viewport.height = lgpu->current_swapchain.swapchain_extent.height;

    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
  }

  {
    scissor.offset.x = 0;
    scissor.offset.y = 0;

    scissor.extent = lgpu->current_swapchain.swapchain_extent;
  }

  {
    vs_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

    vs_info.viewportCount = 1;
    // vs_info.pViewports = &viewport; // not set because of dynamic mode

    vs_info.scissorCount = 1;
    // vs_info.pScissors = &scissor; // not set because of dynamic mode
  }

  {
    r_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

    r_info.depthClampEnable = VK_FALSE;
    r_info.rasterizerDiscardEnable = VK_FALSE;

    r_info.polygonMode = VK_POLYGON_MODE_FILL;
    r_info.lineWidth = 1.0f;

    r_info.cullMode = VK_CULL_MODE_BACK_BIT;
    r_info.frontFace = VK_FRONT_FACE_CLOCKWISE;

    r_info.depthBiasEnable = VK_FALSE;
    r_info.depthBiasConstantFactor = 0.0f;
    r_info.depthBiasClamp = 0.0f;
    r_info.depthBiasSlopeFactor = 0.0f;
  }

  // TODO: multisampling
  {
    ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms_info.sampleShadingEnable = VK_FALSE;
    ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    ms_info.minSampleShading = 1.0f;
    ms_info.pSampleMask = NULL;
    ms_info.alphaToCoverageEnable = VK_FALSE;
    ms_info.alphaToOneEnable = VK_FALSE;
  }

  {
    cb_state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb_state.blendEnable = VK_FALSE;

    cb_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    cb_state.colorBlendOp = VK_BLEND_OP_ADD;

    cb_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cb_state.alphaBlendOp = VK_BLEND_OP_ADD;
  }

  {
    cb_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb_info.logicOpEnable = VK_FALSE;
    cb_info.logicOp = VK_LOGIC_OP_COPY;
    cb_info.attachmentCount = 1;
    cb_info.pAttachments = &cb_state;
    cb_info.blendConstants[0] = 0.0f;
    cb_info.blendConstants[1] = 0.0f;
    cb_info.blendConstants[2] = 0.0f;
    cb_info.blendConstants[3] = 0.0f;
  }

  {
    VkPipelineLayout old_layout = p->layout;

    VkPipelineLayoutCreateInfo pl_info = {0};
    pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_info.setLayoutCount = 0;
    pl_info.pSetLayouts = NULL;
    pl_info.pushConstantRangeCount = 0;
    pl_info.pPushConstantRanges = NULL;

    int result = vkCreatePipelineLayout(lgpu->gpu, &pl_info, NULL, &p_layout);

    if (VK_SUCCESS != result) {
      return -3;
    }

    if (VK_NULL_HANDLE != p->layout)
      vkDestroyPipelineLayout(lgpu->gpu, p->layout, NULL);

    p->layout = p_layout;
  }

  {
    p_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    p_info.stageCount = 2;
    p_info.pStages = stage_infos;

    p_info.pVertexInputState = &v_info;
    p_info.pInputAssemblyState = &a_info;
    p_info.pViewportState = &vs_info;
    p_info.pRasterizationState = &r_info;
    p_info.pMultisampleState = &ms_info;
    p_info.pDepthStencilState = NULL;
    p_info.pColorBlendState = &cb_info;
    p_info.pDynamicState = &d_info;

    p_info.layout = p->layout;

    p_info.renderPass = lgpu->render_passes[indices->render_pass];
    p_info.subpass = 0;

    p_info.basePipelineHandle = VK_NULL_HANDLE;
    p_info.basePipelineIndex = -1;
  }

  {
    int success = vkCreateGraphicsPipelines(lgpu->gpu, VK_NULL_HANDLE, 1,
                                            &p_info, NULL, &output_pipeline);

    if (VK_SUCCESS != success) {
      return -4;
    }
  }

  p->vertex_count = vertices;
  p->pipeline = output_pipeline;

#define DESTROY_IF_EXISTS(mod)                                                 \
  if (VK_NULL_HANDLE != mod)                                                   \
  vkDestroyShaderModule(lgpu->gpu, mod, NULL)

  // DESTROY_IF_EXISTS(set->input);
  DESTROY_IF_EXISTS(set->vertex);
  DESTROY_IF_EXISTS(set->tesselation);
  DESTROY_IF_EXISTS(set->geometry);
  // DESTROY_IF_EXISTS(set->rasterization);
  DESTROY_IF_EXISTS(set->fragment);
  // DESTROY_IF_EXISTS(set->blending);

#undef DESTROY_IF_EXISTS

  return 0;
}

int create_framebuffers(vulkan_context *ctx, struct indices *indices) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];
  struct active_swapchain *sc = &lgpu->current_swapchain;

  for (int i = 0; i < sc->frame_count; ++i) {
    VkFramebufferCreateInfo fb_info = {0};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = lgpu->render_passes[indices->render_pass];
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &sc->frames[i].view;
    fb_info.width = sc->swapchain_extent.width;
    fb_info.height = sc->swapchain_extent.height;
    fb_info.layers = 1;

    {
      int success = vkCreateFramebuffer(lgpu->gpu, &fb_info, NULL,
                                        &sc->frames[i].framebuffer);

      if (VK_SUCCESS != success)
        return -1;
    }
  }

  return 0;
}

int create_command_pool(vulkan_context *ctx, struct indices *indices) {
  VkCommandPool pool = VK_NULL_HANDLE;

  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  VkCommandPoolCreateInfo p_info = {0};
  p_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  p_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  p_info.queueFamilyIndex = ctx->logical_gpus[indices->logical_gpu].render_qf;

  {
    int success = vkCreateCommandPool(lgpu->gpu, &p_info, NULL, &pool);

    if (VK_SUCCESS != success)
      return -1;
  }

  lgpu->command_pool = pool;

  return 0;
}

int create_command_buffers(vulkan_context *ctx, struct indices *indices,
                           size_t amount) {
  VkCommandBuffer *buffers = NULL;

  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  buffers = (VkCommandBuffer *)malloc(amount * sizeof(VkCommandBuffer));
  if (NULL == buffers) {
    perror("malloc()");
    return -1;
  }

  VkCommandBufferAllocateInfo b_alloc = {0};
  b_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  b_alloc.commandPool = lgpu->command_pool;
  b_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  b_alloc.commandBufferCount = amount;

  {
    int success = vkAllocateCommandBuffers(lgpu->gpu, &b_alloc, buffers);

    if (VK_SUCCESS != success)
      return -2;
  }

  FREE_SAFE(lgpu->command_buffers);
  lgpu->command_buffers = buffers;
  lgpu->command_buffer_count = amount;

  return 0;
}

int record_command_buffer(vulkan_context *ctx, struct indices *indices,
                          uint8_t *pipeline_indices, size_t pipeline_count) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  VkCommandBufferBeginInfo b_info = {0};
  b_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  b_info.flags = 0;
  b_info.pInheritanceInfo = NULL;

  {
    int success = vkBeginCommandBuffer(
        lgpu->command_buffers[indices->command_buffer], &b_info);
  }

  VkRenderPassBeginInfo r_info = {0};
  r_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  r_info.renderPass = lgpu->render_passes[indices->render_pass];
  r_info.framebuffer =
      lgpu->current_swapchain.frames[indices->swapchain_frame].framebuffer;

  r_info.renderArea.extent = lgpu->current_swapchain.swapchain_extent;
  r_info.renderArea.offset.x = 0;
  r_info.renderArea.offset.y = 0;

  VkClearValue clear = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  r_info.clearValueCount = 1;
  r_info.pClearValues = &clear;

  VkCommandBuffer *cb = &lgpu->command_buffers[indices->command_buffer];

  vkCmdBeginRenderPass(*cb, &r_info, VK_SUBPASS_CONTENTS_INLINE);

  {
    VkViewport vp = {0};
    vp.x = 0.0f;
    vp.y = 0.0f;

    vp.width = (float)lgpu->current_swapchain.swapchain_extent.width;
    vp.height = (float)lgpu->current_swapchain.swapchain_extent.height;

    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(*cb, 0, 1, &vp);

    VkRect2D scissor = {0};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = lgpu->current_swapchain.swapchain_extent;
    vkCmdSetScissor(*cb, 0, 1, &scissor);
  }

  for (int i = 0; i < pipeline_count; ++i) {
    vkCmdBindPipeline(*cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      lgpu->pipelines[pipeline_indices[i]].pipeline);

    // TODO: fix hard-coded stuff
    vkCmdDraw(*cb, lgpu->pipelines[pipeline_indices[i]].vertex_count, 1, 0, 0);
  }

  vkCmdEndRenderPass(*cb);

  {
    int success = vkEndCommandBuffer(*cb);
    if (VK_SUCCESS != success)
      return -1;
  }

  return 0;
}

int submit_command_buffer(vulkan_context *ctx, struct indices *indices) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  VkSubmitInfo s_info = {0};
  s_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkPipelineStageFlags sf = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  s_info.waitSemaphoreCount = 1;
  s_info.pWaitSemaphores = &lgpu->current_swapchain.aquire_semaphore;
  s_info.pWaitDstStageMask = &sf;

  s_info.commandBufferCount = 1;
  s_info.pCommandBuffers = &lgpu->command_buffers[indices->command_buffer];

  s_info.signalSemaphoreCount = 1;
  s_info.pSignalSemaphores =
      &lgpu->current_swapchain.frames[indices->swapchain_frame].render_finished;

  {
    int success =
        vkQueueSubmit(lgpu->render_queues[indices->render_queue], 1, &s_info,
                      lgpu->current_swapchain.in_flight_fence);

    if (VK_SUCCESS != success) {
#ifdef DEBUG
      fprintf(stderr, "Could not submit draw command\n");
#endif
      return -1;
    }
  }

  return 0;
}

int present_swap_frame(vulkan_context *ctx, struct indices *indices) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  {
    uint32_t img_idx = indices->swapchain_frame;
    VkPresentInfoKHR p_info = {0};
    p_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    p_info.waitSemaphoreCount = 1;
    p_info.pWaitSemaphores =
        &lgpu->current_swapchain.frames[img_idx].render_finished;

    p_info.swapchainCount = 1;
    p_info.pSwapchains = &lgpu->current_swapchain.swapchain;
    p_info.pImageIndices = &img_idx;

    p_info.pResults = NULL;

    vkQueuePresentKHR(lgpu->presentation_queues[indices->present_queue],
                      &p_info);
  }

  return 0;
}

void draw_frame(window_context *w_ctx, struct indices *indices,
                uint8_t *pipeline_indices, size_t pipeline_count) {
  // TODO: something with multiple in-flight frames. idk shit's weird bcs the
  // tutorial is outdated asf
  // https://vulkan-tutorial.com/en/Drawing_a_triangle/Drawing/Frames_in_flight

  uint32_t image_index = UINT32_MAX;

  vulkan_context *ctx = w_ctx->vk_context;
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  vkWaitForFences(lgpu->gpu, 1, &lgpu->current_swapchain.in_flight_fence,
                  VK_TRUE, UINT64_MAX);

  {
    VkResult success = vkAcquireNextImageKHR(
        lgpu->gpu, lgpu->current_swapchain.swapchain, UINT64_MAX,
        lgpu->current_swapchain.aquire_semaphore, VK_NULL_HANDLE, &image_index);

    if (VK_ERROR_OUT_OF_DATE_KHR == success || VK_SUBOPTIMAL_KHR == success) {
      struct swapchain_details new_details =
          swapchain_compatibility(ctx, ctx->physical_gpu);

      refresh_vulkan_swap_chain(w_ctx, &new_details, indices);

      return;
    }
  }

  vkResetFences(lgpu->gpu, 1, &lgpu->current_swapchain.in_flight_fence);

  if (UINT32_MAX == image_index) {
    // uhhhh
#ifdef DEBUG
    fprintf(stderr, "Failed to retrieve swapchain image index\n");
#endif
    return;
  }

  indices->swapchain_frame = image_index;

  vkResetCommandBuffer(lgpu->command_buffers[indices->command_buffer], 0);

  record_command_buffer(ctx, indices, pipeline_indices, pipeline_count);

  if (0 > submit_command_buffer(ctx, indices))
    return;

  {
    VkSemaphore temp = lgpu->current_swapchain.frames[indices->swapchain_frame]
                           .write_available;

    lgpu->current_swapchain.frames[indices->swapchain_frame].write_available =
        lgpu->current_swapchain.aquire_semaphore;

    lgpu->current_swapchain.aquire_semaphore = temp;
  }

  present_swap_frame(ctx, indices);

  return;
}
