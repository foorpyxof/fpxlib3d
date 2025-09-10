/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include "debug.h"
#include "macros.h"
#include "volk/volk.h"

#include <string.h>
#include <unistd.h>

#include "vk/context.h"
#include "vk/pipeline.h"
#include "vk/renderpass.h"
#include "vk/typedefs.h"

#include "vk/logical_gpu.h"

extern void __fpx3d_vk_destroy_command_pool(Fpx3d_Vk_LogicalGpu *,
                                            Fpx3d_Vk_CommandPool *);

extern Fpx3d_E_Result __fpx3d_vk_destroy_swapchain(Fpx3d_Vk_LogicalGpu *,
                                                   Fpx3d_Vk_Swapchain *,
                                                   bool force);

extern Fpx3d_E_Result __fpx3d_realloc_array(void **, size_t obj_size,
                                            size_t amount,
                                            size_t *old_capacity);

// static declarations -----------------------------------------------
static Fpx3d_E_Result _find_queue_families(Fpx3d_Vk_Context *ctx,
                                           size_t g_queues, size_t p_queues,
                                           size_t t_queues,
                                           struct fpx3d_vk_qf_holder *output);
static Fpx3d_E_Result _construct_command_pool(Fpx3d_Vk_LogicalGpu *,
                                              Fpx3d_Vk_CommandPool *,
                                              Fpx3d_Vk_E_CommandPoolType);
static Fpx3d_E_Result _create_all_available_queues(Fpx3d_Vk_LogicalGpu *);
static Fpx3d_Vk_QueueFamily
_choose_queue_family(Fpx3d_Vk_Context *, Fpx3d_Vk_QueueFamilyRequirements *);
static Fpx3d_E_Result _create_queues(Fpx3d_Vk_LogicalGpu *,
                                     Fpx3d_Vk_E_QueueType, size_t count);
static bool _qf_meets_requirements(VkQueueFamilyProperties,
                                   Fpx3d_Vk_QueueFamilyRequirements *,
                                   size_t qf_index);
// end of static declarations ----------------------------------------

struct fpx3d_vulkan_queues *
__fpx3d_vk_get_queues_ptr_by_type(Fpx3d_Vk_LogicalGpu *lgpu,
                                  Fpx3d_Vk_E_QueueType type) {
  switch (type) {
  case GRAPHICS_QUEUE:
    return &lgpu->graphicsQueues;
    break;

  case PRESENT_QUEUE:
    return &lgpu->presentQueues;
    break;

  case TRANSFER_QUEUE:
    return &lgpu->transferQueues;
    break;

  default:
    return NULL;
  }
}

void __fpx3d_vk_destroy_lgpu(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(ctx, );
  NULL_CHECK(ctx, );
  NULL_CHECK(lgpu->handle, );

  vkDeviceWaitIdle(lgpu->handle);

  FPX3D_DEBUG("Starting destruction of a logical device");

  if (NULL != lgpu->commandPools) {
    for (size_t i = 0; i < lgpu->commandPoolCapacity; ++i) {
      fpx3d_vk_destroy_commandpool_at(lgpu, i);

      FREE_SAFE(lgpu->commandPools[i].buffers);
    }
  }
  FREE_SAFE(lgpu->commandPools);
  lgpu->commandPoolCapacity = 0;

  __fpx3d_vk_destroy_command_pool(lgpu, &lgpu->inFlightCommandPool);
  FREE_SAFE(lgpu->inFlightCommandPool.buffers);

  FPX3D_DEBUG(" - command pools destroyed");

  fpx3d_vk_destroy_current_swapchain(lgpu);

  for (Fpx3d_Vk_Swapchain *ptr = lgpu->oldSwapchainsList; NULL != ptr;) {
    Fpx3d_Vk_Swapchain *temp = ptr->nextInList;
    __fpx3d_vk_destroy_swapchain(lgpu, ptr, true);
    ptr = temp;
  }

  FPX3D_DEBUG(" - swapchains destroyed");

  if (NULL != lgpu->renderPasses)
    for (size_t i = 0; i < lgpu->renderPassCapacity; ++i)
      fpx3d_vk_destroy_renderpass_at(lgpu, i);

  FREE_SAFE(lgpu->renderPasses);
  lgpu->renderPassCapacity = 0;

  FPX3D_DEBUG(" - render passes destroyed");

  for (size_t i = 0; i < lgpu->pipelineCapacity; ++i) {
    fpx3d_vk_destroy_pipeline_at(lgpu, i, ctx);
  }
  FREE_SAFE(lgpu->pipelines);
  lgpu->pipelineCapacity = 0;

  FPX3D_DEBUG(" - all pipelines destroyed");

  for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
    vkDestroyFence(lgpu->handle, lgpu->inFlightFences[i], NULL);
  }
  FREE_SAFE(lgpu->inFlightFences);

  FPX3D_DEBUG(" - remaining sync objects destroyed");

  if (VK_NULL_HANDLE != lgpu->handle) {
    vkDestroyDevice(lgpu->handle, NULL);
  }

  FPX3D_DEBUG(" - logical device destroyed");

  memset(lgpu, 0, sizeof(*lgpu));

  return;
}

