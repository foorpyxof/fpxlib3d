/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "fpx3d.h"
#include "macros.h"
#include "vk/context.h"
#include "vk/descriptors.h"
#include "vk/logical_gpu.h"
#include "vk/pipeline.h"
#include "vk/renderpass.h"
#include "vk/shape.h"
#include "vk/typedefs.h"

#include "vulkan/vulkan_core.h"

#include "volk/volk.h"

#include "vk/command.h"

extern Fpx3d_E_Result __fpx3d_realloc_array(void **arr_ptr, size_t obj_size,
                                            size_t amount,
                                            size_t *old_capacity);

VkCommandPool *__fpx3d_vk_select_pool_of_type(Fpx3d_Vk_E_CommandPoolType,
                                              Fpx3d_Vk_LogicalGpu *);

void __fpx3d_vk_destroy_command_pool(Fpx3d_Vk_LogicalGpu *,
                                     Fpx3d_Vk_CommandPool *);

VkCommandBuffer __fpx3d_vk_begin_temp_command_buffer(VkCommandPool, VkDevice);

Fpx3d_E_Result __fpx3d_vk_end_temp_command_buffer(VkCommandBuffer buf,
                                                  VkCommandPool graphics_pool,
                                                  VkQueue graphics_queue,
                                                  VkDevice);

VkCommandPool *__fpx3d_vk_select_pool_of_type(Fpx3d_Vk_E_CommandPoolType type,
                                              Fpx3d_Vk_LogicalGpu *lgpu) {
  for (size_t i = 0; i < lgpu->commandPoolCapacity; ++i) {
    if (type == lgpu->commandPools[i].type) {
      return &lgpu->commandPools[i].pool;
    }
  }

  return NULL;
}

void __fpx3d_vk_destroy_command_pool(Fpx3d_Vk_LogicalGpu *lgpu,
                                     Fpx3d_Vk_CommandPool *pool) {
  if (VK_NULL_HANDLE == pool->pool)
    return;

  vkDestroyCommandPool(lgpu->handle, pool->pool, NULL);

  FREE_SAFE(pool->buffers);

  memset(pool, 0, sizeof(*pool));

  return;
}

VkCommandBuffer
__fpx3d_vk_begin_temp_command_buffer(VkCommandPool graphics_pool,
                                     VkDevice lgpu) {
  VkCommandBufferAllocateInfo b_info = {0};
  b_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  b_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  b_info.commandPool = graphics_pool;
  b_info.commandBufferCount = 1;

  VkCommandBuffer cbuffer = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(lgpu, &b_info, &cbuffer);

  VkCommandBufferBeginInfo begin = {0};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(cbuffer, &begin);

  return cbuffer;
}

Fpx3d_E_Result __fpx3d_vk_end_temp_command_buffer(VkCommandBuffer buf,
                                                  VkCommandPool graphics_pool,
                                                  VkQueue graphics_queue,
                                                  VkDevice lgpu) {
  NULL_CHECK(buf, FPX3D_ARGS_ERROR);
  Fpx3d_E_Result retval = FPX3D_SUCCESS;

  vkEndCommandBuffer(buf);

  VkSubmitInfo s_info = {0};
  s_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  s_info.commandBufferCount = 1;
  s_info.pCommandBuffers = &buf;

  VkResult success = vkQueueSubmit(graphics_queue, 1, &s_info, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphics_queue);

  if ((VkResult)0 > success) {
    FPX3D_ERROR("Command buffer submission failed");

    retval = FPX3D_VK_ERROR;
  }

  vkFreeCommandBuffers(lgpu, graphics_pool, 1, &buf);

  return retval;
}

Fpx3d_E_Result fpx3d_vk_allocate_commandpools(Fpx3d_Vk_LogicalGpu *lgpu,
                                              size_t amount) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  return __fpx3d_realloc_array((void **)&lgpu->commandPools,
                               sizeof(lgpu->commandPools[0]), amount,
                               &lgpu->commandPoolCapacity);
}

Fpx3d_E_Result fpx3d_vk_create_commandpool_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                              size_t index,
                                              Fpx3d_Vk_E_CommandPoolType type) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->commandPools, FPX3D_NULLPTR_ERROR);

  if (lgpu->commandPoolCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  Fpx3d_Vk_CommandPool *to_create = &lgpu->commandPools[index];

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
      vkCreateCommandPool(lgpu->handle, &p_info, NULL, &to_create->pool))
    return FPX3D_VK_ERROR; // TODO: make better error

  to_create->type = type;

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_CommandPool *fpx3d_vk_get_commandpool_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                                  size_t index) {
  NULL_CHECK(lgpu, NULL);
  NULL_CHECK(lgpu->handle, NULL);

  NULL_CHECK(lgpu->commandPools, NULL);

  if (lgpu->commandPoolCapacity <= index)
    return NULL;

  return &lgpu->commandPools[index];
}

