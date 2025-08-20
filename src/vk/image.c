/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "fpx3d.h"
#include "macros.h"
#include "vk/buffer.h"
#include "vk/typedefs.h"
#include "vk/utility.h"
#include "volk/volk.h"

#include "debug.h"
#include "vk/context.h"
#include "vk/logical_gpu.h"

#include "vk/image.h"

#define CHECK_DIMENSIONS(d, ret)                                               \
  if (1 > d.channels || 1 > d.height || 1 > d.width || 1 > d.channelWidth)     \
    return ret;

extern Fpx3d_E_Result __fpx3d_vk_new_memory(VkPhysicalDevice,
                                            Fpx3d_Vk_LogicalGpu *,
                                            VkMemoryPropertyFlags,
                                            VkMemoryRequirements,
                                            VkDeviceMemory *);

extern VkCommandPool *__fpx3d_vk_select_pool_of_type(Fpx3d_Vk_E_CommandPoolType,
                                                     Fpx3d_Vk_LogicalGpu *);

extern VkCommandBuffer __fpx3d_vk_begin_temp_command_buffer(VkCommandPool,
                                                            VkDevice);
extern Fpx3d_E_Result __fpx3d_vk_end_temp_command_buffer(VkCommandBuffer,
                                                         VkCommandPool, VkQueue,
                                                         VkDevice);

extern void __fpx3d_vk_destroy_buffer_object(Fpx3d_Vk_LogicalGpu *,
                                             Fpx3d_Vk_Buffer *);

extern Fpx3d_E_Result
__fpx3d_vk_new_buffer(VkPhysicalDevice, Fpx3d_Vk_LogicalGpu *,
                      VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags mem_flags, VkSharingMode,
                      Fpx3d_Vk_Buffer *output);
extern Fpx3d_E_Result __fpx3d_vk_data_to_buffer(Fpx3d_Vk_LogicalGpu *,
                                                Fpx3d_Vk_Buffer *, void *data,
                                                VkDeviceSize size);

// static declarations -----------------------------------
static VkFormat _fpx3d_vk_texture_formats_table[][5] = {
    {0, 0, 0, 0, 0},
    {0, VK_FORMAT_R8_SRGB, VK_FORMAT_R8G8_SRGB, VK_FORMAT_R8G8B8_SRGB,
     VK_FORMAT_R8G8B8A8_SRGB}};
#define VALID_FORMAT_INDEX(idx)                                                \
  ((ARRAY_SIZE(_fpx3d_vk_texture_formats_table) *                              \
    ARRAY_SIZE(_fpx3d_vk_texture_formats_table[0])) < (idx))

static VkFormat _supported_format(VkFormat *fmts, size_t count, VkImageTiling,
                                  VkFormatFeatureFlags, VkPhysicalDevice);
static Fpx3d_E_Result _new_image_sampler(Fpx3d_Vk_Context *,
                                         Fpx3d_Vk_LogicalGpu *,
                                         VkSamplerAddressMode addr_mode,
                                         bool bilinear, bool anisotropy,
                                         Fpx3d_Vk_ImageSampler *output);

static Fpx3d_E_Result _fill_image_data(Fpx3d_Vk_Image *, void *data,
                                       size_t data_length,
                                       Fpx3d_Vk_LogicalGpu *, VkPhysicalDevice);

static struct fpx3d_vulkan_pool_queue_pair
_graphics_pool_and_queue(Fpx3d_Vk_LogicalGpu *lgpu);

static Fpx3d_E_Result
_transition_image_layout(VkImage, VkFormat, VkImageLayout *old,
                         VkImageLayout new, VkImageSubresourceRange,
                         VkCommandPool graphics_pool, VkQueue graphics_queue,
                         VkDevice lgpu);

static Fpx3d_E_Result _vk_buf_to_image(VkBuffer, Fpx3d_Vk_Image *,
                                       VkImageLayout,
                                       VkImageSubresourceRange s_range,
                                       VkCommandPool graphics_pool,
                                       VkQueue graphics_queue, VkDevice lgpu);
// end of static declarations ----------------------------