Fpx3d_E_Result fpx3d_vk_allocate_logicalgpus(Fpx3d_Vk_Context *ctx,
                                             size_t amount) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  return __fpx3d_realloc_array((void **)&ctx->logicalGpus,
                               sizeof(Fpx3d_Vk_LogicalGpu), amount,
                               &ctx->logicalGpuCapacity);
}

Fpx3d_E_Result fpx3d_vk_create_logicalgpu_at(Fpx3d_Vk_Context *ctx,
                                             size_t index,
                                             VkPhysicalDeviceFeatures features,
                                             size_t g_queues, size_t p_queues,
                                             size_t t_queues) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx->physicalGpu, FPX3D_VK_BAD_GPU_HANDLE_ERROR);
  NULL_CHECK(ctx->vkSurface, FPX3D_VK_BAD_VULKAN_INSTANCE_ERROR);
  NULL_CHECK(ctx->logicalGpus, FPX3D_NULLPTR_ERROR);

  if (ctx->logicalGpuCapacity <= index)
    return FPX3D_NO_CAPACITY_ERROR;

  Fpx3d_Vk_LogicalGpu new_lgpu = {0};
  uint32_t available_qf_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalGpu,
                                           &available_qf_count, NULL);

  if (1 > available_qf_count)
    return FPX3D_VK_ERROR; // TODO: make more specific errors

  struct fpx3d_vk_qf_holder qfs = {0};
  if (FPX3D_SUCCESS !=
      _find_queue_families(ctx, g_queues, p_queues, t_queues, &qfs))
    return FPX3D_VK_NO_QUEUEFAMILY_ERROR;

  size_t highest_queue_count = MAX(qfs.g_family.properties.queueCount,
                                   MAX(qfs.p_family.properties.queueCount,
                                       qfs.t_family.properties.queueCount));

  float *priorities = (float *)malloc(highest_queue_count * sizeof(float));
  if (NULL == priorities) {
    perror("malloc()");
    return FPX3D_MEMORY_ERROR;
  }

  for (size_t i = 0; i < highest_queue_count; ++i) {
    priorities[i] = 1.0f;
  }

  FPX3D_DEBUG("Initializing Logical GPU creation");

  VkDeviceQueueCreateInfo infos[8] = {0};
  size_t infos_count = 0;

