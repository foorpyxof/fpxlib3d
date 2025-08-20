/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "debug.h"
#include "fpx3d.h"
#include "vk/buffer.h"
#include "vk/command.h"
#include "vk/logical_gpu.h"
#include "vk/utility.h"
#include "volk/volk.h"

extern VkCommandBuffer __fpx3d_vk_begin_temp_command_buffer(VkCommandPool,
                                                            VkDevice);

extern VkCommandPool *__fpx3d_vk_select_pool_of_type(Fpx3d_Vk_E_CommandPoolType,
                                                     Fpx3d_Vk_LogicalGpu *);

extern Fpx3d_E_Result
__fpx3d_vk_end_temp_command_buffer(VkCommandBuffer, VkCommandPool graphics_pool,
                                   VkQueue graphics_queue, VkDevice lgpu);

Fpx3d_E_Result __fpx3d_vk_new_memory(VkPhysicalDevice, Fpx3d_Vk_LogicalGpu *,
                                     VkMemoryPropertyFlags mem_flags,
                                     VkMemoryRequirements mem_reqs,
                                     VkDeviceMemory *output);

Fpx3d_E_Result __fpx3d_vk_new_buffer(VkPhysicalDevice, Fpx3d_Vk_LogicalGpu *,
                                     VkDeviceSize size,
                                     VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags mem_flags,
                                     VkSharingMode,
                                     Fpx3d_Vk_Buffer *output_buffer);

Fpx3d_E_Result __fpx3d_vk_data_to_buffer(Fpx3d_Vk_LogicalGpu *,
                                         Fpx3d_Vk_Buffer *, void *data,
                                         VkDeviceSize size);

Fpx3d_E_Result __fpx3d_vk_bufcopy(VkDevice, VkQueue transfer_queue,
                                  Fpx3d_Vk_Buffer *src, Fpx3d_Vk_Buffer *dst,
                                  VkDeviceSize size,
                                  VkCommandPool transfer_cmd_pool);

Fpx3d_Vk_Buffer __fpx3d_vk_new_buffer_with_data(VkPhysicalDevice,
                                                Fpx3d_Vk_LogicalGpu *,
                                                void *data, VkDeviceSize size,
                                                VkBufferUsageFlags usage_flags);

void __fpx3d_vk_destroy_buffer_object(Fpx3d_Vk_LogicalGpu *lgpu,
                                      Fpx3d_Vk_Buffer *buffer);