Fpx3d_E_Result __fpx3d_vk_new_image(VkPhysicalDevice, Fpx3d_Vk_LogicalGpu *,
                                    Fpx3d_Vk_ImageDimensions dimensions,
                                    VkFormat fmt, VkImageTiling tiling,
                                    VkImageSubresourceRange s_range,
                                    VkImageUsageFlags usage,
                                    Fpx3d_Vk_Image *output);

Fpx3d_E_Result __fpx3d_vk_new_image_view(Fpx3d_Vk_Image *,
                                         Fpx3d_Vk_LogicalGpu *,
                                         VkImageView *output);

Fpx3d_E_Result
__fpx3d_vk_new_image(VkPhysicalDevice dev, Fpx3d_Vk_LogicalGpu *lgpu,
                     Fpx3d_Vk_ImageDimensions dimensions, VkFormat fmt,
                     VkImageTiling tiling, VkImageSubresourceRange s_range,
                     VkImageUsageFlags usage, Fpx3d_Vk_Image *output) {
  NULL_CHECK(output, FPX3D_ARGS_ERROR);
  NULL_CHECK(dev, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if ((VkFormat)0 == fmt)
    return FPX3D_ARGS_ERROR;

  FPX3D_TODO(
      "_new_image() and its subsidiaries have a lot of hard-coded stuff. fix");

  struct fpx3d_vulkan_pool_queue_pair pair = _graphics_pool_and_queue(lgpu);
  if (NULL == pair.pool || NULL == pair.queue) {
    FPX3D_ERROR("No graphics-enabled queues or command pools available on LGPU "
                "%p to create image",
                (void *)lgpu->handle);

    return FPX3D_GENERIC_ERROR;
  }

  VkImageUsageFlags u_flags = usage;
  VkSharingMode s_mode = VK_SHARING_MODE_EXCLUSIVE;
  VkMemoryPropertyFlags m_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  u_flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  // m_flags &= ~(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
  //              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  // m_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  m_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  VkImage new_img = {0};
  VkDeviceMemory new_mem = {0};

  VkImageCreateInfo i_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .extent = {.width = dimensions.width,
                                         .height = dimensions.height,
                                         .depth = 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .format = fmt,
                              .tiling = tiling,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                              .usage = u_flags,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .sharingMode = s_mode};

  if (VK_SUCCESS != vkCreateImage(lgpu->handle, &i_info, NULL, &new_img)) {
    FPX3D_WARN("Failed to create new VkImage");
    return FPX3D_VK_ERROR;
  }

  VkMemoryRequirements mem_reqs = {0};
  vkGetImageMemoryRequirements(lgpu->handle, new_img, &mem_reqs);

  FPX3D_ONFAIL(__fpx3d_vk_new_memory(dev, lgpu, m_flags, mem_reqs, &new_mem),
               mem_success, vkDestroyImage(lgpu->handle, new_img, NULL);
               FPX3D_ERROR("Failed to allocate image memory");
               return mem_success;);

  if (VK_SUCCESS != vkBindImageMemory(lgpu->handle, new_img, new_mem, 0)) {
    FPX3D_ERROR("Failed to bind image memory");

    return FPX3D_VK_ERROR;
  }

  {
    VkImageLayout temp_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    FPX3D_ONFAIL(_transition_image_layout(new_img, fmt, &temp_layout,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          s_range, *pair.pool, *pair.queue,
                                          lgpu->handle),
                 success, FPX3D_ERROR("Failed to prepare image layout");
                 return success;);
  }

  output->image = new_img;
  output->memory = new_mem;

  output->imageFormat = fmt;

  output->dimensions = dimensions;

  output->subresourceRange = s_range;

  output->isValid = true;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result __fpx3d_vk_new_image_view(Fpx3d_Vk_Image *image,
                                         Fpx3d_Vk_LogicalGpu *lgpu,
                                         VkImageView *output) {
  NULL_CHECK(image, FPX3D_ARGS_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_ARGS_ERROR);

  NULL_CHECK(image->image, FPX3D_VK_LGPU_INVALID_ERROR);

  VkImageView new_view = {0};

  VkImageViewCreateInfo v_info = {.sType =
                                      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                  .image = image->image,
                                  .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                  .format = image->imageFormat,
                                  .subresourceRange = image->subresourceRange};

  if (VK_SUCCESS != vkCreateImageView(lgpu->handle, &v_info, NULL, &new_view))
    return FPX3D_VK_ERROR;

  FPX3D_DEBUG("Created new VkImageView %p", (void *)new_view);

  *output = new_view;
  return FPX3D_SUCCESS;
}

Fpx3d_Vk_Image
fpx3d_vk_create_depth_image(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu,
                            Fpx3d_Vk_ImageDimensions dimensions) {
  Fpx3d_Vk_Image retval = {0};
  NULL_CHECK(ctx, retval);

  NULL_CHECK(lgpu, retval);
  NULL_CHECK(lgpu->handle, retval);

  if (1 > dimensions.width || 1 > dimensions.height)
    return retval;

  VkImageSubresourceRange s_range = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                     .baseArrayLayer = 0,
                                     .layerCount = 1,
                                     .baseMipLevel = 0,
                                     .levelCount = 1};

  VkFormat choices[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                        VK_FORMAT_D24_UNORM_S8_UINT};
  VkFormat depth_format = _supported_format(
      choices, ARRAY_SIZE(choices), VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, ctx->physicalGpu);

  // if no valid depth formats found
  if (VK_FORMAT_UNDEFINED == depth_format)
    return retval;

  if (depth_format != choices[0]) {
    // the format has a stencil component (S8_UINT)
    s_range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  FPX3D_ONFAIL(__fpx3d_vk_new_image(
                   ctx->physicalGpu, lgpu, dimensions, depth_format,
                   VK_IMAGE_TILING_OPTIMAL, s_range,
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &retval),
               success, return retval;);

  struct fpx3d_vulkan_pool_queue_pair pair = _graphics_pool_and_queue(lgpu);
  _transition_image_layout(retval.image, depth_format, &retval.imageLayout,
                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                           s_range, *pair.pool, *pair.queue, lgpu->handle);

  VkImageView new_view = {0};
  FPX3D_ONFAIL(__fpx3d_vk_new_image_view(&retval, lgpu, &new_view), success,
               fpx3d_vk_destroy_image(&retval, lgpu);
               return retval;);

  retval.imageView = new_view;

  return retval;
}

Fpx3d_Vk_Image
fpx3d_vk_create_texture_image(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu,
                              Fpx3d_Vk_ImageDimensions dimensions) {
  Fpx3d_Vk_Image retval = {0};
  NULL_CHECK(ctx, retval);
  NULL_CHECK(lgpu, retval);
  NULL_CHECK(lgpu->handle, retval);

  if (VALID_FORMAT_INDEX(dimensions.channels * dimensions.channelWidth)) {
    // lookup table index out of range
    return retval;
  }

  CHECK_DIMENSIONS(dimensions, retval);

  VkFormat fmt = _fpx3d_vk_texture_formats_table[dimensions.channelWidth]
                                                [dimensions.channels];

  VkImageSubresourceRange s_range = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .baseMipLevel = 0,
                                     .levelCount = 1,
                                     .baseArrayLayer = 0,
                                     .layerCount = 1};

  FPX3D_ONFAIL(__fpx3d_vk_new_image(ctx->physicalGpu, lgpu, dimensions, fmt,
                                    VK_IMAGE_TILING_OPTIMAL, s_range,
                                    VK_IMAGE_USAGE_SAMPLED_BIT, &retval),
               success, return retval;);

  VkImageView new_view = {0};
  FPX3D_ONFAIL(__fpx3d_vk_new_image_view(&retval, lgpu, &new_view), success,
               fpx3d_vk_destroy_image(&retval, lgpu);
               return retval;);

  retval.imageView = new_view;
  retval.sizeInBytes = fpx3d_vk_get_image_size_bytes;

  return retval;
}

#undef CHECK_DIMENSIONS

Fpx3d_E_Result fpx3d_vk_fill_image(Fpx3d_Vk_Image *img, Fpx3d_Vk_Context *ctx,
                                   Fpx3d_Vk_LogicalGpu *lgpu, void *data) {
  NULL_CHECK(img, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(data, FPX3D_ARGS_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  size_t data_length = img->dimensions.width * img->dimensions.height *
                       img->dimensions.channels * img->dimensions.channelWidth;

  _fill_image_data(img, data, data_length, lgpu, ctx->physicalGpu);

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_image_readonly(Fpx3d_Vk_Image *image,
                                       Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(image, FPX3D_ARGS_ERROR);

  if (image->isReadOnly)
    return FPX3D_SUCCESS;

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  struct fpx3d_vulkan_pool_queue_pair pair = _graphics_pool_and_queue(lgpu);

  if (NULL == pair.pool || NULL == pair.queue) {
    return FPX3D_VK_ERROR;
  }

  _transition_image_layout(
      image->image, image->imageFormat, &image->imageLayout,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, image->subresourceRange,
      *pair.pool, *pair.queue, lgpu->handle);

  image->isReadOnly = true;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_destroy_image(Fpx3d_Vk_Image *image,
                                      Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(image, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE != image->imageView)
    vkDestroyImageView(lgpu->handle, image->imageView, NULL);

  if (VK_NULL_HANDLE != image->image)
    vkDestroyImage(lgpu->handle, image->image, NULL);

  if (VK_NULL_HANDLE != image->memory)
    vkFreeMemory(lgpu->handle, image->memory, NULL);

  memset(image, 0, sizeof(*image));

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_ImageSampler fpx3d_vk_create_image_sampler(Fpx3d_Vk_Context *ctx,
                                                    Fpx3d_Vk_LogicalGpu *lgpu,
                                                    bool bilinear_filter,
                                                    bool anisotropic_filter) {
  Fpx3d_Vk_ImageSampler retval = {0};

  NULL_CHECK(ctx, retval);

  NULL_CHECK(lgpu, retval);
  NULL_CHECK(lgpu->handle, retval);

  _new_image_sampler(ctx, lgpu, VK_SAMPLER_ADDRESS_MODE_REPEAT, bilinear_filter,
                     anisotropic_filter, &retval);

  return retval;
}

Fpx3d_E_Result fpx3d_vk_destroy_image_sampler(Fpx3d_Vk_ImageSampler *sampler,
                                              Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(sampler, FPX3D_ARGS_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_ARGS_ERROR);

  if (VK_NULL_HANDLE != sampler->handle)
    vkDestroySampler(lgpu->handle, sampler->handle, NULL);

  memset(sampler, 0, sizeof(*sampler));

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_Texture fpx3d_vk_create_texture(Fpx3d_Vk_Image *image,
                                         Fpx3d_Vk_ImageSampler *sampler) {
  Fpx3d_Vk_Texture retval = {0};

  NULL_CHECK(image, retval);
  NULL_CHECK(sampler, retval);

  retval.imageReference = image;
  retval.samplerReference = sampler;

  retval.isValid = true;

  return retval;
}

size_t fpx3d_vk_get_image_size_bytes(Fpx3d_Vk_Image *image) {
  NULL_CHECK(image, 0);

  return (size_t)(image->dimensions.width * image->dimensions.height *
                  image->dimensions.channels * image->dimensions.channelWidth);
}

// STATIC FUNCTIONS -------------------------------------------
static VkFormat _supported_format(VkFormat *fmts, size_t count,
                                  VkImageTiling tiling,
                                  VkFormatFeatureFlags features,
                                  VkPhysicalDevice dev) {
  for (size_t i = 0; i < count; ++i) {
    VkFormatProperties props = {0};
    vkGetPhysicalDeviceFormatProperties(dev, fmts[i], &props);

    VkFormatFeatureFlags supported_features = {0};

    if (VK_IMAGE_TILING_LINEAR == tiling) {
      supported_features = props.linearTilingFeatures & features;
    } else if (VK_IMAGE_TILING_OPTIMAL == tiling) {
      supported_features = props.optimalTilingFeatures & features;
    }

    if (supported_features == features)
      return fmts[i];
  }

  return VK_FORMAT_UNDEFINED;
}

static Fpx3d_E_Result _new_image_sampler(Fpx3d_Vk_Context *ctx,
                                         Fpx3d_Vk_LogicalGpu *lgpu,
                                         VkSamplerAddressMode addr_mode,
                                         bool bilinear, bool anisotropy,
                                         Fpx3d_Vk_ImageSampler *output) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx->physicalGpu, FPX3D_VK_BAD_GPU_HANDLE_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  VkSampler new_sampler = {0};

  if (VK_SAMPLER_ADDRESS_MODE_MAX_ENUM == addr_mode)
    return FPX3D_ARGS_ERROR;

  VkSamplerCreateInfo s_info = {0};
  s_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  s_info.magFilter = CONDITIONAL(bilinear, VK_FILTER_LINEAR, VK_FILTER_NEAREST);
  s_info.minFilter =
      CONDITIONAL(anisotropy, VK_FILTER_LINEAR, VK_FILTER_NEAREST);

  s_info.addressModeU = addr_mode;
  s_info.addressModeV = addr_mode;
  s_info.addressModeW = addr_mode;

  VkPhysicalDeviceProperties dev_props = {0};
  VkPhysicalDeviceFeatures dev_features = {0};
  vkGetPhysicalDeviceProperties(ctx->physicalGpu, &dev_props);
  vkGetPhysicalDeviceFeatures(ctx->physicalGpu, &dev_features);

  s_info.anisotropyEnable = CONDITIONAL(
      anisotropy && dev_features.samplerAnisotropy, VK_TRUE, VK_FALSE);
  s_info.maxAnisotropy = dev_props.limits.maxSamplerAnisotropy;

  s_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  s_info.unnormalizedCoordinates = VK_FALSE;

  s_info.compareEnable = VK_FALSE;
  s_info.compareOp = VK_COMPARE_OP_ALWAYS;

  s_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  s_info.mipLodBias = 0.0f;
  s_info.minLod = 0.0f;
  s_info.maxLod = 0.0f;

  if (VK_SUCCESS != vkCreateSampler(lgpu->handle, &s_info, NULL, &new_sampler))
    return FPX3D_VK_ERROR;

  output->handle = new_sampler;
  output->isValid = true;

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _fill_image_data(Fpx3d_Vk_Image *image, void *data,
                                       size_t data_length,
                                       Fpx3d_Vk_LogicalGpu *lgpu,
                                       VkPhysicalDevice dev) {
  NULL_CHECK(image, FPX3D_ARGS_ERROR);
  NULL_CHECK(data, FPX3D_ARGS_ERROR);

  NULL_CHECK(image->image, FPX3D_VK_BAD_IMAGE_HANDLE_ERROR);
  NULL_CHECK(image->memory, FPX3D_VK_BAD_MEMORY_HANDLE_ERROR);

  struct fpx3d_vulkan_pool_queue_pair pair = _graphics_pool_and_queue(lgpu);

  if (NULL == pair.pool || NULL == pair.queue) {
    return FPX3D_GENERIC_ERROR;
  }

  data_length =
      MIN(data_length, image->dimensions.width * image->dimensions.height *
                           image->dimensions.channels *
                           image->dimensions.channelWidth);
  size_t size = data_length;

  Fpx3d_Vk_Buffer staging_buf = {0};
  FPX3D_ONFAIL(
      __fpx3d_vk_new_buffer(dev, lgpu, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            VK_SHARING_MODE_CONCURRENT, &staging_buf),
      success,
      FPX3D_ERROR("Failed to create staging buffer for image transfer");
      return success;);

  FPX3D_ONFAIL(__fpx3d_vk_data_to_buffer(lgpu, &staging_buf, data, size),
               success,
               FPX3D_ERROR("Failed to fill staging buffer with image data");
               return success;);

  FPX3D_ONFAIL(_vk_buf_to_image(staging_buf.buffer, image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                image->subresourceRange, *pair.pool,
                                *pair.queue, lgpu->handle),
               success, FPX3D_ERROR("Failed to copy staging memory into image");
               return success;);

  __fpx3d_vk_destroy_buffer_object(lgpu, &staging_buf);

  return FPX3D_SUCCESS;
}

static struct fpx3d_vulkan_pool_queue_pair
_graphics_pool_and_queue(Fpx3d_Vk_LogicalGpu *lgpu) {
  struct fpx3d_vulkan_pool_queue_pair pair = {0};

  NULL_CHECK(lgpu, pair);
  NULL_CHECK(lgpu->handle, pair);

  VkQueue *graphics_queue = NULL;
  VkCommandPool *graphics_pool = NULL;
  if (NULL != lgpu->commandPools) {
    graphics_pool = __fpx3d_vk_select_pool_of_type(GRAPHICS_POOL, lgpu);
  }

  if (NULL == graphics_pool) {
    return pair;
  }

  if (NULL != lgpu->graphicsQueues.queues && lgpu->graphicsQueues.count > 0) {
    graphics_queue =
        &lgpu->graphicsQueues.queues[lgpu->graphicsQueues.nextToUse++];
    pair.type = GRAPHICS_QUEUE;
  } else {
    return pair;
  }

  lgpu->graphicsQueues.nextToUse %= lgpu->graphicsQueues.count;

  pair.pool = graphics_pool;
  pair.queue = graphics_queue;

  return pair;
}

static Fpx3d_E_Result
_transition_image_layout(VkImage img, VkFormat fmt, VkImageLayout *old,
                         VkImageLayout new, VkImageSubresourceRange s_range,
                         VkCommandPool graphics_pool, VkQueue graphics_queue,
                         VkDevice lgpu) {
  UNUSED(fmt);

  VkCommandBuffer cbuf =
      __fpx3d_vk_begin_temp_command_buffer(graphics_pool, lgpu);

  VkImageMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout = *old,
      .newLayout = new,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = img,
      .subresourceRange = s_range,
      .srcAccessMask = 0,
      .dstAccessMask = 0};

  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM,
                       dstStage = VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM;

  switch (*old) {
  case VK_IMAGE_LAYOUT_UNDEFINED:
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    barrier.srcAccessMask = 0;
    break;

  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;

  default:
    FPX3D_ERROR("Image layout transition from %u not implemented", *old);
    return FPX3D_GENERIC_ERROR;
    break;
  }

  switch (new) {
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    break;

  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;

  default:
    FPX3D_ERROR("Image layout transition to %u not implemented", new);
    return FPX3D_GENERIC_ERROR;
    break;
  }

  // https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-access-types-supported
  vkCmdPipelineBarrier(cbuf, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1,
                       &barrier);

  Fpx3d_E_Result success = __fpx3d_vk_end_temp_command_buffer(
      cbuf, graphics_pool, graphics_queue, lgpu);
  if (FPX3D_SUCCESS != success)
    return success;

  *old = new;

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _vk_buf_to_image(VkBuffer buf, Fpx3d_Vk_Image *img,
                                       VkImageLayout layout,
                                       VkImageSubresourceRange s_range,
                                       VkCommandPool graphics_pool,
                                       VkQueue graphics_queue, VkDevice lgpu) {
  VkCommandBuffer cbuf =
      __fpx3d_vk_begin_temp_command_buffer(graphics_pool, lgpu);

  VkBufferImageCopy region = {
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {.aspectMask = s_range.aspectMask,
                           .mipLevel = s_range.baseMipLevel,
                           .baseArrayLayer = s_range.baseArrayLayer,
                           .layerCount = s_range.layerCount},
      .imageOffset = {0, 0, 0},
      .imageExtent = {.width = img->dimensions.width,
                      .height = img->dimensions.height,
                      .depth = 1}};

  vkCmdCopyBufferToImage(cbuf, buf, img->image, layout, 1, &region);

  return __fpx3d_vk_end_temp_command_buffer(cbuf, graphics_pool, graphics_queue,
                                            lgpu);
}
// END OF STATIC FUNCTIONS -------------------------------------
