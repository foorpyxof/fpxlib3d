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
#include "vk/logical_gpu.h"
#include "vk/pipeline.h"
#include "vk/shape.h"
#include "vk/typedefs.h"
#include "volk/volk.h"

#include "vk/descriptors.h"

extern Fpx3d_E_Result __fpx3d_realloc_array(void **arr_ptr, size_t obj_size,
                                            size_t amount,
                                            size_t *old_capacity);

extern Fpx3d_E_Result
__fpx3d_vk_new_buffer(VkPhysicalDevice, Fpx3d_Vk_LogicalGpu *,
                      VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags mem_flags, VkSharingMode,
                      Fpx3d_Vk_Buffer *output_buffer);

extern void __fpx3d_vk_destroy_buffer_object(Fpx3d_Vk_LogicalGpu *lgpu,
                                             Fpx3d_Vk_Buffer *buffer);

// static declarations --------------------------------------------
static Fpx3d_E_Result _bind_descriptors(Fpx3d_Vk_DescriptorSet *,
                                        Fpx3d_Vk_Context *,
                                        Fpx3d_Vk_LogicalGpu *);

static Fpx3d_E_Result _create_descriptor_buffer_write_set(
    VkWriteDescriptorSet *w_sets, Fpx3d_Vk_DescriptorSet *ds,
    size_t binding_index, size_t buffer_alignment, size_t *buffer_offset);
static Fpx3d_E_Result
_create_descriptor_image_write_set(VkWriteDescriptorSet *w_sets,
                                   Fpx3d_Vk_DescriptorSet *ds,
                                   size_t binding_index);
// end of static declarations -------------------------------------

Fpx3d_Vk_DescriptorSetLayout
fpx3d_vk_create_descriptor_set_layout(Fpx3d_Vk_DescriptorSetBinding *bindings,
                                      size_t binding_count,
                                      Fpx3d_Vk_LogicalGpu *lgpu) {
  Fpx3d_Vk_DescriptorSetLayout retval = {0};

  VkDescriptorSetLayoutBinding *layout_binds =
      (VkDescriptorSetLayoutBinding *)calloc(
          binding_count, sizeof(VkDescriptorSetLayoutBinding));

  if (NULL == layout_binds) {
    perror("calloc()");
    return retval;
  }

  for (size_t i = 0; i < binding_count; ++i) {
    VkDescriptorSetLayoutBinding *b = &layout_binds[i];

    b->binding = i;
    b->descriptorCount = bindings[i].elementCount;
    b->descriptorType = (VkDescriptorType)bindings[i].type;

    b->stageFlags = bindings[i].shaderStages;

    b->pImmutableSamplers = NULL;
  }

  VkDescriptorSetLayoutCreateInfo dsl_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = binding_count,
      .pBindings = layout_binds};

  if (VK_SUCCESS != vkCreateDescriptorSetLayout(lgpu->handle, &dsl_info, NULL,
                                                &retval.handle))
    retval.handle = VK_NULL_HANDLE;

  FREE_SAFE(layout_binds);

  retval.bindingCount = binding_count;
  retval.isValid = true;

  return retval;
}

