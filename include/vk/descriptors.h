/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_DESCRIPTORS_H
#define FPX_VK_DESCRIPTORS_H

#include "fpx3d.h"

#include "vk/buffer.h"
#include "vk/typedefs.h"

struct _fpx3d_vk_descriptor_set_layout {
  VkDescriptorSetLayout handle;
  size_t bindingCount;

  bool isValid;
};

struct _fpx3d_vk_descriptor_set_binding {
  size_t elementCount, elementSize;
  Fpx3d_Vk_E_DescriptorType type;
  Fpx3d_Vk_E_ShaderStage shaderStages;

  union {
    struct {
      Fpx3d_Vk_Texture **textureReferences;
    } imageSampler;
  };
};

struct _fpx3d_vk_descriptor_set {
  VkDescriptorSet handle;

  VkDescriptorPool pool;

  Fpx3d_Vk_DescriptorSetLayout *layoutReference;

  struct {
    Fpx3d_Vk_DescriptorSetBinding bindingProperties;
    size_t dataOffset;
  } *bindings;
  size_t bindingCount;

  Fpx3d_Vk_Buffer buffer;

  bool isValid;
};

Fpx3d_Vk_DescriptorSetLayout
fpx3d_vk_create_descriptor_set_layout(Fpx3d_Vk_DescriptorSetBinding *bindings,
                                      size_t binding_count,
                                      Fpx3d_Vk_LogicalGpu *);
Fpx3d_E_Result
fpx3d_vk_destroy_descriptor_set_layout(Fpx3d_Vk_DescriptorSetLayout *,
                                       Fpx3d_Vk_LogicalGpu *);

Fpx3d_Vk_DescriptorSet fpx3d_vk_create_descriptor_set(
    Fpx3d_Vk_DescriptorSetBinding *bindings, size_t binding_count,
    Fpx3d_Vk_DescriptorSetLayout *, Fpx3d_Vk_LogicalGpu *, Fpx3d_Vk_Context *);
Fpx3d_E_Result fpx3d_vk_destroy_descriptor_set(Fpx3d_Vk_DescriptorSet *,
                                               Fpx3d_Vk_LogicalGpu *);

Fpx3d_E_Result fpx3d_vk_create_pipeline_descriptors(
    Fpx3d_Vk_Pipeline *, Fpx3d_Vk_DescriptorSetBinding *bindings,
    size_t binding_count, Fpx3d_Vk_Context *, Fpx3d_Vk_LogicalGpu *);

// NOTE: depending on the type of the binding, the value of the `void *value`
// argument must differ between:
// - DESC_UNIFORM -> pointer to the 1:1 data to copy in (like a struct)
// - DESC_IMAGE_SAMPLER -> pointer to the Fpx3d_Vk_Image object to apply
Fpx3d_E_Result fpx3d_vk_update_pipeline_descriptor(Fpx3d_Vk_Pipeline *,
                                                   size_t binding,
                                                   size_t element, void *value,
                                                   Fpx3d_Vk_Context *);

Fpx3d_E_Result fpx3d_vk_create_shape_descriptors(
    Fpx3d_Vk_Shape *, Fpx3d_Vk_DescriptorSetBinding *bindings,
    size_t binding_count, Fpx3d_Vk_DescriptorSetLayout *, Fpx3d_Vk_Context *,
    Fpx3d_Vk_LogicalGpu *);

// NOTE: depending on the type of the binding, the value of the `void *value`
// argument must differ between:
// - DESC_UNIFORM -> pointer to the 1:1 data to copy in (like a struct)
// - DESC_IMAGE_SAMPLER -> pointer to the Fpx3d_Vk_Image object to apply
Fpx3d_E_Result fpx3d_vk_update_shape_descriptor(Fpx3d_Vk_Shape *,
                                                size_t binding, size_t element,
                                                void *value, Fpx3d_Vk_Context *,
                                                Fpx3d_Vk_LogicalGpu *);

#endif // FPX_VK_DESCRIPTORS_H
