#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cglm/mat4.h>
#include <cglm/vec4.h>

#define TRUE 1
#define FALSE 0

#include "vk.h"

#define ABS(x) ((x < 0) ? (x * -1) : (x))
#define MAX(x, y) ((x > y) ? x : y)
#define MIN(x, y) ((x < y) ? x : y)
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

  for (size_t i = 0; i < layer_count; ++i) {
    uint8_t found = FALSE;

    for (uint32_t j = 0; j < available; ++j) {
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

  for (size_t i = 0; i < extension_count; ++i) {
    uint8_t found = FALSE;

    for (uint32_t j = 0; j < available; ++j) {
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

static void glfw_resize_callback(GLFWwindow *window, int width, int height) {
  window_context *w_ctx = (window_context *)glfwGetWindowUserPointer(window);

  if (NULL == w_ctx) {
#ifdef DEBUG
    fprintf(stderr, "GLFW resize callback called, but could not retrieve "
                    "matching window_context\n");
#endif
    return;
  }

  w_ctx->window_dimensions[0] = (uint32_t)width;
  w_ctx->window_dimensions[1] = (uint32_t)height;

  w_ctx->resized = TRUE;
}

int create_vulkan_window(window_context *ctx) {
  if (VK_STRUCTURE_TYPE_APPLICATION_INFO != ctx->vk_context->app_info.sType ||
      NULL == ctx->window_title || 1 > ctx->window_dimensions[0] ||
      1 > ctx->window_dimensions[1])
    return -99;

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  const char **val_layers = ctx->vk_context->validation_layers;

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
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  ctx->glfw_window =
      glfwCreateWindow(ctx->window_dimensions[0], ctx->window_dimensions[1],
                       ctx->window_title, NULL, NULL);

  glfwSetWindowUserPointer(ctx->glfw_window, ctx);
  glfwSetFramebufferSizeCallback(ctx->glfw_window, glfw_resize_callback);

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
    for (size_t i = 0; i < sc->frame_count; ++i) {
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

static void destroy_lgpu(vulkan_context *vk_ctx, size_t lgpu_index) {

  struct indices in = {0};
  in.logical_gpu = lgpu_index;
  struct logical_gpu_info *lgpu = &vk_ctx->logical_gpus[lgpu_index];
  struct active_swapchain *sc = &lgpu->current_swapchain;

  vkDeviceWaitIdle(lgpu->gpu);

#ifdef DEBUG
  fprintf(stderr, "Starting destruction of a logical device\n");
#endif

  if (NULL != lgpu->command_pools) {
    for (size_t i = 0; i < lgpu->command_pool_capacity; ++i) {
      if (VK_NULL_HANDLE != lgpu->command_pools[i].pool)
        vkDestroyCommandPool(lgpu->gpu, lgpu->command_pools[i].pool, NULL);

      FREE_SAFE(lgpu->command_pools[i].buffers);
    }
  }
  FREE_SAFE(lgpu->command_pools);

  clean_swapchain(lgpu);

  if (VK_NULL_HANDLE != sc->swapchain)
    vkDestroySwapchainKHR(lgpu->gpu, sc->swapchain, NULL);

#ifdef DEBUG
  fprintf(stderr, " Swapchain destroyed\n");
#endif

  if (NULL != lgpu->render_passes)
    for (size_t i = 0; i < lgpu->render_pass_capacity; ++i) {
      if (VK_NULL_HANDLE != lgpu->render_passes[i])
        vkDestroyRenderPass(lgpu->gpu, lgpu->render_passes[i], NULL);
    }

  FREE_SAFE(lgpu->render_passes);

#ifdef DEBUG
  fprintf(stderr, " all render passes destroyed\n");
#endif

  for (size_t i = 0; i < lgpu->pipeline_capacity; ++i) {
    struct pipeline *p = &lgpu->pipelines[i];

    if (NULL != p->pipeline)
      vkDestroyPipeline(lgpu->gpu, p->pipeline, NULL);

    if (NULL != p->layout)
      vkDestroyPipelineLayout(lgpu->gpu, p->layout, NULL);
    ;

    // XXX: this could definitely cause problems later on, if the shapes are
    // supposed to be reused. it's probably fine tho? especially since we're
    // destroying the LGPU in this function anyway
    for (size_t i = 0; i < p->shape_count; ++i) {
      free_shape_buffer(vk_ctx, &in, &p->shapes[i]);
    }
    FREE_SAFE(p->shapes);
  }
  FREE_SAFE(lgpu->pipelines);

#ifdef DEBUG
  fprintf(stderr, " all pipelines destroyed\n");
#endif

  if (VK_NULL_HANDLE != lgpu->current_swapchain.aquire_semaphore) {
    vkDestroySemaphore(lgpu->gpu, lgpu->current_swapchain.aquire_semaphore,
                       NULL);
  }

  // fences
  if (VK_NULL_HANDLE != sc->in_flight_fence) {
    vkDestroyFence(lgpu->gpu, sc->in_flight_fence, NULL);
  }

#ifdef DEBUG
  fprintf(stderr, " remaining sync objects destroyed\n");
#endif

  vkDestroyDevice(lgpu->gpu, NULL);

#ifdef DEBUG
  fprintf(stderr, " logical device destroyed\n");
#endif

  memset(lgpu, 0, sizeof(*lgpu));
}

VkResult destroy_vulkan_window(window_context *ctx) {
  vulkan_context *vk = ctx->vk_context;

  for (size_t i = 0; i < vk->lg_count; ++i) {
    destroy_lgpu(vk, i);
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

    if (0 == gpus_available || VK_SUCCESS != success) {
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
  for (uint32_t i = 0; i < gpus_available; ++i) {
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

  case TRANSFER_ONLY:
    if (fam.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
      return FALSE;
    // else we fall through to case RENDER to see if other things match

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
    if (reqs->index_blacklist_bits & (1 << i))
      continue;

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
                           VkPhysicalDeviceFeatures features,
                           size_t render_queues, size_t presentation_queues,
                           size_t transfer_queues) {
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
  struct queue_family_info transfer_qf = {0};

  uint8_t render = FALSE;
  uint8_t present = FALSE;
  uint8_t transfer = FALSE;

  size_t render_transfer_overlap = 0;

  struct queue_family_requirements reqs;
  memset(&reqs, 0, sizeof(reqs));

  if (0 < transfer_queues) {
    // first we see if there are queue families that support TRANSFER but not
    // COMPUTE/GRAPHICS because if so, we can use a separate queue family for
    // TRANSFER operations so that we can always use a separate queue for that
    reqs.type = TRANSFER_ONLY;
    reqs.minimum_queues = transfer_queues;
    reqs.reqs.render.qf_flags = VK_QUEUE_TRANSFER_BIT;
    transfer_qf = vulkan_queue_family(ctx, &reqs);

    memset(&reqs, 0, sizeof(reqs));
  }

  if (0 < render_queues) {
    reqs.type = RENDER;
    reqs.minimum_queues =
        CONDITIONAL(0 > transfer_qf.index, MAX(render_queues, transfer_queues),
                    render_queues);
    reqs.reqs.render.qf_flags = VK_QUEUE_GRAPHICS_BIT;
    render_qf = vulkan_queue_family(ctx, &reqs);

    if (0 > transfer_qf.index)
      render_transfer_overlap =
          (render_queues + transfer_queues -
           MIN(render_queues + transfer_queues, render_qf.family.queueCount));

    memset(&reqs, 0, sizeof(reqs));

    if (0 > render_qf.index)
      return -3;
  }

  if (0 < presentation_queues) {
    reqs.type = PRESENTATION;
    reqs.minimum_queues = presentation_queues;
    reqs.reqs.present.gpu = ctx->physical_gpu;
    reqs.reqs.present.surface = ctx->vk_surface;
    presentation_qf = vulkan_queue_family(ctx, &reqs);

    if ((render_qf.index == presentation_qf.index) &&
        (render_queues + presentation_queues > render_qf.family.queueCount)) {
      // reroll presentation_qf
      reqs.index_blacklist_bits = (1 << render_qf.index);
      presentation_qf = vulkan_queue_family(ctx, &reqs);
    }

    if (0 > presentation_qf.index)
      return -3;
  }

  if (0 > render_qf.index || 0 > presentation_qf.index)
    return -3;

  VkDeviceQueueCreateInfo infos[8] = {0};
  size_t infos_count = 0;

  lgpu.render_capacity = render_queues;
  lgpu.render_qf = render_qf.index;
  lgpu.presentation_capacity = presentation_queues;
  lgpu.presentation_qf = presentation_qf.index;
  lgpu.transfer_capacity = transfer_queues;
  lgpu.transfer_qf =
      CONDITIONAL(-1 < transfer_qf.index, transfer_qf.index, render_qf.index);

  // should always be true. i guess we can still check
  if (-1 < render_qf.index) {
    ++infos_count;

    VkDeviceQueueCreateInfo *r_info = &infos[infos_count - 1];
    r_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    r_info->queueFamilyIndex = render_qf.index;
    r_info->queueCount = render_queues;
    r_info->pQueuePriorities = &priority;

    if (presentation_qf.index == render_qf.index) {
      r_info->queueCount += presentation_queues;
      present = TRUE;
    }

    render = TRUE;
  }

  if (-1 < presentation_qf.index && render_qf.index != presentation_qf.index) {
    ++infos_count;

    VkDeviceQueueCreateInfo *p_info = &infos[infos_count - 1];
    p_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    p_info->queueFamilyIndex = presentation_qf.index;
    p_info->queueCount = presentation_queues;
    p_info->pQueuePriorities = &priority;

    present = TRUE;
  }

  if (-1 < transfer_qf.index) {
    ++infos_count;

    VkDeviceQueueCreateInfo *t_info = &infos[infos_count - 1];
    t_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    t_info->queueFamilyIndex = transfer_qf.index;
    t_info->queueCount = transfer_queues;
    t_info->pQueuePriorities = &priority;

    transfer = TRUE;

  } else if (0 < transfer_queues &&
             render_transfer_overlap != transfer_queues) {
    ++infos_count;

    VkDeviceQueueCreateInfo *t_info = &infos[infos_count - 1];
    t_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    t_info->queueFamilyIndex = render_qf.index;
    t_info->queueCount = transfer_queues - render_transfer_overlap;
    t_info->pQueuePriorities = &priority;

    transfer = TRUE;
  }

#ifdef DEBUG
  fprintf(stderr, " Selected queue family %d for rendering (%lu queue%s)\n",
          lgpu.render_qf, render_queues, (render_queues != 1) ? "s" : "");

  fprintf(stderr, " Selected queue family %d for presenting (%lu queue%s)\n",
          lgpu.presentation_qf, presentation_queues,
          (presentation_queues != 1) ? "s" : "");

  fprintf(stderr, " Selected queue family %d for transfering (%lu queue%s)\n",
          lgpu.transfer_qf, transfer_queues, (transfer_queues != 1) ? "s" : "");

  if (render_transfer_overlap > 0)
    fprintf(stderr,
            "  (%lu overlapping queue%s between rendering and transfering "
            "queues)\n",
            render_transfer_overlap, (render_transfer_overlap != 1) ? "s" : "");

  for (size_t i = 0; i < infos_count; ++i) {
    fprintf(stderr, " Requesting %u queues from queue family %u\n",
            infos[i].queueCount, infos[i].queueFamilyIndex);
  }
#endif

  lgpu.render_transfer_overlap = render_transfer_overlap;

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

  if (FALSE != render && 0 < lgpu.render_capacity) {
    lgpu.render_queues =
        (VkQueue *)calloc(lgpu.render_capacity, sizeof(VkQueue));
    if (NULL == lgpu.render_queues) {
      perror("calloc()");
      return -4;
    }
  }

  if (FALSE != present && 0 < lgpu.presentation_capacity) {
    lgpu.presentation_queues =
        (VkQueue *)calloc(lgpu.presentation_capacity, sizeof(VkQueue));
    if (NULL == lgpu.presentation_queues) {
      perror("calloc()");
      FREE_SAFE(lgpu.render_queues);
      return -4;
    }
  }

  if ((FALSE != transfer ||
       (lgpu.render_qf == lgpu.transfer_qf && FALSE != render)) &&
      0 < lgpu.transfer_capacity) {
    lgpu.transfer_queues =
        (VkQueue *)calloc(lgpu.transfer_capacity, sizeof(VkQueue));
    if (NULL == lgpu.transfer_queues) {
      perror("calloc()");
      FREE_SAFE(lgpu.render_queues);
      FREE_SAFE(lgpu.presentation_queues);
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
    FREE_SAFE(lgpu.transfer_queues);

    return -5;
  }

  lgpu.features = features;
  ctx->logical_gpus[ctx->lg_count] = lgpu;

  return ctx->lg_count++;
}

int new_vulkan_queue(vulkan_context *vk_ctx, const struct indices *indices,
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

  case TRANSFER_ONLY:
    capacity = &lgpu->transfer_capacity;
    count = &lgpu->transfer_count;
    queues = lgpu->transfer_queues;
    qf_index = &lgpu->transfer_qf;
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
  uint32_t formats_available = 0;
  VkSurfaceFormatKHR *formats = NULL;

  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, sfc, &formats_available, NULL);

  if (0 == formats_available)
    return NULL;

  formats = (VkSurfaceFormatKHR *)calloc(formats_available,
                                         sizeof(VkSurfaceFormatKHR));

  if (NULL == formats) {
    perror("calloc()");
    return NULL;
  }

  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, sfc, &formats_available, formats);

  if (0 == fmts_count) {
    *output = formats[0];
    FREE_SAFE(formats);
    return output;
  }

  for (size_t i = 0; i < fmts_count; ++i) {
    for (uint32_t j = 0; j < formats_available; ++j) {
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
  uint32_t modes_available = 0;
  VkPresentModeKHR *modes = NULL;

  vkGetPhysicalDeviceSurfacePresentModesKHR(dev, sfc, &modes_available, NULL);

  if (0 == modes_available)
    return NULL;

  modes = (VkPresentModeKHR *)calloc(modes_available, sizeof(VkPresentModeKHR));

  if (NULL == modes) {
    perror("calloc()");
    return NULL;
  }

  vkGetPhysicalDeviceSurfacePresentModesKHR(dev, sfc, &modes_available, modes);

  if (0 == md_count) {
    *output = modes[0];
    FREE_SAFE(modes);
    return output;
  }

  for (size_t i = 0; i < md_count; ++i) {
    for (uint32_t j = 0; j < modes_available; ++j) {
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
                             const struct indices *indices) {
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

  for (uint32_t i = 0; i < frame_count; ++i) {
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

    for (uint32_t i = 0; i < frame_count; ++i) {
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
                              const struct indices *indices) {
  struct logical_gpu_info *lgpu =
      &w_ctx->vk_context->logical_gpus[indices->logical_gpu];

  int width = 0, height = 0;
  glfwGetFramebufferSize(w_ctx->glfw_window, &width, &height);

  // if the window is currently minimized; pause until it is re-opened
  while (0 == width || 0 == height) {
    glfwGetFramebufferSize(w_ctx->glfw_window, &width, &height);
    glfwWaitEvents();
  }

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

  if (4 > size)
    return file;

  rewind(fp);

  uint32_t magic = 0;
  fread(&magic, sizeof(uint32_t), 1, fp);
  if (0x07230203 != magic) {
    // bad file format (probably)
    // also add more checking, bcs this isn't good enough lol
    return file;
  }

  rewind(fp);

#ifdef DEBUG
  fprintf(stderr, "Read %d bytes from file \"%s\"\n", size, filename);
#endif

  // align to 4 bytes (sizeof uint32_t) because the shader module reads it using
  // a uint32_t pointer for some reason
  file.buffer =
      (uint8_t *)malloc(size + (sizeof(uint32_t) - (size % sizeof(uint32_t))));
  if (NULL == file.buffer) {
    perror("malloc()");
    fclose(fp);
    return file;
  }

  if (size > (int)fread(file.buffer, 1, size, fp)) {
#ifdef DEBUG
    fprintf(stderr, "Read too little from SPIR-V file\n");
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
  case TESSELATION_CONTROL:
    module = &set->tesselation_control;
    break;
  case TESSELATION_EVALUATION:
    module = &set->tesselation_evaluation;
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

int fill_shader_module(vulkan_context *ctx, const struct indices *indices,
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

VkRenderPass create_render_pass(vulkan_context *ctx,
                                const struct indices *indices) {
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

int allocate_render_passes(vulkan_context *ctx, const struct indices *indices,
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

  if (lgpu->render_pass_capacity < amount)
    memset(&lgpu->render_passes[lgpu->render_pass_capacity], 0,
           sizeof(lgpu->render_passes[0]) *
               (amount - lgpu->render_pass_capacity));

  lgpu->render_pass_capacity = amount;

  return 0;
}

int add_render_pass(vulkan_context *ctx, const struct indices *indices,
                    VkRenderPass pass) {
  ctx->logical_gpus[indices->logical_gpu].render_passes[indices->render_pass] =
      pass;

  return 0;
}

int init_vertices_struct(struct vertex_bundle *vertices) {
  memset(vertices, 0, sizeof(*vertices));

  return 0;
}

int allocate_vertices(struct vertex_bundle *vertices, size_t amount) {
  if (1 > amount) {
    FREE_SAFE(vertices->vertices);
    return 0;
  }

  if (vertices->vertex_capacity == amount)
    return -1;

  struct vertex *new_verts = (struct vertex *)realloc(
      vertices->vertices, amount * sizeof(struct vertex));
  if (NULL == new_verts) {
    perror("realloc()");
    return -2;
  }

  if (vertices->vertex_capacity < amount)
    memset(&new_verts[vertices->vertex_capacity], 0,
           sizeof(*new_verts) * (amount - vertices->vertex_capacity));

  vertices->vertices = new_verts;
  vertices->vertex_capacity = amount;

  return 0;
}

void free_vertices(struct vertex_bundle *v) {
  FREE_SAFE(v->vertices);
  FREE_SAFE(v->indices);

  memset(v, 0, sizeof(*v));

  return;
}

int append_vertex(struct vertex_bundle *vertices, struct vertex v) {
  if (vertices->vertex_count == vertices->vertex_capacity)
    return -1;

  vertices->vertices[vertices->vertex_count] = v;

  return (vertices->vertex_count++);
}

int allocate_pipelines(vulkan_context *ctx, const struct indices *indices,
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

  if (lgpu->pipeline_capacity < amount)
    memset(&lgpu->pipelines[lgpu->pipeline_capacity], 0,
           sizeof(lgpu->pipelines[0]) * (amount - lgpu->pipeline_capacity));

  lgpu->pipeline_capacity = amount;

  return 0;
}

struct buffer_mem_pair {
  VkBuffer buffer;
  VkDeviceSize buffer_size;
  VkDeviceMemory mem;
};

static void new_buffer(VkPhysicalDevice dev, struct logical_gpu_info *lgpu,
                       size_t size, VkBufferUsageFlags usage_flags,
                       VkMemoryPropertyFlags mem_prop_flags,
                       struct buffer_mem_pair *output) {
  // TODO: allocator stuff
  // this tutorial page talks about it at the bottom of the page
  // https://vulkan-tutorial.com/en/Vertex_buffers/Staging_buffer

  VkBuffer new_buf = {0};
  VkDeviceMemory new_mem = {0};

  VkBufferCreateInfo b_info = {0};

  b_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  b_info.size = size;
  b_info.usage = usage_flags;
  b_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  uint32_t indices[2] = {lgpu->render_qf, lgpu->transfer_qf};
  if (-1 < lgpu->transfer_qf && lgpu->transfer_qf != lgpu->render_qf) {
    b_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
    b_info.queueFamilyIndexCount = 2;
    b_info.pQueueFamilyIndices = indices;
  }

  if (VK_SUCCESS != vkCreateBuffer(lgpu->gpu, &b_info, NULL, &new_buf)) {
    fprintf(stderr, "Could not create a buffer\n");
    return;
  }

  VkMemoryRequirements mem_reqs = {0};
  vkGetBufferMemoryRequirements(lgpu->gpu, new_buf, &mem_reqs);

  VkPhysicalDeviceMemoryProperties mem_props = {0};
  vkGetPhysicalDeviceMemoryProperties(dev, &mem_props);

  int idx = -1;
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
    if ((mem_reqs.memoryTypeBits & (1 << i)) &&
        (mem_props.memoryTypes[i].propertyFlags & mem_prop_flags) ==
            mem_prop_flags)
      idx = i;
  }

  if (0 > idx) {
    // error
    fprintf(stderr, "Could not find valid memory type\n");
    return;
  }

  VkMemoryAllocateInfo m_info = {0};
  m_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  m_info.allocationSize = mem_reqs.size;
  m_info.memoryTypeIndex = idx;

  if (VK_SUCCESS != vkAllocateMemory(lgpu->gpu, &m_info, NULL, &new_mem)) {
    vkDestroyBuffer(lgpu->gpu, new_buf, NULL);

    fprintf(stderr, "Could not allocate buffer memory\n");

    return;
  }

  if (VK_SUCCESS != vkBindBufferMemory(lgpu->gpu, new_buf, new_mem, 0)) {
    // error
    vkFreeMemory(lgpu->gpu, new_mem, NULL);
    vkDestroyBuffer(lgpu->gpu, new_buf, NULL);

    fprintf(stderr, "Could not bind buffer memory\n");

    return;
  }

  output->buffer = new_buf;
  output->buffer_size = b_info.size;

  output->mem = new_mem;

  return;
}

static int data_to_buffer(struct logical_gpu_info *lgpu,
                          struct buffer_mem_pair *bm, void *to_copy) {
  // TODO: revisit this funtion after reading this again:
  // https://vulkan-tutorial.com/en/Vertex_buffers/Vertex_buffer_creation

  // TODO: before performing any action:
  // check if the amount of vertices in v->vertices (in bytes) is at least the
  // same size (or bigger) than bm->buffer_size

  // this memory-mapping function stores the mapped address into the 'data'
  // variable
  void *data = NULL;
  if (VK_SUCCESS !=
      vkMapMemory(lgpu->gpu, bm->mem, 0, bm->buffer_size, 0, &data)) {
    return -1;
  }

  memcpy(data, to_copy, bm->buffer_size);

  vkUnmapMemory(lgpu->gpu, bm->mem);
  data = NULL;

  return 0;
}

static void vulkan_bufcopy(VkDevice lgpu, VkQueue transfer_queue, VkBuffer src,
                           VkBuffer dst, VkDeviceSize size,
                           VkCommandPool command_pool) {
  VkCommandBufferAllocateInfo b_info = {0};
  b_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  b_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  b_info.commandPool = command_pool;
  b_info.commandBufferCount = 1;

  VkCommandBuffer cbuffer = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(lgpu, &b_info, &cbuffer);

  VkCommandBufferBeginInfo begin = {0};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(cbuffer, &begin);

  VkBufferCopy region = {0};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = size;

  vkCmdCopyBuffer(cbuffer, src, dst, 1, &region);

  vkEndCommandBuffer(cbuffer);

  VkSubmitInfo s_info = {0};
  s_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  s_info.commandBufferCount = 1;
  s_info.pCommandBuffers = &cbuffer;

  vkQueueSubmit(transfer_queue, 1, &s_info, VK_NULL_HANDLE);
  vkQueueWaitIdle(transfer_queue);

  vkFreeCommandBuffers(lgpu, command_pool, 1, &cbuffer);

  return;
}

static struct buffer_mem_pair new_vertex_buffer(VkPhysicalDevice dev,
                                                VkQueue transfer_queue,
                                                struct logical_gpu_info *lgpu,
                                                const struct vertex_bundle *v) {
  struct buffer_mem_pair staging_buffer = {0};
  struct buffer_mem_pair vertex_buffer = {0};

  size_t buffer_size = v->vertex_count * sizeof(v->vertices[0]);

  VkCommandPool transfer_pool = VK_NULL_HANDLE;
  for (size_t i = 0; i < lgpu->command_pool_capacity; ++i) {
    if (lgpu->command_pools[i].type == TRANSFER_POOL)
      transfer_pool = lgpu->command_pools[i].pool;
  }

  if (VK_NULL_HANDLE == transfer_pool)
    return vertex_buffer;

  new_buffer(dev, lgpu, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
             &staging_buffer);

  data_to_buffer(lgpu, &staging_buffer, v->vertices);

  new_buffer(dev, lgpu, buffer_size,
             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vertex_buffer);

  vulkan_bufcopy(lgpu->gpu, transfer_queue, staging_buffer.buffer,
                 vertex_buffer.buffer, buffer_size, transfer_pool);

  vkDestroyBuffer(lgpu->gpu, staging_buffer.buffer, NULL);
  vkFreeMemory(lgpu->gpu, staging_buffer.mem, NULL);

  return vertex_buffer;
}

static struct buffer_mem_pair new_index_buffer(VkPhysicalDevice dev,
                                               VkQueue transfer_queue,
                                               struct logical_gpu_info *lgpu,
                                               const struct vertex_bundle *v) {
  struct buffer_mem_pair staging_buffer = {0};
  struct buffer_mem_pair index_buffer = {0};

  size_t buffer_size = v->index_count * sizeof(v->indices[0]);

  VkCommandPool transfer_pool = VK_NULL_HANDLE;
  for (size_t i = 0; i < lgpu->command_pool_capacity; ++i) {
    if (lgpu->command_pools[i].type == TRANSFER_POOL)
      transfer_pool = lgpu->command_pools[i].pool;
  }

  if (VK_NULL_HANDLE == transfer_pool)
    return index_buffer;

  new_buffer(dev, lgpu, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
             &staging_buffer);

  data_to_buffer(lgpu, &staging_buffer, v->indices);

  new_buffer(dev, lgpu, buffer_size,
             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &index_buffer);

  vulkan_bufcopy(lgpu->gpu, transfer_queue, staging_buffer.buffer,
                 index_buffer.buffer, buffer_size, transfer_pool);

  vkDestroyBuffer(lgpu->gpu, staging_buffer.buffer, NULL);
  vkFreeMemory(lgpu->gpu, staging_buffer.mem, NULL);

  return index_buffer;
}

void shape_buffer_init(struct shape_buffer *shape) {
  memset(shape, 0, sizeof(*shape));
}

int shape_buffer_from_vertices(vulkan_context *ctx, struct indices *indices,
                               struct shape_buffer *output_shape,
                               struct vertex_bundle *input_bundle) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];
  struct buffer_mem_pair vb = {0};
  struct buffer_mem_pair ib = {0};

  if (1 > input_bundle->vertex_count || NULL == input_bundle->vertices)
    return -1;

  vb = new_vertex_buffer(ctx->physical_gpu,
                         lgpu->transfer_queues[indices->transfer_queue], lgpu,
                         input_bundle);

  if (VK_NULL_HANDLE == vb.buffer || VK_NULL_HANDLE == vb.mem) {
    return -2;
  }

  if (0 < input_bundle->index_count && NULL != input_bundle->indices) {
    ib = new_index_buffer(ctx->physical_gpu,
                          lgpu->transfer_queues[indices->transfer_queue], lgpu,
                          input_bundle);

    if (VK_NULL_HANDLE == ib.buffer || VK_NULL_HANDLE == ib.mem) {
      vkDestroyBuffer(lgpu->gpu, vb.buffer, NULL);
      vkFreeMemory(lgpu->gpu, vb.mem, NULL);

      return -2;
    }
  }

  output_shape->vertex_buffer = vb.buffer;
  output_shape->vertex_count = input_bundle->vertex_count;
  output_shape->vertex_buffer_memory = vb.mem;

  output_shape->index_buffer = ib.buffer;
  output_shape->index_count = input_bundle->index_count;
  output_shape->index_buffer_memory = ib.mem;

  return 0;
}

void free_shape_buffer(vulkan_context *ctx, struct indices *indices,
                       struct shape_buffer *shape) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  if (VK_NULL_HANDLE != shape->vertex_buffer)
    vkDestroyBuffer(lgpu->gpu, shape->vertex_buffer, NULL);

  if (VK_NULL_HANDLE != shape->vertex_buffer_memory)
    vkFreeMemory(lgpu->gpu, shape->vertex_buffer_memory, NULL);

  if (VK_NULL_HANDLE != shape->index_buffer)
    vkDestroyBuffer(lgpu->gpu, shape->index_buffer, NULL);

  if (VK_NULL_HANDLE != shape->index_buffer_memory)
    vkFreeMemory(lgpu->gpu, shape->index_buffer_memory, NULL);

  memset(shape, 0, sizeof(*shape));

  return;
}

int create_graphics_pipeline(vulkan_context *ctx, const struct indices *indices,
                             const struct spirv_file *spirvs,
                             size_t spirv_count, struct shader_set *set,
                             struct vertex_attributes *attr,
                             struct shape_buffer *shapes, size_t shape_count) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];
  struct pipeline *p = &lgpu->pipelines[indices->pipeline];

  NULL_CHECK(shapes, -1);

  // uint32_t imported_modules = 0;

  uint8_t infos_created = 0;
  VkPipelineShaderStageCreateInfo stage_infos[8] = {0};

  if (NULL != spirvs && 0 < spirv_count) {
    // populate shader_set struct
    for (size_t i = 0; i < spirv_count; ++i) {
      VkShaderModule *module = select_module_stage(set, spirvs[i].stage);
      if (VK_NULL_HANDLE != *module)
        continue;

      int success = fill_shader_module(ctx, indices, spirvs[i], set);

      if (0 != success) {
        // error happened
        return -2;
      }

      // ++imported_modules;
    }

#define PIPELINE_STAGE(mod, stage_bit)                                         \
  {                                                                            \
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,              \
    .stage = stage_bit, .module = mod, .pName = "main",                        \
  }

    if (VK_NULL_HANDLE != set->vertex) {
      VkPipelineShaderStageCreateInfo s_info =
          PIPELINE_STAGE(set->vertex, VK_SHADER_STAGE_VERTEX_BIT);

      stage_infos[infos_created++] = s_info;
    }

    if (VK_NULL_HANDLE != set->tesselation_control) {
      VkPipelineShaderStageCreateInfo s_info = PIPELINE_STAGE(
          set->tesselation_control, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);

      stage_infos[infos_created++] = s_info;
    }

    if (VK_NULL_HANDLE != set->tesselation_evaluation) {
      VkPipelineShaderStageCreateInfo s_info =
          PIPELINE_STAGE(set->tesselation_evaluation,
                         VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

      stage_infos[infos_created++] = s_info;
    }

    if (VK_NULL_HANDLE != set->geometry) {
      VkPipelineShaderStageCreateInfo s_info =
          PIPELINE_STAGE(set->geometry, VK_SHADER_STAGE_GEOMETRY_BIT);

      stage_infos[infos_created++] = s_info;
    }

    if (VK_NULL_HANDLE != set->fragment) {
      VkPipelineShaderStageCreateInfo s_info =
          PIPELINE_STAGE(set->fragment, VK_SHADER_STAGE_FRAGMENT_BIT);

      stage_infos[infos_created++] = s_info;
    }
#undef PIPELINE_STAGE
  }

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

    uint32_t b_count = 0;
    VkVertexInputBindingDescription *b = NULL;
    uint32_t a_count = 0;
    VkVertexInputAttributeDescription *a = NULL;

    if (NULL != attr) {
      if (NULL != attr->bindings && 0 < attr->binding_count) {
        b_count = attr->binding_count;
        b = attr->bindings;
      }

      if (NULL != attr->attributes && 0 < attr->attribute_count) {
        a_count = attr->attribute_count;
        a = attr->attributes;
      }
    }

    v_info.vertexBindingDescriptionCount = b_count;
    v_info.pVertexBindingDescriptions = b;

    v_info.vertexAttributeDescriptionCount = a_count;
    v_info.pVertexAttributeDescriptions = a;
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

    p_info.stageCount = infos_created;
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

  p->shapes = shapes;
  p->shape_count = shape_count;

  {
    int success = vkCreateGraphicsPipelines(lgpu->gpu, VK_NULL_HANDLE, 1,
                                            &p_info, NULL, &output_pipeline);

    if (VK_SUCCESS != success) {
      return -4;
    }
  }

  p->pipeline = output_pipeline;

#define DESTROY_IF_EXISTS(mod)                                                 \
  if (VK_NULL_HANDLE != mod)                                                   \
  vkDestroyShaderModule(lgpu->gpu, mod, NULL)

  // DESTROY_IF_EXISTS(set->input);
  DESTROY_IF_EXISTS(set->vertex);
  DESTROY_IF_EXISTS(set->tesselation_control);
  DESTROY_IF_EXISTS(set->tesselation_evaluation);
  DESTROY_IF_EXISTS(set->geometry);
  // DESTROY_IF_EXISTS(set->rasterization);
  DESTROY_IF_EXISTS(set->fragment);
  // DESTROY_IF_EXISTS(set->blending);

#undef DESTROY_IF_EXISTS

  return 0;
}

int create_framebuffers(vulkan_context *ctx, const struct indices *indices) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];
  struct active_swapchain *sc = &lgpu->current_swapchain;

  for (size_t i = 0; i < sc->frame_count; ++i) {
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

int allocate_command_pools(vulkan_context *ctx, const struct indices *indices,
                           size_t amount) {
  if (1 > amount)
    return -1;

  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  lgpu->command_pools = (struct command_pool *)realloc(
      lgpu->command_pools, amount * sizeof(struct command_pool));

  if (NULL == lgpu->command_pools) {
    perror("realloc()");
    return -2;
  }

  if (lgpu->command_pool_capacity < amount)
    memset(&lgpu->command_pools[lgpu->command_pool_capacity], 0,
           sizeof(lgpu->command_pools[0]) *
               (amount - lgpu->command_pool_capacity));

  lgpu->command_pool_capacity = amount;

  return 0;
}

int create_command_pool(vulkan_context *ctx, const struct indices *indices,
                        enum command_pool_type type) {
  struct command_pool pool = {0};

  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  if (1 > lgpu->command_pool_capacity)
    return -1;

  VkCommandPoolCreateInfo p_info = {0};
  p_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  switch (type) {
  case RENDER_POOL:
    p_info.queueFamilyIndex = ctx->logical_gpus[indices->logical_gpu].render_qf;
    p_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    break;

  case TRANSFER_POOL:
    p_info.queueFamilyIndex =
        ctx->logical_gpus[indices->logical_gpu].transfer_qf;
    p_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    break;
  }

  {
    int success = vkCreateCommandPool(lgpu->gpu, &p_info, NULL, &pool.pool);

    if (VK_SUCCESS != success)
      return -2;
  }

  pool.type = type;
  lgpu->command_pools[indices->command_pool] = pool;

  return 0;
}

int create_command_buffers(vulkan_context *ctx, const struct indices *indices,
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
  b_alloc.commandPool = lgpu->command_pools[indices->command_pool].pool;
  b_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  b_alloc.commandBufferCount = amount;

  {
    int success = vkAllocateCommandBuffers(lgpu->gpu, &b_alloc, buffers);

    if (VK_SUCCESS != success) {
      FREE_SAFE(buffers);
      return -2;
    }
  }

  FREE_SAFE(lgpu->command_pools[indices->command_pool].buffers);
  lgpu->command_pools[indices->command_pool].buffers = buffers;
  lgpu->command_pools[indices->command_pool].buffer_count = amount;

  return 0;
}

int record_command_buffer(vulkan_context *ctx, const struct indices *indices,
                          size_t pipeline_index) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  VkCommandBufferBeginInfo b_info = {0};
  b_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  b_info.flags = 0;
  b_info.pInheritanceInfo = NULL;

  {
    int success =
        vkBeginCommandBuffer(lgpu->command_pools[indices->command_pool]
                                 .buffers[indices->command_buffer],
                             &b_info);

    if (VK_SUCCESS != success) {
      // out of memory
      return -1;
    }
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

  VkCommandBuffer *cb = &lgpu->command_pools[indices->command_pool]
                             .buffers[indices->command_buffer];

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

  struct pipeline *p = &lgpu->pipelines[pipeline_index];
  vkCmdBindPipeline(*cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);

  for (size_t i = 0; i < p->shape_count; ++i) {

    // TODO: fix hard-coded stuff

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(*cb, 0, 1, &p->shapes[i].vertex_buffer, &offset);

    if (VK_NULL_HANDLE == p->shapes[i].index_buffer ||
        VK_NULL_HANDLE == p->shapes[i].index_buffer_memory) {
      // normal draw, using the given vertices
      // because there's no index buffer
      vkCmdDraw(*cb, p->shapes[i].vertex_count, 1, 0, 0);
    } else {
#ifdef DEBUG
      void *vertex_buffer = NULL;
      void *index_buffer = NULL;

      // debug mem-mapping
      // allows for reading GPU memory in a debugger
      vkMapMemory(lgpu->gpu, p->shapes[i].vertex_buffer_memory, 0,
                  p->shapes[i].vertex_count * sizeof(struct vertex), 0,
                  &vertex_buffer);
      vkMapMemory(lgpu->gpu, p->shapes[i].index_buffer_memory, 0,
                  p->shapes[i].index_count * sizeof(uint16_t), 0,
                  &index_buffer);

      vkUnmapMemory(lgpu->gpu, p->shapes[i].vertex_buffer_memory);
      vkUnmapMemory(lgpu->gpu, p->shapes[i].index_buffer_memory);
#endif

      // we have an index buffer
      vkCmdBindIndexBuffer(*cb, p->shapes[i].index_buffer, 0,
                           VK_INDEX_TYPE_UINT16);
      vkCmdDrawIndexed(*cb, p->shapes[i].index_count, 1, 0, 0, 0);
    }
  }

  vkCmdEndRenderPass(*cb);

  {
    int success = vkEndCommandBuffer(*cb);
    if (VK_SUCCESS != success)
      return -2;
  }

  return 0;
}

int submit_command_buffer(vulkan_context *ctx, const struct indices *indices) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

  VkSubmitInfo s_info = {0};
  s_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkPipelineStageFlags sf = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  s_info.waitSemaphoreCount = 1;
  s_info.pWaitSemaphores = &lgpu->current_swapchain.aquire_semaphore;
  s_info.pWaitDstStageMask = &sf;

  s_info.commandBufferCount = 1;
  s_info.pCommandBuffers = &lgpu->command_pools[indices->command_pool]
                                .buffers[indices->command_buffer];

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

int present_swap_frame(vulkan_context *ctx, const struct indices *indices) {
  struct logical_gpu_info *lgpu = &ctx->logical_gpus[indices->logical_gpu];

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

  return vkQueuePresentKHR(lgpu->presentation_queues[indices->present_queue],
                           &p_info);
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

    if (VK_ERROR_OUT_OF_DATE_KHR == success) {
      struct swapchain_details new_details =
          swapchain_compatibility(ctx, ctx->physical_gpu);

      refresh_vulkan_swap_chain(w_ctx, &new_details, indices);

      return;
    } else if (VK_SUCCESS != success && VK_SUBOPTIMAL_KHR != success) {
#ifdef DEBUG
      fprintf(stderr, "Could not retrieve next image in swap chain\n");
#endif
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

  // XXX: this will probs break when using more than 1 pipeline.
  // just got a feeling. problem for later tho
  for (size_t i = 0; i < pipeline_count; ++i) {
    vkResetCommandBuffer(lgpu->command_pools[indices->command_pool]
                             .buffers[indices->command_buffer],
                         0);

    record_command_buffer(ctx, indices, pipeline_indices[i]);

    if (0 > submit_command_buffer(ctx, indices))
      return;
  }

  {
    VkSemaphore temp = lgpu->current_swapchain.frames[indices->swapchain_frame]
                           .write_available;

    lgpu->current_swapchain.frames[indices->swapchain_frame].write_available =
        lgpu->current_swapchain.aquire_semaphore;

    lgpu->current_swapchain.aquire_semaphore = temp;
  }

  {
    int success = present_swap_frame(ctx, indices);

    if (VK_ERROR_OUT_OF_DATE_KHR == success || VK_SUBOPTIMAL_KHR == success ||
        FALSE != w_ctx->resized) {
      struct swapchain_details new_details =
          swapchain_compatibility(ctx, ctx->physical_gpu);

      refresh_vulkan_swap_chain(w_ctx, &new_details, indices);

      w_ctx->resized = FALSE;

      return;
    } else if (VK_SUCCESS != success) {
#ifdef DEBUG
      fprintf(stderr, "Could not present image\n");
#endif
      return;
    }
  }

  return;
}