Fpx3d_E_Result
fpx3d_vk_destroy_descriptor_set_layout(Fpx3d_Vk_DescriptorSetLayout *layout,
                                       Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(layout, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE != layout->handle && layout->isValid) {
    vkDestroyDescriptorSetLayout(lgpu->handle, layout->handle, NULL);
    memset(layout, 0, sizeof(*layout));
  }

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_DescriptorSet fpx3d_vk_create_descriptor_set(
    Fpx3d_Vk_DescriptorSetBinding *bindings, size_t binding_count,
    Fpx3d_Vk_DescriptorSetLayout *layout, Fpx3d_Vk_LogicalGpu *lgpu,
    Fpx3d_Vk_Context *ctx) {
  Fpx3d_Vk_DescriptorSet retval = {0};

  NULL_CHECK(bindings, retval);

  for (size_t i = 0; i < binding_count; ++i)
    if (bindings[i].type == DESC_UNIFORM &&
        (0 == bindings[i].elementSize || 0 == bindings[i].elementCount)) {
      // one or more buffer bindings is empty. we do not like that
      return retval;
    }

  NULL_CHECK(layout, retval);
  NULL_CHECK(ctx, retval);
  NULL_CHECK(lgpu, retval);
  NULL_CHECK(lgpu->handle, retval);

  if (false == layout->isValid)
    return retval;

  Fpx3d_E_Result alloc_success = __fpx3d_realloc_array(
      (void **)&retval.bindings, sizeof(retval.bindings[0]), binding_count,
      &retval.bindingCount);

  if (FPX3D_SUCCESS != alloc_success)
    return retval;

  size_t total_mem_size = 0;

#define POOL_TYPE_COUNT 2

  size_t pool_size_count = 0;
  VkDescriptorPoolSize p_sizes[POOL_TYPE_COUNT] = {0};
  VkDescriptorPoolSize *uniforms = NULL;
  VkDescriptorPoolSize *image_samplers = NULL;

#define WIND_DOWN_DESCRIPTOR_SET                                               \
  for (size_t j = 0; j < i; ++j)                                               \
    if (DESC_IMAGE_SAMPLER == retval.bindings[i].bindingProperties.type)       \
      FREE_SAFE(retval.bindings[i]                                             \
                    .bindingProperties.imageSampler.textureReferences);        \
  FREE_SAFE(retval.bindings);

  for (size_t i = 0; i < binding_count; ++i) {
    retval.bindings[i].bindingProperties = bindings[i];

    switch (bindings[i].type) {
    case DESC_UNIFORM:
      if (NULL == uniforms) {
        uniforms = &p_sizes[pool_size_count++];
        uniforms->type = (VkDescriptorType)DESC_UNIFORM;
      }

      uniforms->descriptorCount += bindings[i].elementCount;
      retval.bindings[i].dataOffset = total_mem_size;
      break;

    case DESC_IMAGE_SAMPLER:
      if (NULL == image_samplers) {
        image_samplers = &p_sizes[pool_size_count++];
        image_samplers->type = (VkDescriptorType)DESC_IMAGE_SAMPLER;
      }

      image_samplers->descriptorCount += bindings[i].elementCount;

      size_t temp = 0;
      retval.bindings[i].bindingProperties.imageSampler.textureReferences =
          NULL;
      FPX3D_ONFAIL(__fpx3d_realloc_array(
                       (void **)&retval.bindings[i]
                           .bindingProperties.imageSampler.textureReferences,
                       sizeof(bindings[i].imageSampler.textureReferences[0]),
                       bindings[i].elementCount, &temp),
                   success, WIND_DOWN_DESCRIPTOR_SET;
                   return retval;);
      memcpy(
          retval.bindings[i].bindingProperties.imageSampler.textureReferences,
          bindings[i].imageSampler.textureReferences,
          sizeof(bindings[i].imageSampler.textureReferences[0]) *
              bindings[i].elementCount);

      break;

    default:
      // TODO: proper handling
      break;
    }

    total_mem_size +=
        bindings[i].elementCount *
        ALIGN_UP(bindings[i].elementSize, ctx->constants.bufferAlignment);
  }
  retval.bindingCount = binding_count;

  VkDescriptorPoolCreateInfo p_info = {0};
  p_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  p_info.poolSizeCount = pool_size_count;
  p_info.pPoolSizes = p_sizes;
  p_info.maxSets = 1;

  if (VK_SUCCESS !=
      vkCreateDescriptorPool(lgpu->handle, &p_info, NULL, &retval.pool))
    return retval;

  VkDescriptorSetAllocateInfo s_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = retval.pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &layout->handle};
  if (VK_SUCCESS !=
      vkAllocateDescriptorSets(lgpu->handle, &s_info, &retval.handle)) {
    vkDestroyDescriptorPool(lgpu->handle, retval.pool, NULL);
    return retval;
  }

  __fpx3d_vk_new_buffer(
      ctx->physicalGpu, lgpu, total_mem_size,
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT /* TODO: make not hard-coded */,
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      VK_SHARING_MODE_EXCLUSIVE, &retval.buffer);

  if (retval.buffer.isValid) {
    if (VK_SUCCESS != vkMapMemory(lgpu->handle, retval.buffer.memory, 0,
                                  VK_WHOLE_SIZE, 0,
                                  &retval.buffer.mapped_memory)) {
      __fpx3d_vk_destroy_buffer_object(lgpu, &retval.buffer);
    } else {
      memset(retval.buffer.mapped_memory, 0, total_mem_size);
      retval.buffer.objectCount = 1;
      retval.buffer.stride = total_mem_size;
    }
  }

  if (false == retval.buffer.isValid) {
    FREE_SAFE(retval.bindings);
    vkDestroyDescriptorPool(lgpu->handle, retval.pool, NULL);
  } else if (FPX3D_SUCCESS != _bind_descriptors(&retval, ctx, lgpu)) {
    FREE_SAFE(retval.bindings);
    __fpx3d_vk_destroy_buffer_object(lgpu, &retval.buffer);

    vkDestroyDescriptorPool(lgpu->handle, retval.pool, NULL);
  }

  retval.layoutReference = layout;
  retval.isValid = retval.buffer.isValid;

  return retval;

#undef POOL_TYPE_COUNT
}