Fpx3d_E_Result fpx3d_vk_destroy_commandpool_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                               size_t index) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->commandPools, FPX3D_NULLPTR_ERROR);

  if (lgpu->commandPoolCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  __fpx3d_vk_destroy_command_pool(lgpu, &lgpu->commandPools[index]);

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result
fpx3d_vk_allocate_commandbuffers_at_pool(Fpx3d_Vk_LogicalGpu *lgpu,
                                         size_t cmd_pool_index, size_t amount) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->commandPools, FPX3D_NULLPTR_ERROR);

  if (lgpu->commandPoolCapacity <= cmd_pool_index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  Fpx3d_Vk_CommandPool *cmd_pool = &lgpu->commandPools[cmd_pool_index];

  if (VK_NULL_HANDLE == cmd_pool->pool)
    return FPX3D_VK_COMMAND_POOL_INVALID;

  VkCommandBuffer *buffers =
      (VkCommandBuffer *)malloc(amount * sizeof(VkCommandBuffer));

  if (NULL == buffers) {
    perror("malloc()");
    return FPX3D_MEMORY_ERROR;
  }

  VkCommandBufferAllocateInfo b_alloc = {0};
  b_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  b_alloc.commandPool = cmd_pool->pool;
  b_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  b_alloc.commandBufferCount = amount;

  if (VK_SUCCESS != vkAllocateCommandBuffers(lgpu->handle, &b_alloc, buffers)) {
    FREE_SAFE(buffers);
    return FPX3D_VK_ERROR; // TODO: better error message
  }

  FREE_SAFE(cmd_pool->buffers);
  cmd_pool->buffers = buffers;
  cmd_pool->bufferCount = amount;

  return FPX3D_SUCCESS;
}

VkCommandBuffer *fpx3d_vk_get_commandbuffer_at(Fpx3d_Vk_CommandPool *pool,
                                               size_t index) {
  NULL_CHECK(pool, NULL);
  NULL_CHECK(pool->buffers, NULL);

  if (pool->bufferCount <= index)
    return NULL;

  return &pool->buffers[index];
}

