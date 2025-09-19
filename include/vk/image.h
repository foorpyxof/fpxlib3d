/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_IMAGE_H
#define FPX_VK_IMAGE_H

#include "../fpx3d.h"

#include "./typedefs.h"

struct _fpx3d_vk_image_dimensions {
  uint32_t width;
  uint32_t height;
  uint32_t channels;
  uint32_t channelWidth;
};

struct _fpx3d_vk_image_sampler {
  VkSampler handle;

  bool isValid;
};

struct _fpx3d_vk_image {
  Fpx3d_Vk_ImageDimensions dimensions;
  size_t (*sizeInBytes)(Fpx3d_Vk_Image *);

  VkImage image;
  VkDeviceMemory memory;

  VkImageView imageView;

  VkFormat imageFormat;

  VkImageSubresourceRange subresourceRange;

  VkImageLayout imageLayout;
  bool isReadOnly;

  bool isValid;
};

struct _fpx3d_vk_texture {
  Fpx3d_Vk_Image *imageReference;
  Fpx3d_Vk_ImageSampler *samplerReference;

  bool isValid;
};

Fpx3d_Vk_Image fpx3d_vk_create_depth_image(Fpx3d_Vk_Context *,
                                           Fpx3d_Vk_LogicalGpu *,
                                           Fpx3d_Vk_ImageDimensions dimensions);

Fpx3d_Vk_Image
fpx3d_vk_create_texture_image(Fpx3d_Vk_Context *, Fpx3d_Vk_LogicalGpu *,
                              Fpx3d_Vk_ImageDimensions dimensions);

Fpx3d_E_Result fpx3d_vk_fill_image(Fpx3d_Vk_Image *, Fpx3d_Vk_Context *,
                                   Fpx3d_Vk_LogicalGpu *, void *data);

Fpx3d_E_Result fpx3d_vk_image_readonly(Fpx3d_Vk_Image *, Fpx3d_Vk_LogicalGpu *);

Fpx3d_E_Result fpx3d_vk_destroy_image(Fpx3d_Vk_Image *, Fpx3d_Vk_LogicalGpu *);

Fpx3d_Vk_ImageSampler fpx3d_vk_create_image_sampler(Fpx3d_Vk_Context *,
                                                    Fpx3d_Vk_LogicalGpu *,
                                                    bool bilinear_filter,
                                                    bool anisotropic_filter);

Fpx3d_E_Result fpx3d_vk_destroy_image_sampler(Fpx3d_Vk_ImageSampler *,
                                              Fpx3d_Vk_LogicalGpu *);

Fpx3d_Vk_Texture fpx3d_vk_create_texture(Fpx3d_Vk_Image *,
                                         Fpx3d_Vk_ImageSampler *);

size_t fpx3d_vk_get_image_size_bytes(Fpx3d_Vk_Image *);

#endif // FPX_VK_IMAGE_H