Fpx3d_E_Result fpx3d_vk_destroy_descriptor_set(Fpx3d_Vk_DescriptorSet *set,
                                               Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(set, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE != set->pool && set->isValid) {
    vkDestroyDescriptorPool(lgpu->handle, set->pool, NULL);
    FREE_SAFE(set->bindings);
  }

  __fpx3d_vk_destroy_buffer_object(lgpu, &set->buffer);

  memset(set, 0, sizeof(*set));

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_create_pipeline_descriptors(
    Fpx3d_Vk_Pipeline *pipeline, Fpx3d_Vk_DescriptorSetBinding *bindings,
    size_t binding_count, Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(pipeline, FPX3D_ARGS_ERROR);
  NULL_CHECK(bindings, FPX3D_ARGS_ERROR);

  {
    size_t temp = 0;
    Fpx3d_E_Result alloc_res = __fpx3d_realloc_array(
        (void **)&pipeline->bindings.inFlightDescriptorSets,
        sizeof(Fpx3d_Vk_DescriptorSet), ctx->constants.maxFramesInFlight,
        &temp);

    if (FPX3D_SUCCESS != alloc_res)
      return alloc_res;
  }

  for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
    pipeline->bindings
        .inFlightDescriptorSets[i] = fpx3d_vk_create_descriptor_set(
        bindings, binding_count,
        &pipeline->layout.descriptorSetLayouts[DESCRIPTOR_SET_INDEX_PIPELINE],
        lgpu, ctx);

    if (false == pipeline->bindings.inFlightDescriptorSets[i].isValid) {
      for (size_t j = 0; j < i; ++j)
        fpx3d_vk_destroy_descriptor_set(
            &pipeline->bindings.inFlightDescriptorSets[j], lgpu);

      return FPX3D_VK_ERROR;
    }
  }

  pipeline->bindings.rawBufferData =
      calloc(pipeline->bindings.inFlightDescriptorSets->buffer.objectCount,
             pipeline->bindings.inFlightDescriptorSets->buffer.stride);

  if (NULL == pipeline->bindings.rawBufferData) {
    perror("calloc()");

    for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
      fpx3d_vk_destroy_descriptor_set(
          &pipeline->bindings.inFlightDescriptorSets[i], lgpu);
    }

    FREE_SAFE(pipeline->bindings.inFlightDescriptorSets);

    return FPX3D_MEMORY_ERROR;
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_update_pipeline_descriptor(Fpx3d_Vk_Pipeline *pipeline,
                                                   size_t binding,
                                                   size_t element, void *value,
                                                   Fpx3d_Vk_Context *ctx) {
  NULL_CHECK(pipeline, FPX3D_ARGS_ERROR);
  NULL_CHECK(value, FPX3D_ARGS_ERROR);

  NULL_CHECK(pipeline->bindings.rawBufferData, FPX3D_NULLPTR_ERROR);

  Fpx3d_Vk_DescriptorSet *set_data = pipeline->bindings.inFlightDescriptorSets;

  NULL_CHECK(set_data, FPX3D_NULLPTR_ERROR);

  if (set_data->bindingCount <= binding)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  if (set_data->bindings[binding].bindingProperties.elementCount <= element)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  uint8_t *destination = (uint8_t *)pipeline->bindings.rawBufferData;
  destination += set_data->bindings[binding].dataOffset;
  destination +=
      element *
      ALIGN_UP(set_data->bindings[binding].bindingProperties.elementSize,
               ctx->constants.bufferAlignment);

  memcpy(destination, value,
         set_data->bindings[binding].bindingProperties.elementSize);

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_create_shape_descriptors(
    Fpx3d_Vk_Shape *shape, Fpx3d_Vk_DescriptorSetBinding *bindings,
    size_t binding_count, Fpx3d_Vk_DescriptorSetLayout *ds_layout,
    Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(shape, FPX3D_ARGS_ERROR);
  NULL_CHECK(bindings, FPX3D_ARGS_ERROR);
  NULL_CHECK(ds_layout, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  {
    size_t temp = 0;

    Fpx3d_E_Result alloc_res =
        __fpx3d_realloc_array((void **)&shape->bindings.inFlightDescriptorSets,
                              sizeof(Fpx3d_Vk_DescriptorSet),
                              ctx->constants.maxFramesInFlight, &temp);

    if (FPX3D_SUCCESS != alloc_res)
      return alloc_res;
  }

  for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
    shape->bindings.inFlightDescriptorSets[i] = fpx3d_vk_create_descriptor_set(
        bindings, binding_count, ds_layout, lgpu, ctx);

    if (false == shape->bindings.inFlightDescriptorSets[i].isValid) {
      for (size_t j = 0; j < i; ++j)
        fpx3d_vk_destroy_descriptor_set(
            &shape->bindings.inFlightDescriptorSets[j], lgpu);

      return FPX3D_VK_ERROR;
    }
  }

  shape->bindings.rawBufferData =
      calloc(shape->bindings.inFlightDescriptorSets->buffer.objectCount,
             shape->bindings.inFlightDescriptorSets->buffer.stride);

  if (NULL == shape->bindings.rawBufferData) {
    perror("calloc()");

    for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
      fpx3d_vk_destroy_descriptor_set(
          &shape->bindings.inFlightDescriptorSets[i], lgpu);
    }

    FREE_SAFE(shape->bindings.inFlightDescriptorSets);

    return FPX3D_MEMORY_ERROR;
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_update_shape_descriptor(Fpx3d_Vk_Shape *shape,
                                                size_t binding, size_t element,
                                                void *value,
                                                Fpx3d_Vk_Context *ctx,
                                                Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(shape, FPX3D_ARGS_ERROR);
  NULL_CHECK(value, FPX3D_ARGS_ERROR);

  NULL_CHECK(shape->bindings.rawBufferData, FPX3D_NULLPTR_ERROR);

  Fpx3d_Vk_DescriptorSet *set_data = shape->bindings.inFlightDescriptorSets;

  NULL_CHECK(set_data, FPX3D_NULLPTR_ERROR);

  if (set_data->bindingCount <= binding)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  if (set_data->bindings[binding].bindingProperties.elementCount <= element)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  uint8_t *buf_data_destination = (uint8_t *)shape->bindings.rawBufferData;
  Fpx3d_Vk_Texture *texture_to_apply = NULL;

  switch (set_data->bindings[binding].bindingProperties.type) {
  case DESC_UNIFORM:
    buf_data_destination += set_data->bindings[binding].dataOffset;
    buf_data_destination +=
        element *
        ALIGN_UP(set_data->bindings[binding].bindingProperties.elementSize,
                 ctx->constants.bufferAlignment);

    memcpy(buf_data_destination, value,
           set_data->bindings[binding].bindingProperties.elementSize);
    break;

  case DESC_IMAGE_SAMPLER:
    texture_to_apply = (Fpx3d_Vk_Texture *)value;
    for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
      set_data[i]
          .bindings[binding]
          .bindingProperties.imageSampler.textureReferences[element] =
          texture_to_apply;

      VkWriteDescriptorSet w_set = {0};
      _create_descriptor_image_write_set(&w_set, &set_data[i], binding);
      vkUpdateDescriptorSets(lgpu->handle, 1, &w_set, 0, NULL);
    }
    break;

  case DESC_INVALID:
    // bad
    break;
  }

  return FPX3D_SUCCESS;
}

// STATIC FUNCTIONS -----------------------------------------------
static Fpx3d_E_Result _bind_descriptors(Fpx3d_Vk_DescriptorSet *set,
                                        Fpx3d_Vk_Context *ctx,
                                        Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(set, FPX3D_ARGS_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  VkWriteDescriptorSet *w_sets = NULL;
  w_sets = (VkWriteDescriptorSet *)calloc(set->bindingCount,
                                          sizeof(VkWriteDescriptorSet));
  if (NULL == w_sets) {
    perror("calloc()");
    return FPX3D_MEMORY_ERROR;
  }

#define FREE_W_SETS                                                            \
  for (size_t i = 0; i < j; ++i) {                                             \
    void *temp = (void *)w_sets[i].pBufferInfo;                                \
    FREE_SAFE(temp);                                                           \
  }

  size_t offset_in_buffer = 0;

  for (size_t j = 0; j < set->bindingCount; ++j) {
    switch (set->bindings[j].bindingProperties.type) {
    case DESC_UNIFORM:
      FPX3D_ONFAIL(_create_descriptor_buffer_write_set(
                       w_sets, set, j, ctx->constants.bufferAlignment,
                       &offset_in_buffer),
                   success, FREE_W_SETS;
                   return success;);
      break;

    case DESC_IMAGE_SAMPLER:
      FPX3D_ONFAIL(_create_descriptor_image_write_set(w_sets, set, j), success,
                   FREE_W_SETS;
                   return success;);
      break;

    default:
      // bad

      break;
    }
  }

  vkUpdateDescriptorSets(lgpu->handle, set->bindingCount, w_sets, 0, NULL);

  for (size_t i = 0; i < set->bindingCount; ++i) {
    void *temp = (void *)w_sets[i].pBufferInfo;
    FREE_SAFE(temp);
  }
  FREE_SAFE(w_sets);

  return FPX3D_SUCCESS;
}

#define CREATE_WRITE_INFO(set, bufinfoptr, imginfoptr, txlinfoptr)             \
  {                                                                            \
    VkWriteDescriptorSet *write_set = &w_sets[binding_index];                  \
    write_set->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;                 \
    write_set->dstSet = ds->handle;                                            \
    write_set->dstBinding = binding_index;                                     \
    write_set->dstArrayElement = 0;                                            \
    write_set->descriptorCount =                                               \
        ds->bindings[binding_index].bindingProperties.elementCount;            \
    write_set->descriptorType =                                                \
        (VkDescriptorType)ds->bindings[binding_index].bindingProperties.type;  \
                                                                               \
    write_set->pBufferInfo = bufinfoptr;                                       \
    write_set->pImageInfo = imginfoptr;                                        \
    write_set->pTexelBufferView = txlinfoptr;                                  \
  }

static Fpx3d_E_Result _create_descriptor_buffer_write_set(
    VkWriteDescriptorSet *w_sets, Fpx3d_Vk_DescriptorSet *ds,
    size_t binding_index, size_t buffer_alignment, size_t *buffer_offset) {
  NULL_CHECK(w_sets, FPX3D_ARGS_ERROR);
  NULL_CHECK(ds, FPX3D_ARGS_ERROR);
  NULL_CHECK(buffer_offset, FPX3D_ARGS_ERROR);

  NULL_CHECK(ds->bindings, FPX3D_NULLPTR_ERROR);
  NULL_CHECK(ds->handle, FPX3D_VK_BAD_HANDLE_ERROR);
  NULL_CHECK(ds->buffer.buffer, FPX3D_VK_BAD_HANDLE_ERROR);

  VkDescriptorBufferInfo *b_infos = NULL;
  b_infos = (VkDescriptorBufferInfo *)calloc(
      ds->bindings[binding_index].bindingProperties.elementCount,
      sizeof(VkDescriptorBufferInfo));

  if (NULL == b_infos) {
    perror("calloc()");
    return FPX3D_MEMORY_ERROR;
  }

  for (size_t k = 0;
       k < ds->bindings[binding_index].bindingProperties.elementCount; ++k) {
    VkDescriptorBufferInfo *b = &b_infos[k];
    b->buffer = ds->buffer.buffer;
    b->offset = *buffer_offset;
    b->range = ds->bindings[binding_index].bindingProperties.elementSize;

    *buffer_offset +=
        ALIGN_UP(ds->bindings[binding_index].bindingProperties.elementSize,
                 buffer_alignment);
  }

  CREATE_WRITE_INFO(ds, b_infos, NULL, NULL);

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_create_descriptor_image_write_set(VkWriteDescriptorSet *w_sets,
                                   Fpx3d_Vk_DescriptorSet *ds,
                                   size_t binding_index) {
  NULL_CHECK(w_sets, FPX3D_ARGS_ERROR);
  NULL_CHECK(ds, FPX3D_ARGS_ERROR);

  NULL_CHECK(ds->bindings, FPX3D_NULLPTR_ERROR);
  NULL_CHECK(ds->handle, FPX3D_VK_BAD_HANDLE_ERROR);
  NULL_CHECK(ds->buffer.buffer, FPX3D_VK_BAD_HANDLE_ERROR);

  VkDescriptorImageInfo *i_infos = NULL;
  i_infos = (VkDescriptorImageInfo *)calloc(
      ds->bindings[binding_index].bindingProperties.elementCount,
      sizeof(VkDescriptorImageInfo));

  if (NULL == i_infos) {
    perror("calloc()");
    return FPX3D_MEMORY_ERROR;
  }

  for (size_t k = 0;
       k < ds->bindings[binding_index].bindingProperties.elementCount; ++k) {
    VkDescriptorImageInfo *i = &i_infos[k];
    Fpx3d_Vk_Texture *tex =
        ds->bindings[binding_index]
            .bindingProperties.imageSampler.textureReferences[k];

    if (NULL == tex || NULL == tex->imageReference ||
        NULL == tex->samplerReference) {
      FREE_SAFE(i_infos);
      return FPX3D_NULLPTR_ERROR;
    }

    i->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    i->imageView = ds->bindings[binding_index]
                       .bindingProperties.imageSampler.textureReferences[k]
                       ->imageReference->imageView;
    i->sampler = ds->bindings[binding_index]
                     .bindingProperties.imageSampler.textureReferences[k]
                     ->samplerReference->handle;
  }

  CREATE_WRITE_INFO(ds, NULL, i_infos, NULL);

  return FPX3D_SUCCESS;
}

#undef CREATE_WRITE_INFO
// END OF STATIC FUNCTIONS ----------------------------------------