#define APPEND_TO_TEMP(fam, q_count)                                           \
  {                                                                            \
    temp_infos[temp_info_count].sType =                                        \
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;                            \
    temp_infos[temp_info_count].queueFamilyIndex = fam.qfIndex;                \
    temp_infos[temp_info_count].queueCount = q_count,                          \
    temp_infos[temp_info_count].pQueuePriorities = priorities;                 \
    maximums[temp_info_count] = fam.properties.queueCount;                     \
  }

  {
    size_t temp_info_count = 0;
    VkDeviceQueueCreateInfo temp_infos[8] = {0};
    size_t maximums[8] = {0};
    if (qfs.g_family.isValid) {
      APPEND_TO_TEMP(qfs.g_family, g_queues);

      new_lgpu.graphicsQueues.queues =
          (VkQueue *)calloc(g_queues, sizeof(VkQueue));
      if (NULL == new_lgpu.graphicsQueues.queues) {
        perror("calloc()");
        FREE_SAFE(priorities);
        return FPX3D_MEMORY_ERROR;
      }

      ++temp_info_count;
    }
    if (qfs.p_family.isValid) {
      APPEND_TO_TEMP(qfs.p_family, p_queues);

      new_lgpu.presentQueues.queues =
          (VkQueue *)calloc(p_queues, sizeof(VkQueue));
      if (NULL == new_lgpu.presentQueues.queues) {
        perror("calloc()");
        FREE_SAFE(priorities);
        FREE_SAFE(new_lgpu.graphicsQueues.queues);
        return FPX3D_MEMORY_ERROR;
      }

      ++temp_info_count;
    }
    if (qfs.t_family.isValid) {
      APPEND_TO_TEMP(qfs.t_family, t_queues);

      new_lgpu.transferQueues.queues =
          (VkQueue *)calloc(t_queues, sizeof(VkQueue));
      if (NULL == new_lgpu.transferQueues.queues) {
        perror("calloc()");
        FREE_SAFE(priorities);
        FREE_SAFE(new_lgpu.graphicsQueues.queues);
        FREE_SAFE(new_lgpu.presentQueues.queues);
        return FPX3D_MEMORY_ERROR;
      }

      ++temp_info_count;
    }

#undef APPEND_TO_TEMP

    uint64_t processed_indices = 0;
    for (size_t i = 0; i < temp_info_count; ++i) {
      if (processed_indices & (1 << i))
        continue;

      processed_indices |= (1 << i);

      memcpy(&infos[infos_count], &temp_infos[i], sizeof(*infos));

      for (size_t j = i + 1; j < temp_info_count; ++j) {
        if (temp_infos[j].queueFamilyIndex != temp_infos[i].queueFamilyIndex)
          continue;

        processed_indices |= (1 << j);

        infos[infos_count].queueCount =
            MIN(infos[infos_count].queueCount + temp_infos[j].queueCount,
                maximums[j]);
      }

      ++infos_count;
    }
  }

  new_lgpu.graphicsQueues.count = g_queues;
  new_lgpu.graphicsQueues.queueFamilyIndex = qfs.g_family.qfIndex;
  new_lgpu.graphicsQueues.offsetInFamily = qfs.g_family.firstQueueIndex;
  new_lgpu.presentQueues.count = p_queues;
  new_lgpu.presentQueues.queueFamilyIndex = qfs.p_family.qfIndex;
  new_lgpu.presentQueues.offsetInFamily = qfs.p_family.firstQueueIndex;
  new_lgpu.transferQueues.count = t_queues;
  new_lgpu.transferQueues.queueFamilyIndex = qfs.t_family.qfIndex;
  new_lgpu.transferQueues.offsetInFamily = qfs.t_family.firstQueueIndex;

  FPX3D_DEBUG(" - Selected queue family %d for rendering (%" LONG_FORMAT
              "u queue%s)",
              new_lgpu.graphicsQueues.queueFamilyIndex, g_queues,
              (g_queues != 1) ? "s" : "");

  FPX3D_DEBUG(" - Selected queue family %d for presenting (%" LONG_FORMAT
              "u queue%s)",
              new_lgpu.presentQueues.queueFamilyIndex, p_queues,
              (p_queues != 1) ? "s" : "");

  FPX3D_DEBUG(" - Selected queue family %d for transfering (%" LONG_FORMAT
              "u queue%s)",
              new_lgpu.transferQueues.queueFamilyIndex, t_queues,
              (t_queues != 1) ? "s" : "");

  for (size_t i = 0; i < infos_count; ++i) {
    FPX3D_DEBUG(" - Requesting %u queue%s from queue family %u",
                infos[i].queueCount,
                CONDITIONAL(infos[i].queueCount != 1, "s", ""),
                infos[i].queueFamilyIndex);
  }

  VkDeviceCreateInfo d_info = {0};
  d_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  d_info.pQueueCreateInfos = infos;
  d_info.queueCreateInfoCount = infos_count;
  d_info.pEnabledFeatures = &features;

  d_info.enabledExtensionCount = ctx->lgpuExtensionCount;
  d_info.ppEnabledExtensionNames = ctx->lgpuExtensions;

  // // no longer required, but used for
  // // compatibility with older impl. of Vulkan
  // d_info.enabledLayerCount = ctx->instanceLayerCount;
  // d_info.ppEnabledLayerNames = ctx->instanceLayers;

  VkResult res =
      vkCreateDevice(ctx->physicalGpu, &d_info, NULL, &new_lgpu.handle);

  FREE_SAFE(priorities);

  if (VK_SUCCESS != res) {
    FPX3D_WARN("vkCreateDevice() failed: error code %d", res);
    FPX3D_ERROR("Failed to create Logical GPU");

    FREE_SAFE(new_lgpu.graphicsQueues.queues);
    FREE_SAFE(new_lgpu.presentQueues.queues);
    FREE_SAFE(new_lgpu.transferQueues.queues);

    return FPX3D_VK_LGPU_CREATE_ERROR;
  }

  new_lgpu.inFlightFences =
      (VkFence *)malloc(ctx->constants.maxFramesInFlight * sizeof(VkFence));

  if (NULL == new_lgpu.inFlightFences) {
    __fpx3d_vk_destroy_lgpu(ctx, &new_lgpu);

    return FPX3D_MEMORY_ERROR;
  }

  VkFenceCreateInfo f_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };

  if (FPX3D_SUCCESS != _construct_command_pool(&new_lgpu,
                                               &new_lgpu.inFlightCommandPool,
                                               GRAPHICS_POOL)) {
    __fpx3d_vk_destroy_lgpu(ctx, &new_lgpu);
    return FPX3D_VK_ERROR;
  }

  {
    new_lgpu.inFlightCommandPool.buffers = (VkCommandBuffer *)calloc(
        ctx->constants.maxFramesInFlight, sizeof(VkCommandBuffer));

    if (NULL == new_lgpu.inFlightCommandPool.buffers) {
      perror("calloc()");

      __fpx3d_vk_destroy_lgpu(ctx, &new_lgpu);
      return FPX3D_MEMORY_ERROR;
    }

    VkCommandBufferAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = new_lgpu.inFlightCommandPool.pool;
    alloc_info.commandBufferCount = ctx->constants.maxFramesInFlight;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    if (VK_SUCCESS !=
        vkAllocateCommandBuffers(new_lgpu.handle, &alloc_info,
                                 new_lgpu.inFlightCommandPool.buffers)) {
      __fpx3d_vk_destroy_lgpu(ctx, &new_lgpu);
    }

    new_lgpu.inFlightCommandPool.bufferCount = ctx->constants.maxFramesInFlight;
  }

  for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
    if (VK_SUCCESS != vkCreateFence(new_lgpu.handle, &f_info, NULL,
                                    &new_lgpu.inFlightFences[i])) {
      __fpx3d_vk_destroy_lgpu(ctx, &new_lgpu);
      return FPX3D_VK_ERROR;
    }
  }

  if (FPX3D_SUCCESS != _create_all_available_queues(&new_lgpu)) {
    __fpx3d_vk_destroy_lgpu(ctx, &new_lgpu);
    return FPX3D_VK_QUEUE_RETRIEVE_ERROR;
  }

  new_lgpu.features = features;
  ctx->logicalGpus[index] = new_lgpu;

  FPX3D_DEBUG(" - Logical GPU created!");

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_LogicalGpu *fpx3d_vk_get_logicalgpu_at(Fpx3d_Vk_Context *ctx,
                                                size_t index) {
  NULL_CHECK(ctx, NULL);
  NULL_CHECK(ctx->logicalGpus, NULL);

  if (ctx->logicalGpuCapacity <= index)
    return NULL;

  return &ctx->logicalGpus[index];
}

