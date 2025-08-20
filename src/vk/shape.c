/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "fpx3d.h"
#include "vk/buffer.h"
#include "vk/context.h"
#include "vk/descriptors.h"
#include "vk/logical_gpu.h"
#include "vk/vertex.h"

#include "vk/shape.h"

extern Fpx3d_Vk_Buffer
__fpx3d_vk_new_buffer_with_data(VkPhysicalDevice, Fpx3d_Vk_LogicalGpu *,
                                void *data, VkDeviceSize size,
                                VkBufferUsageFlags usage_flags);
extern void __fpx3d_vk_destroy_buffer_object(Fpx3d_Vk_LogicalGpu *,
                                             Fpx3d_Vk_Buffer *buffer);

// static declarations ---------------------------------------
static Fpx3d_Vk_Buffer _new_vertex_buffer(VkPhysicalDevice,
                                          Fpx3d_Vk_LogicalGpu *,
                                          Fpx3d_Vk_VertexBundle *);
static Fpx3d_Vk_Buffer _new_index_buffer(VkPhysicalDevice,
                                         Fpx3d_Vk_LogicalGpu *,
                                         Fpx3d_Vk_VertexBundle *);
// end of static declarations --------------------------------

Fpx3d_E_Result fpx3d_vk_create_shapebuffer(Fpx3d_Vk_Context *vk_ctx,
                                           Fpx3d_Vk_LogicalGpu *lgpu,
                                           Fpx3d_Vk_VertexBundle *vertex_input,
                                           Fpx3d_Vk_ShapeBuffer *shape_output) {
  NULL_CHECK(vk_ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(vertex_input, FPX3D_ARGS_ERROR);
  NULL_CHECK(shape_output, FPX3D_ARGS_ERROR);

  NULL_CHECK(vk_ctx->physicalGpu, FPX3D_VK_BAD_GPU_HANDLE_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (1 > vertex_input->vertexCount || NULL == vertex_input->vertices)
    return FPX3D_ARGS_ERROR;

  Fpx3d_Vk_Buffer vb = {0};
  Fpx3d_Vk_Buffer ib = {0};

  vb = _new_vertex_buffer(vk_ctx->physicalGpu, lgpu, vertex_input);

  if (false == vb.isValid) {
    __fpx3d_vk_destroy_buffer_object(lgpu, &vb);
    return -2;
  }

  if (0 < vertex_input->indexCount) {
    ib = _new_index_buffer(vk_ctx->physicalGpu, lgpu, vertex_input);

    if (false == ib.isValid) {
      __fpx3d_vk_destroy_buffer_object(lgpu, &vb);
      __fpx3d_vk_destroy_buffer_object(lgpu, &ib);
      return -2;
    }

    shape_output->indexBuffer = ib;
  }

  shape_output->vertexBuffer = vb;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_destroy_shapebuffer(Fpx3d_Vk_LogicalGpu *lgpu,
                                            Fpx3d_Vk_ShapeBuffer *shape) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(shape, FPX3D_ARGS_ERROR);

  __fpx3d_vk_destroy_buffer_object(lgpu, &shape->vertexBuffer);
  __fpx3d_vk_destroy_buffer_object(lgpu, &shape->indexBuffer);

  memset(shape, 0, sizeof(*shape));

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_Shape fpx3d_vk_create_shape(Fpx3d_Vk_ShapeBuffer *buffer) {
  Fpx3d_Vk_Shape retval = {0};
  NULL_CHECK(buffer, retval);

  retval.shapeBuffer = buffer;
  retval.isValid = true;

  return retval;
}

Fpx3d_E_Result fpx3d_vk_destroy_shape(Fpx3d_Vk_Shape *shape,
                                      Fpx3d_Vk_Context *ctx,
                                      Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(shape, FPX3D_ARGS_ERROR);

  if (NULL != shape->bindings.inFlightDescriptorSets)
    for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
      fpx3d_vk_destroy_descriptor_set(
          &shape->bindings.inFlightDescriptorSets[i], lgpu);
    }

  FREE_SAFE(shape->bindings.inFlightDescriptorSets);
  FREE_SAFE(shape->bindings.rawBufferData);

  memset(shape, 0, sizeof(*shape));

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_Shape fpx3d_vk_duplicate_shape(Fpx3d_Vk_Shape *subject,
                                        Fpx3d_Vk_Context *ctx,
                                        Fpx3d_Vk_LogicalGpu *lgpu) {
  Fpx3d_Vk_Shape retval = {0};

  NULL_CHECK(subject, retval);
  NULL_CHECK(ctx, retval);
  NULL_CHECK(lgpu, retval);
  NULL_CHECK(lgpu->handle, retval);

  Fpx3d_Vk_DescriptorSetBinding *bindings = NULL;
  void *raw_binding_data = NULL;

  bool descriptors = NULL != subject->bindings.inFlightDescriptorSets &&
                     NULL != subject->bindings.rawBufferData;

  if (descriptors) {
    size_t alloc_size =
        subject->bindings.inFlightDescriptorSets->buffer.objectCount *
        subject->bindings.inFlightDescriptorSets->buffer.stride;

    bindings = (Fpx3d_Vk_DescriptorSetBinding *)calloc(alloc_size, 1);

    if (NULL == bindings) {
      perror("calloc()");
      return retval;
    }

    raw_binding_data = calloc(alloc_size, 1);

    if (NULL == raw_binding_data) {
      perror("calloc()");
      FREE_SAFE(bindings);
      return retval;
    }
  }

  retval = fpx3d_vk_create_shape(subject->shapeBuffer);

  if (false == retval.isValid) {
    FREE_SAFE(bindings);
    FREE_SAFE(raw_binding_data);
    return retval;
  }

  if (descriptors) {
    for (size_t i = 0;
         i < subject->bindings.inFlightDescriptorSets->bindingCount; ++i) {
      bindings[i] = subject->bindings.inFlightDescriptorSets->bindings[i]
                        .bindingProperties;
    }

    if (FPX3D_SUCCESS !=
        fpx3d_vk_create_shape_descriptors(
            &retval, bindings,
            subject->bindings.inFlightDescriptorSets->bindingCount,
            subject->bindings.inFlightDescriptorSets->layoutReference, ctx,
            lgpu)) {
      fpx3d_vk_destroy_shape(&retval, ctx, lgpu);

      FREE_SAFE(bindings);
      FREE_SAFE(raw_binding_data);

      return retval;
    }

    memcpy(raw_binding_data, subject->bindings.rawBufferData,
           subject->bindings.inFlightDescriptorSets->buffer.objectCount *
               subject->bindings.inFlightDescriptorSets->buffer.stride);

    FREE_SAFE(bindings);
  }

  retval.bindings.rawBufferData = raw_binding_data;

  return retval;
}

// STATIC FUNCTIONS --------------------------------------------
static Fpx3d_Vk_Buffer _new_vertex_buffer(VkPhysicalDevice dev,
                                          Fpx3d_Vk_LogicalGpu *lgpu,
                                          Fpx3d_Vk_VertexBundle *v_bundle) {
  Fpx3d_Vk_Buffer new_buf = __fpx3d_vk_new_buffer_with_data(
      dev, lgpu, v_bundle->vertices,
      v_bundle->vertexCount * v_bundle->vertexDataSize,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  if (false == new_buf.isValid)
    return new_buf;

  new_buf.objectCount = v_bundle->vertexCount;
  new_buf.stride = v_bundle->vertexDataSize;

  return new_buf;
}

static Fpx3d_Vk_Buffer _new_index_buffer(VkPhysicalDevice dev,
                                         Fpx3d_Vk_LogicalGpu *lgpu,
                                         Fpx3d_Vk_VertexBundle *v_bundle) {
  Fpx3d_Vk_Buffer new_buf = __fpx3d_vk_new_buffer_with_data(
      dev, lgpu, v_bundle->indices,
      v_bundle->indexCount * sizeof(v_bundle->indices[0]),
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  if (false == new_buf.isValid)
    return new_buf;

  new_buf.objectCount = v_bundle->indexCount;
  new_buf.stride = sizeof(v_bundle->indices[0]);

  return new_buf;
}
// END OF STATIC FUNCTIONS ------------------------------------