Fpx3d_E_Result fpx3d_vk_record_drawing_commandbuffer(
    VkCommandBuffer *buffer, Fpx3d_Vk_Pipeline *pipeline,
    Fpx3d_Vk_Swapchain *swapchain, size_t frame_index,
    Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(buffer, FPX3D_ARGS_ERROR);
  NULL_CHECK(pipeline, FPX3D_ARGS_ERROR);
  NULL_CHECK(swapchain, FPX3D_ARGS_ERROR);

  NULL_CHECK(pipeline->handle, FPX3D_VK_PIPELINE_INVALID_ERROR);
  NULL_CHECK(pipeline->graphics.renderPassReference, FPX3D_NULLPTR_ERROR);
  NULL_CHECK(swapchain->swapchain, FPX3D_VK_SWAPCHAIN_INVALID_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE == *buffer)
    return FPX3D_VK_BAD_BUFFER_HANDLE_ERROR;

  if (VK_NULL_HANDLE == pipeline->graphics.renderPassReference->handle)
    return FPX3D_VK_BAD_RENDER_PASS_HANDLE_ERROR;

  if (VK_NULL_HANDLE == pipeline->handle)
    return FPX3D_VK_PIPELINE_INVALID_ERROR;

  if (VK_NULL_HANDLE == swapchain->swapchain)
    return FPX3D_VK_SWAPCHAIN_INVALID_ERROR;

  if (swapchain->frameCount <= frame_index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  VkCommandBufferBeginInfo b_info = {0};
  b_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  b_info.flags = 0;
  b_info.pInheritanceInfo = NULL;

  if (VK_SUCCESS != vkBeginCommandBuffer(*buffer, &b_info))
    return FPX3D_VK_COMMAND_BUFFER_FAULT;

  VkRenderPassBeginInfo r_info = {0};
  r_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  r_info.renderPass = pipeline->graphics.renderPassReference->handle;
  r_info.framebuffer = swapchain->frames[frame_index].framebuffer;

  r_info.renderArea.extent = swapchain->swapchainExtent;
  r_info.renderArea.offset.x = 0;
  r_info.renderArea.offset.y = 0;

  VkClearValue clears[2] = {{{{0.0f, 0.0f, 0.0f, 0.0f}}}, {{{1.0f, 0}}}};
  r_info.clearValueCount =
      CONDITIONAL(swapchain->renderPassReference->depth, 2, 1);
  r_info.pClearValues = clears;

  vkCmdBeginRenderPass(*buffer, &r_info, VK_SUBPASS_CONTENTS_INLINE);

  {
    VkViewport vp = {0};
    vp.x = 0.0f;
    vp.y = 0.0f;

    vp.width = (float)swapchain->swapchainExtent.width;
    vp.height = (float)swapchain->swapchainExtent.height;

    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(*buffer, 0, 1, &vp);

    VkRect2D scissor = {0};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent = swapchain->swapchainExtent;
    vkCmdSetScissor(*buffer, 0, 1, &scissor);
  }

  VkDescriptorSet bind_sets[DESCRIPTOR_SET_INDEX_MAX_VALUE] = {0};
  bind_sets[DESCRIPTOR_SET_INDEX_PIPELINE] =
      pipeline->bindings.inFlightDescriptorSets[lgpu->frameCounter].handle;

  vkCmdBindPipeline(*buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);

  vkCmdBindDescriptorSets(*buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline->layout.handle,
                          DESCRIPTOR_SET_INDEX_PIPELINE, 1, bind_sets, 0, NULL);

  Fpx3d_Vk_DescriptorSet *pipeline_ds =
      &pipeline->bindings.inFlightDescriptorSets[lgpu->frameCounter];
  memcpy(pipeline_ds->buffer.mapped_memory, pipeline->bindings.rawBufferData,
         pipeline_ds->buffer.objectCount * pipeline_ds->buffer.stride);

  for (size_t i = 0; i < pipeline->graphics.shapeCount; ++i) {
    // TODO: fix hardcoded stuff like instanceCount, firstVertex and other args

    Fpx3d_Vk_Shape *shape = pipeline->graphics.shapes[i];

    if (false == shape->isValid)
      continue;

    if (NULL != shape->bindings.inFlightDescriptorSets &&
        NULL != shape->bindings.rawBufferData) {
      Fpx3d_Vk_DescriptorSet *shape_ds =
          &shape->bindings.inFlightDescriptorSets[lgpu->frameCounter];

      bind_sets[DESCRIPTOR_SET_INDEX_OBJECT] = shape_ds->handle;

      vkCmdBindDescriptorSets(
          *buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout.handle,
          DESCRIPTOR_SET_INDEX_PIPELINE, 2, bind_sets, 0, NULL);

      memcpy(shape_ds->buffer.mapped_memory, shape->bindings.rawBufferData,
             shape_ds->buffer.objectCount * shape_ds->buffer.stride);
    }

    VkDeviceSize offset = 0;

    vkCmdBindVertexBuffers(*buffer, 0, 1,
                           &shape->shapeBuffer->vertexBuffer.buffer, &offset);

    if (VK_NULL_HANDLE == shape->shapeBuffer->indexBuffer.buffer ||
        VK_NULL_HANDLE == shape->shapeBuffer->indexBuffer.memory) {
      // normal draw, using the given vertices
      // because there's no index buffer
      vkCmdDraw(*buffer, shape->shapeBuffer->vertexBuffer.objectCount, 1, 0, 0);
    } else {
      // we have an index buffer
      vkCmdBindIndexBuffer(*buffer, shape->shapeBuffer->indexBuffer.buffer, 0,
                           VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(*buffer, shape->shapeBuffer->indexBuffer.objectCount, 1,
                       0, 0, 0);
    }
  }

  vkCmdEndRenderPass(*buffer);

  if (VK_SUCCESS != vkEndCommandBuffer(*buffer))
    return FPX3D_VK_ERROR;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_submit_commandbuffer(VkCommandBuffer *buffer,
                                             Fpx3d_Vk_Context *ctx,
                                             Fpx3d_Vk_LogicalGpu *lgpu,
                                             size_t frame_index,
                                             VkQueue *graphics_queue) {
  NULL_CHECK(buffer, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(graphics_queue, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE == *buffer)
    return FPX3D_VK_BAD_BUFFER_HANDLE_ERROR;

  if (VK_NULL_HANDLE == *graphics_queue)
    return FPX3D_VK_BAD_QUEUE_HANDLE_ERROR;

  if (lgpu->currentSwapchain.frameCount <= frame_index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  VkSubmitInfo s_info = {0};
  s_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkPipelineStageFlags sf = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  s_info.waitSemaphoreCount = 1;
  s_info.pWaitSemaphores = &lgpu->currentSwapchain.acquireSemaphore;
  s_info.pWaitDstStageMask = &sf;

  s_info.commandBufferCount = 1;
  s_info.pCommandBuffers = buffer;

  s_info.signalSemaphoreCount = 1;
  s_info.pSignalSemaphores =
      &lgpu->currentSwapchain.frames[frame_index].renderFinished;

  if (VK_SUCCESS != vkQueueSubmit(*graphics_queue, 1, &s_info,
                                  lgpu->inFlightFences[lgpu->frameCounter]))
    return FPX3D_VK_ERROR;

  lgpu->frameCounter =
      (lgpu->frameCounter + 1) % ctx->constants.maxFramesInFlight;

  return FPX3D_SUCCESS;
}

// STATIC FUNCTIONS --------------------------------------------
// END OF STATIC FUNCTIONS -------------------------------------