Fpx3d_E_Result fpx3d_vk_destroy_logicalgpu_at(Fpx3d_Vk_Context *ctx,
                                              size_t index) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx->logicalGpus, FPX3D_NULLPTR_ERROR);

  if (ctx->logicalGpuCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  NULL_CHECK(&ctx->logicalGpus[index].handle, FPX3D_VK_LGPU_INVALID_ERROR);

  __fpx3d_vk_destroy_lgpu(ctx, &ctx->logicalGpus[index]);

  return FPX3D_SUCCESS;
}

// STATIC FUNCTIONS -----------------------------------------
static Fpx3d_E_Result _find_queue_families(Fpx3d_Vk_Context *ctx,
                                           size_t g_queues, size_t p_queues,
                                           size_t t_queues,
                                           struct fpx3d_vk_qf_holder *output) {
  // TODO: PLEASE OMG FOR THE LOVE OF ALL THAT IS HOLY, PLEASE LOOP THIS CODE
  // TODO: for-loop magic

  Fpx3d_Vk_QueueFamily g_family = {0}, p_family = {0}, t_family = {0};

  Fpx3d_Vk_QueueFamilyRequirements qf_reqs = {0};

  if (0 < g_queues) {
    qf_reqs.type = GRAPHICS_QUEUE;
    qf_reqs.minimumQueues = g_queues;
    qf_reqs.graphics.requiredFlags = VK_QUEUE_GRAPHICS_BIT;
    g_family = _choose_queue_family(ctx, &qf_reqs);

    if (!g_family.isValid)
      return FPX3D_VK_QUEUE_RETRIEVE_ERROR;

    g_family.type = GRAPHICS_QUEUE;

    memset(&qf_reqs, 0, sizeof(qf_reqs));
  }

  if (0 < p_queues) {
    qf_reqs.type = PRESENT_QUEUE;
    qf_reqs.minimumQueues = p_queues;
    qf_reqs.present.gpu = ctx->physicalGpu;
    qf_reqs.present.surface = ctx->vkSurface;
    if (0 < g_queues)
      qf_reqs.indexBlacklistBits = (1 << g_family.qfIndex);
    p_family = _choose_queue_family(ctx, &qf_reqs);

    if (0 < g_queues && !p_family.isValid) {
      qf_reqs.indexBlacklistBits = 0;
      p_family = _choose_queue_family(ctx, &qf_reqs);
    }

    if (!p_family.isValid)
      return FPX3D_VK_QUEUE_RETRIEVE_ERROR;

    p_family.type = PRESENT_QUEUE;

    if (p_family.qfIndex == g_family.qfIndex)
      p_family.firstQueueIndex = CONDITIONAL(
          g_family.firstQueueIndex + g_queues >= g_family.properties.queueCount,
          g_family.properties.queueCount - p_queues,
          g_family.firstQueueIndex + g_queues);

    memset(&qf_reqs, 0, sizeof(qf_reqs));
  }

  if (0 < t_queues) {
    qf_reqs.type = TRANSFER_QUEUE;
    qf_reqs.minimumQueues = t_queues;
    qf_reqs.graphics.requiredFlags = VK_QUEUE_TRANSFER_BIT;
    if (0 < g_queues)
      qf_reqs.indexBlacklistBits |= (1 << g_family.qfIndex);
    if (0 < p_queues)
      qf_reqs.indexBlacklistBits |= (1 << p_family.qfIndex);
    t_family = _choose_queue_family(ctx, &qf_reqs);

    if (!t_family.isValid) {
      if (0 < g_queues)
        qf_reqs.indexBlacklistBits &= ~(1 << g_family.qfIndex);
      if (0 < p_queues)
        qf_reqs.indexBlacklistBits &= ~(1 << p_family.qfIndex);
      t_family = _choose_queue_family(ctx, &qf_reqs);
    }

    if (!t_family.isValid)
      return FPX3D_VK_QUEUE_RETRIEVE_ERROR;

    t_family.type = TRANSFER_QUEUE;

    if (t_family.qfIndex == p_family.qfIndex)
      t_family.firstQueueIndex = CONDITIONAL(
          p_family.firstQueueIndex + p_queues >= p_family.properties.queueCount,
          p_family.properties.queueCount - t_queues,
          p_family.firstQueueIndex + p_queues);
    else if (t_family.qfIndex == g_family.qfIndex)
      t_family.firstQueueIndex = CONDITIONAL(
          g_family.firstQueueIndex + g_queues >= g_family.properties.queueCount,
          g_family.properties.queueCount - t_queues,
          g_family.firstQueueIndex + g_queues);
  }

  output->g_family = g_family;
  output->p_family = p_family;
  output->t_family = t_family;

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _construct_command_pool(Fpx3d_Vk_LogicalGpu *lgpu,
                                              Fpx3d_Vk_CommandPool *pool,
                                              Fpx3d_Vk_E_CommandPoolType type) {

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(pool, FPX3D_ARGS_ERROR);

  Fpx3d_Vk_CommandPool new_pool = {0};

  VkCommandPoolCreateInfo p_info = {0};
  p_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  switch (type) {
  case GRAPHICS_POOL:
    p_info.queueFamilyIndex = lgpu->graphicsQueues.queueFamilyIndex;
    p_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    break;
  case TRANSFER_POOL:
    p_info.queueFamilyIndex = lgpu->transferQueues.queueFamilyIndex;
    p_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    break;
  }

  if (VK_SUCCESS !=
      vkCreateCommandPool(lgpu->handle, &p_info, NULL, &new_pool.pool))
    return FPX3D_VK_ERROR; // TODO: make better error

  new_pool.type = type;
  *pool = new_pool;

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _create_all_available_queues(Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (FPX3D_SUCCESS !=
      _create_queues(lgpu, GRAPHICS_QUEUE, lgpu->graphicsQueues.count))
    return FPX3D_VK_QUEUE_RETRIEVE_ERROR;

  if (FPX3D_SUCCESS !=
      _create_queues(lgpu, PRESENT_QUEUE, lgpu->presentQueues.count))
    return FPX3D_VK_QUEUE_RETRIEVE_ERROR;

  if (FPX3D_SUCCESS !=
      _create_queues(lgpu, TRANSFER_QUEUE, lgpu->transferQueues.count))
    return FPX3D_VK_QUEUE_RETRIEVE_ERROR;

  return FPX3D_SUCCESS;
}

static Fpx3d_Vk_QueueFamily
_choose_queue_family(Fpx3d_Vk_Context *ctx,
                     Fpx3d_Vk_QueueFamilyRequirements *reqs) {
  Fpx3d_Vk_QueueFamily info = {0};
  VkQueueFamilyProperties *props = NULL;

  uint32_t available_qf_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalGpu,
                                           &available_qf_count, NULL);

  if (0 == available_qf_count)
    return info;

  props = (VkQueueFamilyProperties *)calloc(available_qf_count,
                                            sizeof(VkQueueFamilyProperties));

  if (NULL == props) {
    perror("calloc()");
    return info;
  }

  vkGetPhysicalDeviceQueueFamilyProperties(ctx->physicalGpu,
                                           &available_qf_count, props);

  int64_t best_index = -1;
  for (int64_t i = 0; i < available_qf_count; ++i) {
    if (reqs->indexBlacklistBits & (1 << i))
      continue;

    VkQueueFamilyProperties *prop = &props[i];

    if (_qf_meets_requirements(*prop, reqs, i)) {
      if (best_index < 0) {
        best_index = i;
        continue;
      }

      if (prop->queueCount > props[best_index].queueCount) {
        best_index = i;
      }
    }
  }

  if (0 <= best_index && props[best_index].queueCount >= reqs->minimumQueues) {
    info.qfIndex = best_index;
    info.properties = props[best_index];
    info.isValid = true;
  }

  FREE_SAFE(props);

  return info;
}

static Fpx3d_E_Result _create_queues(Fpx3d_Vk_LogicalGpu *lgpu,
                                     Fpx3d_Vk_E_QueueType q_type,
                                     size_t count) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  struct fpx3d_vulkan_queues *queues =
      __fpx3d_vk_get_queues_ptr_by_type(lgpu, q_type);

  NULL_CHECK(queues, FPX3D_VK_QUEUE_RETRIEVE_ERROR);

  if (count > queues->count)
    return FPX3D_NO_CAPACITY_ERROR;

  for (size_t i = 0; i < queues->count; ++i) {
    vkGetDeviceQueue(lgpu->handle, queues->queueFamilyIndex,
                     queues->offsetInFamily + i, &queues->queues[i]);
  }

  return FPX3D_SUCCESS;
}

static bool _qf_meets_requirements(VkQueueFamilyProperties fam,
                                   Fpx3d_Vk_QueueFamilyRequirements *reqs,
                                   size_t qf_index) {
  switch (reqs->type) {
  case PRESENT_QUEUE:
    if (NULL == reqs->present.surface)
      break;

    VkBool32 present_support = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(
        reqs->present.gpu, qf_index, reqs->present.surface, &present_support);

    if (true == present_support)
      return true;

    break;

  case TRANSFER_QUEUE:
    // TODO: reconsider whether or not to do the check underneath at all (i
    // don't think so)

    // if (fam.queueFlags & (VK_QUEUE_GRAPHICS_BIT /*|
    // VK_QUEUE_COMPUTE_BIT*/))
    //   return false;

    // else we fall through to case RENDER to see if other things match

  case GRAPHICS_QUEUE:
    if (reqs->graphics.requiredFlags ==
        (reqs->graphics.requiredFlags & fam.queueFlags))
      return true;

    break;

  default:
    break;
  }

  return false;
}
// END OF STATIC FUNCTIONS ----------------------------------