Fpx3d_E_Result __fpx3d_vk_new_memory(VkPhysicalDevice dev,
                                     Fpx3d_Vk_LogicalGpu *lgpu,
                                     VkMemoryPropertyFlags mem_flags,
                                     VkMemoryRequirements mem_reqs,
                                     VkDeviceMemory *output) {
  VkDeviceMemory new_mem = {0};

  VkPhysicalDeviceMemoryProperties mem_props = {0};
  vkGetPhysicalDeviceMemoryProperties(dev, &mem_props);

  /*
   *  Unsupported extensions
   */
  uint32_t unsupported = 0;

  {
    const char *extension = {VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME};

    if (false == fpx3d_vk_device_extensions_supported(dev, &extension, 1))
      unsupported |= VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD;
  }

  /*
   *  End of Unsupported extensions
   */

  int idx = -1;
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
    if ((mem_reqs.memoryTypeBits & (1 << i)) &&
        (mem_props.memoryTypes[i].propertyFlags & mem_flags) == mem_flags &&
        (mem_props.memoryTypes[i].propertyFlags & unsupported) == 0) {
      idx = i;
      break;
    }
  }

  if (0 > idx) {
    // error
    FPX3D_WARN("Could not find valid memory type");
    return FPX3D_VK_ERROR;
  }

  VkMemoryAllocateInfo m_info = {0};
  m_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  m_info.allocationSize = mem_reqs.size;
  m_info.memoryTypeIndex = idx;

  if (VK_SUCCESS != vkAllocateMemory(lgpu->handle, &m_info, NULL, &new_mem)) {
    FPX3D_WARN("Could not allocate buffer memory");

    return FPX3D_VK_ERROR;
  }

  *output = new_mem;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result __fpx3d_vk_new_buffer(
    VkPhysicalDevice dev, Fpx3d_Vk_LogicalGpu *lgpu, VkDeviceSize size,
    VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_flags,
    VkSharingMode sharing_mode, Fpx3d_Vk_Buffer *output_buffer) {
  VkBuffer new_buf = {0};
  VkDeviceMemory new_mem = {0};

  VkBufferCreateInfo b_info = {0};

  b_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  b_info.size = size;
  b_info.usage = usage;

  uint32_t indices[2] = {lgpu->graphicsQueues.queueFamilyIndex,
                         lgpu->transferQueues.queueFamilyIndex};

  if (VK_SHARING_MODE_CONCURRENT == sharing_mode &&
      (0 <= lgpu->transferQueues.queueFamilyIndex &&
       lgpu->transferQueues.queueFamilyIndex !=
           lgpu->graphicsQueues.queueFamilyIndex)) {
    sharing_mode = VK_SHARING_MODE_CONCURRENT;
    b_info.queueFamilyIndexCount = 2;
    b_info.pQueueFamilyIndices = indices;
  } else {
    sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
  }

  b_info.sharingMode = sharing_mode;

  if (VK_SUCCESS != vkCreateBuffer(lgpu->handle, &b_info, NULL, &new_buf)) {
    FPX3D_WARN("Could not create a buffer");
    return FPX3D_VK_ERROR;
  }

  VkMemoryRequirements mem_reqs = {0};
  vkGetBufferMemoryRequirements(lgpu->handle, new_buf, &mem_reqs);

  Fpx3d_E_Result mem_success =
      __fpx3d_vk_new_memory(dev, lgpu, mem_flags, mem_reqs, &new_mem);
  if (FPX3D_SUCCESS != mem_success) {
    vkDestroyBuffer(lgpu->handle, new_buf, NULL);

    return mem_success;
  }

  if (VK_SUCCESS != vkBindBufferMemory(lgpu->handle, new_buf, new_mem, 0)) {
    // error
    vkFreeMemory(lgpu->handle, new_mem, NULL);
    vkDestroyBuffer(lgpu->handle, new_buf, NULL);

    FPX3D_WARN("Could not bind buffer memory");

    return FPX3D_VK_ERROR;
  }

  output_buffer->isValid = true;
  output_buffer->sharingMode = sharing_mode;

  output_buffer->buffer = new_buf;

  output_buffer->memory = new_mem;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result __fpx3d_vk_data_to_buffer(Fpx3d_Vk_LogicalGpu *lgpu,
                                         Fpx3d_Vk_Buffer *buf, void *data,
                                         VkDeviceSize size) {
  void *mapped = NULL;
  if (VK_SUCCESS != vkMapMemory(lgpu->handle, buf->memory, 0, size, 0, &mapped))
    return FPX3D_VK_ERROR;

  memcpy(mapped, data, size);

  vkUnmapMemory(lgpu->handle, buf->memory);

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result __fpx3d_vk_bufcopy(VkDevice lgpu, VkQueue transfer_queue,
                                  Fpx3d_Vk_Buffer *src, Fpx3d_Vk_Buffer *dst,
                                  VkDeviceSize size,
                                  VkCommandPool transfer_cmd_pool) {
  VkCommandBuffer cbuffer =
      __fpx3d_vk_begin_temp_command_buffer(transfer_cmd_pool, lgpu);

  VkBufferCopy region = {0};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = size;

  vkCmdCopyBuffer(cbuffer, src->buffer, dst->buffer, 1, &region);

  __fpx3d_vk_end_temp_command_buffer(cbuffer, transfer_cmd_pool, transfer_queue,
                                     lgpu);

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_Buffer
__fpx3d_vk_new_buffer_with_data(VkPhysicalDevice dev, Fpx3d_Vk_LogicalGpu *lgpu,
                                void *data, VkDeviceSize size,
                                VkBufferUsageFlags usage_flags) {
  // TODO: Currently when using a staging buffer, both of the buffers will be
  // VK_SHARING_MODE_CONCURRENT. This can be changed by using
  // VkBufferMemoryBarriers

  FPX3D_TODO("Change VK_SHARING_MODE_CONCURRENT to VK_SHARING_MODE_EXCLUSIVE "
             "for _new_buffer_with_data()")

  Fpx3d_Vk_Buffer s_buf = {0};
  Fpx3d_Vk_Buffer return_buf = {0};

  NULL_CHECK(dev, return_buf);
  NULL_CHECK(lgpu->handle, return_buf);

  bool use_staging = true;

  VkQueue *graphics_queue = VK_NULL_HANDLE;
  VkCommandPool *graphics_pool = VK_NULL_HANDLE;
  if (NULL != lgpu->commandPools) {
    graphics_pool = __fpx3d_vk_select_pool_of_type(GRAPHICS_POOL, lgpu);
  }

  use_staging = (NULL != graphics_pool);

  if (use_staging) {
    if (NULL != lgpu->graphicsQueues.queues && lgpu->graphicsQueues.count > 0) {
      graphics_queue =
          &lgpu->graphicsQueues.queues[lgpu->graphicsQueues.nextToUse++];
    } else {
      use_staging = false;
    }
  }

  lgpu->graphicsQueues.nextToUse %= lgpu->graphicsQueues.count;

  VkBufferUsageFlags u_flags = usage_flags;
  VkSharingMode s_mode = VK_SHARING_MODE_EXCLUSIVE;
  VkMemoryPropertyFlags m_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  if (use_staging) {
    __fpx3d_vk_new_buffer(dev, lgpu, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          VK_SHARING_MODE_CONCURRENT, &s_buf);

    if (false == s_buf.isValid)
      use_staging = false;
    else {
      u_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      s_mode = VK_SHARING_MODE_CONCURRENT;
      m_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

      __fpx3d_vk_data_to_buffer(lgpu, &s_buf, data, size);
    }
  }

  __fpx3d_vk_new_buffer(dev, lgpu, size, u_flags, m_flags, s_mode, &return_buf);

  if (use_staging) {
    __fpx3d_vk_bufcopy(lgpu->handle, *graphics_queue, &s_buf, &return_buf, size,
                       *graphics_pool);
    __fpx3d_vk_destroy_buffer_object(lgpu, &s_buf);
  } else {
    __fpx3d_vk_data_to_buffer(lgpu, &return_buf, data, size);
  }

  return return_buf;
}

void __fpx3d_vk_destroy_buffer_object(Fpx3d_Vk_LogicalGpu *lgpu,
                                      Fpx3d_Vk_Buffer *buffer) {
  if (VK_NULL_HANDLE != buffer->buffer)
    vkDestroyBuffer(lgpu->handle, buffer->buffer, NULL);

  if (NULL != buffer->mapped_memory)
    vkUnmapMemory(lgpu->handle, buffer->memory);

  if (VK_NULL_HANDLE != buffer->memory)
    vkFreeMemory(lgpu->handle, buffer->memory, NULL);

  memset(buffer, 0, sizeof(*buffer));
}
