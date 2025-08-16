/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

// LOCAL
#include "fpx3d.h"

#define VOLK_IMPLEMENTATION
#include "vk.h"

#include "window.h"

#ifdef DEBUG
#define FPX_VK_USE_VALIDATION_LAYERS
#define FPX3D_DEBUG_ENABLE
#endif
#include "debug.h"
// END OF LOCAL

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

// EXTERNALS
#include "GLFW/glfw3.h"
// END OF EXTERNALS

// constants
#define PIPELINE_DESCRIPTOR_SET_IDX 0
#define OBJECT_DESCRIPTOR_SET_IDX 1

#define HIGHEST_DESCRIPTOR_SET_IDX OBJECT_DESCRIPTOR_SET_IDX

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

#define FREE_SAFE(ptr)                                                         \
  {                                                                            \
    if (NULL != ptr) {                                                         \
      free(ptr);                                                               \
      ptr = NULL;                                                              \
    }                                                                          \
  }

#define NULL_CHECK(value, ret_code)                                            \
  if (NULL == (value))                                                         \
  return ret_code

// const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

#ifndef MAX_IN_FLIGHT
#define MAX_IN_FLIGHT 2
#endif

#define UNUSED(var)                                                            \
  {                                                                            \
    char _fpx_lineinfo_output_buffer[sizeof(__FILE__) + 16];                   \
    FPX3D_LINE_INFO(_fpx_lineinfo_output_buffer);                              \
    FPX3D_DEBUG("Variable %s is unused (at: %s)", #var,                        \
                _fpx_lineinfo_output_buffer);                                  \
    if (fmt) {                                                                 \
    }                                                                          \
  }

#define LGPU_CHECK(lgpu, retval)                                               \
  if (VK_NULL_HANDLE == (lgpu)->handle) {                                      \
    return retval;                                                             \
  }

#define SELECT_POOL_OF_TYPE(__type, l_gpu, output_ptr)                         \
  for (size_t i = 0; i < l_gpu->commandPoolCapacity; ++i) {                    \
    if (__type == l_gpu->commandPools[i].type) {                               \
      output_ptr = &l_gpu->commandPools[i].pool;                               \
      break;                                                                   \
    }                                                                          \
  }

struct fpx3d_vk_qf_holder {
  Fpx3d_Vk_QueueFamily g_family, p_family, t_family;
};

struct fpx3d_vulkan_pool_queue_pair {
  VkCommandPool *pool;
  VkQueue *queue;

  Fpx3d_Vk_E_QueueType type;
};

static VkFormat _fpx3d_vk_formats_lookup_table[][5] = {
    {0, 0, 0, 0, 0},
    {0, VK_FORMAT_R8_SRGB, VK_FORMAT_R8G8_SRGB, VK_FORMAT_R8G8B8_SRGB,
     VK_FORMAT_R8G8B8A8_SRGB}};

//
//  START OF STATIC FUNCTION DECLARATIONS
//

static size_t _align_up(size_t number, size_t alignment);

static VkCommandBuffer _begin_temp_command_buffer(VkCommandPool graphics_pool,
                                                  VkDevice lgpu);

static Fpx3d_E_Result _end_temp_command_buffer(VkCommandBuffer,
                                               VkCommandPool graphics_pool,
                                               VkQueue graphics_queue,
                                               VkDevice lgpu);

static Fpx3d_E_Result _new_memory(VkPhysicalDevice, Fpx3d_Vk_LogicalGpu *,
                                  VkMemoryPropertyFlags, VkMemoryRequirements,
                                  VkDeviceMemory *output);

static Fpx3d_E_Result _new_buffer(VkPhysicalDevice, Fpx3d_Vk_LogicalGpu *,
                                  VkDeviceSize size, VkBufferUsageFlags,
                                  VkMemoryPropertyFlags, VkSharingMode,
                                  Fpx3d_Vk_Buffer *output);

static Fpx3d_E_Result _data_to_buffer(Fpx3d_Vk_LogicalGpu *, Fpx3d_Vk_Buffer *,
                                      void *data, VkDeviceSize size);

static Fpx3d_E_Result _vk_bufcopy(VkDevice lgpu, VkQueue transfer_queue,
                                  Fpx3d_Vk_Buffer *src, Fpx3d_Vk_Buffer *dst,
                                  VkDeviceSize size,
                                  VkCommandPool transfer_cmd_pool);

static Fpx3d_Vk_Buffer _new_buffer_with_data(VkPhysicalDevice dev,
                                             Fpx3d_Vk_LogicalGpu *lgpu,
                                             void *data, VkDeviceSize size,
                                             VkBufferUsageFlags usage_flags);

static void _destroy_buffer_object(Fpx3d_Vk_LogicalGpu *, Fpx3d_Vk_Buffer *);

static Fpx3d_Vk_Buffer _new_vertex_buffer(VkPhysicalDevice,
                                          Fpx3d_Vk_LogicalGpu *,
                                          Fpx3d_Vk_VertexBundle *);

static Fpx3d_Vk_Buffer _new_index_buffer(VkPhysicalDevice,
                                         Fpx3d_Vk_LogicalGpu *,
                                         Fpx3d_Vk_VertexBundle *);

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

static struct fpx3d_vulkan_pool_queue_pair
_graphics_pool_and_queue(Fpx3d_Vk_LogicalGpu *lgpu);

static Fpx3d_E_Result _new_image(VkPhysicalDevice dev,
                                 Fpx3d_Vk_LogicalGpu *lgpu,
                                 Fpx3d_Vk_ImageDimensions, VkFormat fmt,
                                 VkImageTiling tiling, VkImageUsageFlags usage,
                                 Fpx3d_Vk_Image *output);

static Fpx3d_E_Result _fill_image_data(Fpx3d_Vk_Image *, void *data,
                                       size_t data_length,
                                       Fpx3d_Vk_LogicalGpu *, VkPhysicalDevice);

static VkShaderModule *_select_module_stage(Fpx3d_Vk_ShaderModuleSet *set,
                                            Fpx3d_Vk_E_ShaderStage stage);

static VkShaderModule _new_shader_module(Fpx3d_Vk_LogicalGpu *lgpu,
                                         Fpx3d_Vk_SpirvFile *spirv);

static void _glfw_resize_callback(GLFWwindow *, int width, int height);

static void _destroy_lgpu(Fpx3d_Vk_Context *, Fpx3d_Vk_LogicalGpu *);

static void _destroy_command_pool(Fpx3d_Vk_LogicalGpu *,
                                  Fpx3d_Vk_CommandPool *);

static Fpx3d_E_Result _realloc_array(void **arr, size_t obj_size, size_t amount,
                                     size_t *old_capacity);

static bool _qf_meets_requirements(VkQueueFamilyProperties,
                                   Fpx3d_Vk_QueueFamilyRequirements *,
                                   size_t qf_index);

static Fpx3d_Vk_QueueFamily
_choose_queue_family(Fpx3d_Vk_Context *, Fpx3d_Vk_QueueFamilyRequirements *);

static struct fpx3d_vulkan_queues *
_get_queues_ptr_by_type(Fpx3d_Vk_LogicalGpu *lgpu, Fpx3d_Vk_E_QueueType type);

static Fpx3d_E_Result _destroy_swapchain(Fpx3d_Vk_LogicalGpu *,
                                         Fpx3d_Vk_Swapchain *, bool force);

static Fpx3d_E_Result _retire_current_swapchain(Fpx3d_Vk_LogicalGpu *);

static VkExtent2D _new_swapchain_extent(Fpx3d_Wnd_Context *wnd,
                                        VkSurfaceCapabilitiesKHR cap);

static Fpx3d_E_Result _construct_command_pool(Fpx3d_Vk_LogicalGpu *,
                                              Fpx3d_Vk_CommandPool *,
                                              Fpx3d_Vk_E_CommandPoolType);

static bool _surface_format_picker(VkPhysicalDevice dev, VkSurfaceKHR sfc,
                                   VkSurfaceFormatKHR *fmts, size_t fmt_count,
                                   VkSurfaceFormatKHR *output);

static bool _present_mode_picker(VkPhysicalDevice dev, VkSurfaceKHR sfc,
                                 VkPresentModeKHR *mds, size_t md_count,
                                 VkPresentModeKHR *output);

static Fpx3d_E_Result _find_queue_families(Fpx3d_Vk_Context *ctx,
                                           size_t g_queues, size_t p_queues,
                                           size_t t_queues,
                                           struct fpx3d_vk_qf_holder *output);

static Fpx3d_E_Result _create_queues(Fpx3d_Vk_LogicalGpu *,
                                     Fpx3d_Vk_E_QueueType, size_t count);
static Fpx3d_E_Result _create_all_available_queues(Fpx3d_Vk_LogicalGpu *);

static Fpx3d_E_Result _bind_descriptors_to_buffer(Fpx3d_Vk_DescriptorSet *,
                                                  Fpx3d_Vk_Context *,
                                                  Fpx3d_Vk_LogicalGpu *);
//
//  END OF STATIC FUNCTION DECLARATIONS
//

// -----------------------------------------------------

//
//  START OF PUBLIC UTILITY FUNCTION IMPLEMENTATIONS
//

Fpx3d_E_Result
fpx3d_vk_set_required_surfaceformats(Fpx3d_Vk_SwapchainRequirements *reqs,
                                     VkSurfaceFormatKHR *formats,
                                     size_t count) {
  NULL_CHECK(reqs, FPX3D_ARGS_ERROR);
  NULL_CHECK(formats, FPX3D_ARGS_ERROR);

  if (1 > count)
    return FPX3D_SUCCESS;

  {
    reqs->surfaceFormats = formats;
    reqs->surfaceFormatsCount = count;
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result
fpx3d_vk_set_required_presentmodes(Fpx3d_Vk_SwapchainRequirements *reqs,
                                   VkPresentModeKHR *modes, size_t count) {
  NULL_CHECK(reqs, FPX3D_ARGS_ERROR);
  NULL_CHECK(modes, FPX3D_ARGS_ERROR);

  if (1 > count)
    return FPX3D_SUCCESS;

  {
    reqs->presentModes = modes;
    reqs->presentModesCount = count;
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result
fpx3d_vk_blacklist_queuefamily_index(Fpx3d_Vk_QueueFamilyRequirements *reqs,
                                     size_t index) {
  NULL_CHECK(reqs, FPX3D_ARGS_ERROR);

  if (63 < index)
    return FPX3D_ARGS_ERROR;

  reqs->indexBlacklistBits |= (1 << index);

  return FPX3D_SUCCESS;
}

// vecX is an array of X floats
Fpx3d_E_Result fpx3d_set_vertex_position(Fpx3d_Vk_Vertex *vert, vec3 pos) {
  NULL_CHECK(vert, FPX3D_ARGS_ERROR);
  NULL_CHECK(pos, FPX3D_ARGS_ERROR);

  memcpy(vert->position, pos, sizeof(vec3));

  return FPX3D_SUCCESS;
}

// vecX is an array of X floats
Fpx3d_E_Result fpx3d_set_vertex_color(Fpx3d_Vk_Vertex *vert, vec3 color) {
  NULL_CHECK(vert, FPX3D_ARGS_ERROR);
  NULL_CHECK(color, FPX3D_ARGS_ERROR);

  memcpy(vert->color, color, sizeof(vec3));

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_allocate_vertices(Fpx3d_Vk_VertexBundle *bundle,
                                          size_t amount,
                                          size_t single_vertex_size) {
  if (1 > amount || 1 > single_vertex_size)
    return FPX3D_SUCCESS;

  NULL_CHECK(bundle, FPX3D_ARGS_ERROR);

  bundle->vertexDataSize = single_vertex_size;

  return _realloc_array((void **)&bundle->vertices, single_vertex_size, amount,
                        &bundle->vertexCapacity);
}

Fpx3d_E_Result fpx3d_vk_append_vertices(Fpx3d_Vk_VertexBundle *bundle,
                                        void *vertices, size_t amount) {
  if (1 > amount)
    return FPX3D_SUCCESS;

  NULL_CHECK(bundle, FPX3D_ARGS_ERROR);
  NULL_CHECK(vertices, FPX3D_ARGS_ERROR);

  if (1 > bundle->vertexDataSize)
    return FPX3D_SUCCESS;

  if (bundle->vertexCount + amount > bundle->vertexCapacity)
    return FPX3D_GENERIC_ERROR;

  NULL_CHECK(bundle->vertices, FPX3D_VK_NULLPTR_ERROR);

  memcpy((uint8_t *)bundle->vertices +
             (bundle->vertexCount * bundle->vertexDataSize),
         vertices, amount * bundle->vertexDataSize);

  bundle->vertexCount += amount;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_set_indices(Fpx3d_Vk_VertexBundle *bundle,
                                    uint32_t *indices, size_t amount) {
  if (1 > amount)
    return FPX3D_SUCCESS;

  NULL_CHECK(bundle, FPX3D_ARGS_ERROR);
  NULL_CHECK(indices, FPX3D_ARGS_ERROR);

  uint32_t *ind = (uint32_t *)calloc(amount, sizeof(*indices));
  if (NULL == ind) {
    perror("calloc()");
    return FPX3D_MEMORY_ERROR;
  }

  memcpy(ind, indices, amount * sizeof(*indices));

  FREE_SAFE(bundle->indices);
  bundle->indices = ind;
  bundle->indexCount = amount;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_free_vertices(Fpx3d_Vk_VertexBundle *bundle) {
  NULL_CHECK(bundle, FPX3D_ARGS_ERROR);

  FREE_SAFE(bundle->vertices);
  FREE_SAFE(bundle->indices);

  memset(bundle, 0, sizeof(*bundle));

  return FPX3D_SUCCESS;
}

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
    _destroy_buffer_object(lgpu, &vb);
    return -2;
  }

  if (0 < vertex_input->indexCount) {
    ib = _new_index_buffer(vk_ctx->physicalGpu, lgpu, vertex_input);

    if (false == ib.isValid) {
      _destroy_buffer_object(lgpu, &vb);
      _destroy_buffer_object(lgpu, &ib);
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

  _destroy_buffer_object(lgpu, &shape->vertexBuffer);
  _destroy_buffer_object(lgpu, &shape->indexBuffer);

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
  FREE_SAFE(shape->bindings.rawData);

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
  LGPU_CHECK(lgpu, retval);

  Fpx3d_Vk_DescriptorSetBinding *bindings = NULL;
  void *raw_binding_data = NULL;

  bool descriptors = NULL != subject->bindings.inFlightDescriptorSets &&
                     NULL != subject->bindings.rawData;

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

    memcpy(raw_binding_data, subject->bindings.rawData,
           subject->bindings.inFlightDescriptorSets->buffer.objectCount *
               subject->bindings.inFlightDescriptorSets->buffer.stride);

    FREE_SAFE(bindings);
  }

  retval.bindings.rawData = raw_binding_data;

  return retval;
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
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  {
    size_t temp = 0;

    Fpx3d_E_Result alloc_res =
        _realloc_array((void **)&shape->bindings.inFlightDescriptorSets,
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

  shape->bindings.rawData =
      calloc(shape->bindings.inFlightDescriptorSets->buffer.objectCount,
             shape->bindings.inFlightDescriptorSets->buffer.stride);

  if (NULL == shape->bindings.rawData) {
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
                                                Fpx3d_Vk_Context *ctx) {
  NULL_CHECK(shape, FPX3D_ARGS_ERROR);
  NULL_CHECK(value, FPX3D_ARGS_ERROR);

  NULL_CHECK(shape->bindings.rawData, FPX3D_VK_NULLPTR_ERROR);

  Fpx3d_Vk_DescriptorSet *set_data = shape->bindings.inFlightDescriptorSets;

  NULL_CHECK(set_data, FPX3D_VK_NULLPTR_ERROR);

  if (set_data->bindingCount <= binding)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  if (set_data->bindings[binding].bindingProperties.elementCount <= element)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  uint8_t *destination = (uint8_t *)shape->bindings.rawData;
  destination += set_data->bindings[binding].dataOffset;
  destination +=
      element *
      _align_up(set_data->bindings[binding].bindingProperties.elementSize,
                ctx->constants.bufferAlignment);

  memcpy(destination, value,
         set_data->bindings[binding].bindingProperties.elementSize);

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_create_pipeline_descriptors(
    Fpx3d_Vk_Pipeline *pipeline, Fpx3d_Vk_DescriptorSetBinding *bindings,
    size_t binding_count, Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(pipeline, FPX3D_ARGS_ERROR);
  NULL_CHECK(bindings, FPX3D_ARGS_ERROR);

  {
    size_t temp = 0;
    Fpx3d_E_Result alloc_res =
        _realloc_array((void **)&pipeline->bindings.inFlightDescriptorSets,
                       sizeof(Fpx3d_Vk_DescriptorSet),
                       ctx->constants.maxFramesInFlight, &temp);

    if (FPX3D_SUCCESS != alloc_res)
      return alloc_res;
  }

  for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
    pipeline->bindings.inFlightDescriptorSets[i] =
        fpx3d_vk_create_descriptor_set(
            bindings, binding_count,
            &pipeline->layout.descriptorSetLayouts[PIPELINE_DESCRIPTOR_SET_IDX],
            lgpu, ctx);

    if (false == pipeline->bindings.inFlightDescriptorSets[i].isValid) {
      for (size_t j = 0; j < i; ++j)
        fpx3d_vk_destroy_descriptor_set(
            &pipeline->bindings.inFlightDescriptorSets[j], lgpu);

      return FPX3D_VK_ERROR;
    }
  }

  pipeline->bindings.rawData =
      calloc(pipeline->bindings.inFlightDescriptorSets->buffer.objectCount,
             pipeline->bindings.inFlightDescriptorSets->buffer.stride);

  if (NULL == pipeline->bindings.rawData) {
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

  NULL_CHECK(pipeline->bindings.rawData, FPX3D_VK_NULLPTR_ERROR);

  Fpx3d_Vk_DescriptorSet *set_data = pipeline->bindings.inFlightDescriptorSets;

  NULL_CHECK(set_data, FPX3D_VK_NULLPTR_ERROR);

  if (set_data->bindingCount <= binding)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  if (set_data->bindings[binding].bindingProperties.elementCount <= element)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  uint8_t *destination = (uint8_t *)pipeline->bindings.rawData;
  destination += set_data->bindings[binding].dataOffset;
  destination +=
      element *
      _align_up(set_data->bindings[binding].bindingProperties.elementSize,
                ctx->constants.bufferAlignment);

  memcpy(destination, value,
         set_data->bindings[binding].bindingProperties.elementSize);

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_assign_shapes_to_pipeline(Fpx3d_Vk_Shape **shapes,
                                                  size_t count,
                                                  Fpx3d_Vk_Pipeline *pipeline) {
  NULL_CHECK(pipeline, FPX3D_ARGS_ERROR);

  if (1 > count)
    return FPX3D_SUCCESS;

  NULL_CHECK(shapes, FPX3D_ARGS_ERROR);

  _realloc_array((void **)&pipeline->graphics.shapes, sizeof(*shapes), count,
                 &pipeline->graphics.shapeCount);

  if (NULL == pipeline->graphics.shapes) {
    perror("realloc()");
    return FPX3D_MEMORY_ERROR;
  }

  memcpy(pipeline->graphics.shapes, shapes, count * sizeof(*shapes));

  pipeline->graphics.shapeCount = count;

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_SpirvFile fpx3d_vk_read_spirv_file(const char *filename,
                                            Fpx3d_Vk_E_ShaderStage stage) {
  Fpx3d_Vk_SpirvFile retval = {0};

  FILE *fp = fopen(filename, "rb");
  if (NULL == fp) {
    perror("fopen()");
    FPX3D_ERROR("Could not open file \"%s\". Does it exist in this location?",
                filename);
    return retval;
  }

  if (0 > fseek(fp, 0, SEEK_END)) {
    perror("fseek()");
    fclose(fp);
    return retval;
  }

  int size = ftell(fp);
  if (0 > size) {
    perror("ftell()");
    fclose(fp);
    return retval;
  }

  if (4 > size)
    return retval;

  rewind(fp);

  uint32_t magic = 0;
  fread(&magic, sizeof(uint32_t), 1, fp);
  if (0x07230203 != magic) {
    // bad file format (probably)
    // also add more checking, bcs this isn't good enough lol
    return retval;
  }

  rewind(fp);

  FPX3D_DEBUG("Found file \"%s\" (%d bytes)", filename, size);

  // align to 4 bytes (sizeof uint32_t) because the shader module reads it
  // using a uint32_t pointer for some reason
  retval.buffer =
      (uint8_t *)malloc(size + (sizeof(uint32_t) - (size % sizeof(uint32_t))));
  if (NULL == retval.buffer) {
    perror("malloc()");
    fclose(fp);
    return retval;
  }

  size_t readcount = fread(retval.buffer, 1, size, fp);
  if (size > (int)readcount) {
    FPX3D_WARN(
        "Read too little from SPIR-V file (expected %d; got %" LONG_FORMAT "u)",
        size, readcount);

    int has_eof = feof(fp);
    int has_err = ferror(fp);

    if (has_eof || has_err) {
      FPX3D_WARN("File EOF? -> %d | File ERR? -> %d", has_eof, has_err);
    }

    FREE_SAFE(retval.buffer);
    fclose(fp);
    return retval;
  }

  retval.filesize = size;
  retval.stage = stage;

  fclose(fp);

  return retval;
}

Fpx3d_E_Result fpx3d_vk_destroy_spirv_file(Fpx3d_Vk_SpirvFile *spirv) {
  FREE_SAFE(spirv->buffer);
  memset(spirv, 0, sizeof(*spirv));

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_load_shadermodules(Fpx3d_Vk_SpirvFile *spirvs,
                                           size_t spirv_count,
                                           Fpx3d_Vk_LogicalGpu *lgpu,
                                           Fpx3d_Vk_ShaderModuleSet *output) {
  NULL_CHECK(spirvs, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  Fpx3d_Vk_ShaderModuleSet set = {0};

  size_t stage_count = 0;

  for (size_t i = 0; i < spirv_count; ++i) {
    if (SHADER_STAGE_INVALID == spirvs[i].stage) {
      char lineinfo[64] = {0};
      FPX3D_LINE_INFO(lineinfo);
      FPX3D_WARN("Invalid SPIR-V file at %s", lineinfo);

      continue;
    }

    VkShaderModule *module_in_output =
        _select_module_stage(output, spirvs[i].stage);
    VkShaderModule *module_in_retval =
        _select_module_stage(&set, spirvs[i].stage);

    if (NULL == module_in_output || NULL == module_in_retval ||
        VK_NULL_HANDLE != *module_in_output) {
      continue;
    }

    ++stage_count;

    *module_in_retval = _new_shader_module(lgpu, &spirvs[i]);
    if (VK_NULL_HANDLE == *module_in_retval) {
      fpx3d_vk_destroy_shadermodules(&set, lgpu);
      return FPX3D_GENERIC_ERROR;
    }
  }

  if (0 == stage_count) {
    return FPX3D_VK_NO_SHADER_STAGES;
  }

#define COPY_IF_NOT_EXIST(dst, src)                                            \
  {                                                                            \
    if (VK_NULL_HANDLE == dst.handle) {                                        \
      dst.handle = src.handle;                                                 \
    }                                                                          \
  }

  COPY_IF_NOT_EXIST(output->vertex, set.vertex);
  COPY_IF_NOT_EXIST(output->tesselationControl, set.tesselationControl);
  COPY_IF_NOT_EXIST(output->tesselationEvaluation, set.tesselationEvaluation);
  COPY_IF_NOT_EXIST(output->geometry, set.geometry);
  COPY_IF_NOT_EXIST(output->fragment, set.fragment);

  return FPX3D_SUCCESS;
}

#define DESTROY_IF_EXISTS(mod)                                                 \
  {                                                                            \
    if (VK_NULL_HANDLE != mod.handle) {                                        \
      vkDestroyShaderModule(lgpu->handle, mod.handle, NULL);                   \
    }                                                                          \
  }

Fpx3d_E_Result
fpx3d_vk_destroy_shadermodules(Fpx3d_Vk_ShaderModuleSet *to_destroy,
                               Fpx3d_Vk_LogicalGpu *lgpu) {

  DESTROY_IF_EXISTS(to_destroy->vertex);
  DESTROY_IF_EXISTS(to_destroy->tesselationControl);
  DESTROY_IF_EXISTS(to_destroy->tesselationEvaluation);
  DESTROY_IF_EXISTS(to_destroy->geometry);
  DESTROY_IF_EXISTS(to_destroy->fragment);

  memset(to_destroy, 0, sizeof(*to_destroy));

  return FPX3D_SUCCESS;
}

#undef DESTROY_IF_EXISTS

Fpx3d_Vk_Image
fpx3d_vk_create_texture_image(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu,
                              Fpx3d_Vk_ImageDimensions dimensions) {
  Fpx3d_Vk_Image retval = {0};
  NULL_CHECK(ctx, retval);
  NULL_CHECK(lgpu, retval);
  LGPU_CHECK(lgpu, retval);

  if (1 > dimensions.channels || 1 > dimensions.height ||
      1 > dimensions.width || 1 > dimensions.channelWidth)
    return retval;

  _new_image(ctx->physicalGpu, lgpu, dimensions,
             _fpx3d_vk_formats_lookup_table[dimensions.channelWidth]
                                           [dimensions.channels],
             VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, &retval);

  return retval;
}

Fpx3d_E_Result fpx3d_vk_fill_texture_image(Fpx3d_Vk_Image *img,
                                           Fpx3d_Vk_Context *ctx,
                                           Fpx3d_Vk_LogicalGpu *lgpu,
                                           void *data) {
  NULL_CHECK(img, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(data, FPX3D_ARGS_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

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
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

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
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  vkDestroyImage(lgpu->handle, image->image, NULL);
  vkFreeMemory(lgpu->handle, image->memory, NULL);

  memset(image, 0, sizeof(*image));

  return FPX3D_SUCCESS;
}

bool fpx3d_vk_instance_layers_supported(const char **layers,
                                        size_t layer_count) {
  if (1 > layer_count)
    return true;

  uint32_t available = 0;
  VkLayerProperties *available_layers = NULL;

  vkEnumerateInstanceLayerProperties(&available, NULL);

  if (available < layer_count)
    return false;

  available_layers =
      (VkLayerProperties *)calloc(available, sizeof(VkLayerProperties));

  if (NULL == available_layers) {
    perror("calloc()");
    FPX3D_ERROR("Error while checking for Vulkan validation layers");
    return false;
  }

  vkEnumerateInstanceLayerProperties(&available, available_layers);

  for (size_t i = 0; i < layer_count; ++i) {
    uint8_t found = false;

    for (uint32_t j = 0; j < available; ++j) {
      if (0 == strcmp(layers[i], available_layers[j].layerName)) {
        found = true;
        break;
      }
    }

    if (false == found)
      return false;
  }

  return true;
}

bool fpx3d_vk_device_extensions_supported(VkPhysicalDevice dev,
                                          const char **extensions,
                                          size_t extension_count) {
  NULL_CHECK(dev, false);

  if (extension_count == 0)
    return true;

  uint32_t available = 0;
  VkExtensionProperties *available_extensions = NULL;

  vkEnumerateDeviceExtensionProperties(dev, NULL, &available, NULL);

  if (available < 1 || available < extension_count)
    return false;

  available_extensions =
      (VkExtensionProperties *)calloc(available, sizeof(VkExtensionProperties));

  if (NULL == available_extensions) {
    perror("calloc()");
    FPX3D_ERROR("Error while checking for Vulkan device extensions");
    return false;
  }

  vkEnumerateDeviceExtensionProperties(dev, NULL, &available,
                                       available_extensions);

  for (size_t i = 0; i < extension_count; ++i) {
    uint8_t found = false;

    for (uint32_t j = 0; j < available; ++j) {
      if (0 == strcmp(extensions[i], available_extensions[j].extensionName)) {
        found = true;
        break;
      }
    }

    if (false == found)
      return false;
  }

  return true;
}

Fpx3d_Vk_SwapchainProperties
fpx3d_vk_get_swapchain_support(Fpx3d_Vk_Context *ctx, VkPhysicalDevice dev,
                               Fpx3d_Vk_SwapchainRequirements reqs) {
  Fpx3d_Vk_SwapchainProperties props = {0};

  const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  bool are_supported = fpx3d_vk_device_extensions_supported(dev, &ext, 1);

  if (false == are_supported)
    return props;

  props.surfaceFormatValid =
      _surface_format_picker(dev, ctx->vkSurface, reqs.surfaceFormats,
                             reqs.surfaceFormatsCount, &props.surfaceFormat);

  props.presentModeValid =
      _present_mode_picker(dev, ctx->vkSurface, reqs.presentModes,
                           reqs.presentModesCount, &props.presentMode);

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, ctx->vkSurface,
                                            &props.surfaceCapabilities);

  return props;
}

//
//   END OF PUBLIC UTILITY FUNCTION IMPLEMENTATIONS
//

// -----------------------------------------------------

//
//   START OF LIBRARY IMPLEMENTATION
//

Fpx3d_E_Result fpx3d_vk_set_custom_pointer(Fpx3d_Vk_Context *ctx, void *ptr) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  ctx->customPointer = ptr;

  return FPX3D_SUCCESS;
}

void *fpx3d_vk_get_custom_pointer(Fpx3d_Vk_Context *ctx) {
  NULL_CHECK(ctx, NULL);

  return ctx->customPointer;
}

Fpx3d_Wnd_Context *fpx3d_vk_get_windowcontext(Fpx3d_Vk_Context *vk_ctx) {
  NULL_CHECK(vk_ctx, NULL);

  return vk_ctx->windowContext;
}

Fpx3d_E_Result fpx3d_vk_init_context(Fpx3d_Vk_Context *ctx,
                                     Fpx3d_Wnd_Context *wnd) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  VkResult volk_found = volkInitialize();
  if (VK_SUCCESS != volk_found)
    return FPX3D_VK_ERROR;

  ctx->windowContext = wnd;

  // user can change this
  ctx->constants.maxFramesInFlight = 1;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_create_window(Fpx3d_Vk_Context *ctx) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  Fpx3d_Wnd_Context *wnd = ctx->windowContext;

  NULL_CHECK(wnd, FPX3D_WND_INVALID_DETAILS_ERROR);

  if (VK_STRUCTURE_TYPE_APPLICATION_INFO != ctx->appInfo.sType)
    return FPX3D_VK_APPINFO_ERROR;

  if (NULL == wnd->windowTitle || 1 > wnd->windowDimensions[0] ||
      1 > wnd->windowDimensions[1])
    return FPX3D_WND_INVALID_DETAILS_ERROR;

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  const char *val_layer_ext = "VK_LAYER_KHRONOS_validation";

  bool vl_supported = true;

  if (false == fpx3d_vk_instance_layers_supported(&val_layer_ext, 1)) {

    FPX3D_WARN("Validation layers are not available. Proceeding without. "
               "Have you installed the SDK?");

    vl_supported = false;
  }
#endif

  // we check for the queried instance layers, if need be.
  // if no layers are set to be queried, we just keep going

  if (NULL != ctx->instanceLayers && 0 < ctx->instanceLayerCount) {
    if (false == fpx3d_vk_instance_layers_supported(ctx->instanceLayers,
                                                    ctx->instanceLayerCount)) {
      FPX3D_WARN("Requested instance layers not available. Aborting "
                 "instance+window creation.");
      return FPX3D_VK_BAD_INSTANCE_LAYERS;
    }
  }

  VkInstanceCreateInfo inst_info = {0};

  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  wnd->glfwWindow =
      glfwCreateWindow(wnd->windowDimensions[0], wnd->windowDimensions[1],
                       wnd->windowTitle, NULL, NULL);

  if (NULL == wnd->glfwWindow) {
    return FPX3D_WND_WINDOW_CREATE_ERROR;
  }

  inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  inst_info.pApplicationInfo = &ctx->appInfo;

  size_t layer_count = ctx->instanceLayerCount;
  size_t alloc_count = layer_count;
  size_t layer_iter = 0;

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  if (vl_supported)
    ++alloc_count;
#endif

  const char **total_layers =
      (const char **)malloc(sizeof(const char **) * alloc_count);

  for (; layer_iter < layer_count; ++layer_iter) {
    total_layers[layer_iter] = ctx->instanceLayers[layer_iter];
  }

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  if (vl_supported) {
    ++layer_count;
    total_layers[layer_iter] = val_layer_ext;
  }
#endif

  inst_info.enabledLayerCount = layer_count;
  inst_info.ppEnabledLayerNames = total_layers;

  uint32_t glfw_extensions_count = 0;
  const char **glfw_extensions = NULL;

  glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);

  FPX3D_DEBUG("%u extensions supported.", glfw_extensions_count);

  inst_info.enabledExtensionCount = glfw_extensions_count;
  inst_info.ppEnabledExtensionNames = glfw_extensions;

  VkResult res = vkCreateInstance(&inst_info, NULL, &ctx->vkInstance);

  FREE_SAFE(total_layers);

  if (VK_SUCCESS != res)
    return FPX3D_VK_INSTANCE_CREATE_ERROR;

  volkLoadInstance(ctx->vkInstance);

  res = glfwCreateWindowSurface(ctx->vkInstance, ctx->windowContext->glfwWindow,
                                NULL, &ctx->vkSurface);

  if (VK_SUCCESS != res)
    return FPX3D_VK_SURFACE_CREATE_ERROR;

  glfwSetWindowUserPointer(ctx->windowContext->glfwWindow, ctx->windowContext);
  glfwSetWindowSizeCallback(ctx->windowContext->glfwWindow,
                            _glfw_resize_callback);

  FPX3D_DEBUG("Successfully created Vulkan instance, window and surface");

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_destroy_window(Fpx3d_Vk_Context *ctx,
                                       void (*destruction_callback)(void *)) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  Fpx3d_Wnd_Context *wnd = ctx->windowContext;

  NULL_CHECK(wnd, FPX3D_VK_BAD_WINDOW_CONTEXT_ERROR);

  if (NULL != destruction_callback)
    destruction_callback(ctx->customPointer);

  if (NULL != ctx->logicalGpus) {
    for (size_t i = 0; i < ctx->logicalGpuCapacity; ++i) {
      _destroy_lgpu(ctx, &ctx->logicalGpus[i]);
    }

    FREE_SAFE(ctx->logicalGpus);
  }

  ctx->logicalGpuCapacity = 0;

  if (VK_NULL_HANDLE != ctx->vkInstance)
    vkDestroySurfaceKHR(ctx->vkInstance, ctx->vkSurface, NULL);

  if (NULL != wnd->glfwWindow)
    glfwDestroyWindow(wnd->glfwWindow);

  if (VK_NULL_HANDLE != ctx->vkSurface)
    vkDestroyInstance(ctx->vkInstance, NULL);

  glfwTerminate();

  memset(ctx, 0, sizeof(*ctx));

  return FPX3D_SUCCESS;
}

struct _scored_gpu {
  VkPhysicalDevice gpu;
  int score;
};

Fpx3d_E_Result fpx3d_vk_select_gpu(Fpx3d_Vk_Context *ctx,
                                   int (*scoring_function)(Fpx3d_Vk_Context *,
                                                           VkPhysicalDevice)) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(scoring_function, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx->vkInstance, FPX3D_VK_BAD_VULKAN_INSTANCE_ERROR);

  VkPhysicalDevice *gpus = NULL;
  struct _scored_gpu *scored_gpus = NULL;

  uint32_t gpus_available = 0;

  {
    int success =
        vkEnumeratePhysicalDevices(ctx->vkInstance, &gpus_available, NULL);

    if (0 == gpus_available || VK_SUCCESS != success) {
      FPX3D_DEBUG("No Vulkan compatible GPU's were found!");
      return FPX3D_VK_NO_VULKAN_GPU_ERROR;
    }
  }

  // memory allocation for temporary buffers
  {
    gpus = (VkPhysicalDevice *)calloc(gpus_available, sizeof(VkPhysicalDevice));
    if (NULL == gpus) {
      perror("calloc()");
      return FPX3D_MEMORY_ERROR;
    }

    scored_gpus = (struct _scored_gpu *)calloc(gpus_available,
                                               sizeof(struct _scored_gpu));
    if (NULL == scored_gpus) {
      perror("calloc()");
      FREE_SAFE(gpus);
      return FPX3D_MEMORY_ERROR;
    }
  }

  vkEnumeratePhysicalDevices(ctx->vkInstance, &gpus_available, gpus);

  // auto sort the GPU's based on score, high to low
  for (ssize_t i = 0; i < gpus_available; ++i) {
    if (ctx->lgpuExtensionCount > 0 &&
        false == fpx3d_vk_device_extensions_supported(
                     gpus[i], ctx->lgpuExtensions, ctx->lgpuExtensionCount))
      continue;

    VkPhysicalDeviceProperties dev_props = {0};
    vkGetPhysicalDeviceProperties(gpus[i], &dev_props);

    FPX3D_DEBUG("Found GPU #%" LONG_FORMAT "d - \"%s\"", i,
                dev_props.deviceName);

    int score = scoring_function(ctx, gpus[i]);

    // yipee magic
    for (ssize_t j = i; j >= 0; --j) {
      if (scored_gpus[j].score < score && 0 < i && j < (gpus_available - 1))
        memcpy(&scored_gpus[j + 1], &scored_gpus[j], sizeof(*scored_gpus));

      if (0 == j || scored_gpus[j - 1].score >= score) {
        scored_gpus[j].gpu = gpus[i];
        scored_gpus[j].score = score;

        break;
      }
    }

    for (int j = i; j >= 0; --j) {
    }
  }

  int retval = 0;
  if (1 > scored_gpus[0].score)
    retval = FPX3D_VK_NO_SUITABLE_VULKAN_GPU_ERROR;
  else
    ctx->physicalGpu = scored_gpus[0].gpu;

  FREE_SAFE(gpus);
  FREE_SAFE(scored_gpus);

  VkPhysicalDeviceProperties dev_props = {0};
  vkGetPhysicalDeviceProperties(ctx->physicalGpu, &dev_props);

  ctx->constants.bufferAlignment =
      MAX(dev_props.limits.minUniformBufferOffsetAlignment,
          dev_props.limits.minStorageBufferOffsetAlignment);

  FPX3D_DEBUG("Successfully picked a GPU to use");
  {
    char print_string[128] = {0};
    size_t amount_formatted =
        snprintf(print_string, sizeof(print_string) - 1,
                 " Using Vulkan GPU \"%s\"", dev_props.deviceName);
    char dash_bar[128] = {0};
    snprintf(dash_bar, MIN(sizeof(dash_bar) - 1, amount_formatted + 2),
             "-----------------------------------------------------------------"
             "-------------------------------------------------------------");
    fprintf(stderr, "%s\n%s\n%s\n", dash_bar, print_string, dash_bar);
  }

  return retval;
}

Fpx3d_E_Result fpx3d_vk_allocate_logicalgpus(Fpx3d_Vk_Context *ctx,
                                             size_t amount) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);

  return _realloc_array((void **)&ctx->logicalGpus, sizeof(Fpx3d_Vk_LogicalGpu),
                        amount, &ctx->logicalGpuCapacity);
}

Fpx3d_E_Result fpx3d_vk_create_logicalgpu_at(Fpx3d_Vk_Context *ctx,
                                             size_t index,
                                             VkPhysicalDeviceFeatures features,
                                             size_t g_queues, size_t p_queues,
                                             size_t t_queues) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx->physicalGpu, FPX3D_VK_BAD_GPU_HANDLE_ERROR);
  NULL_CHECK(ctx->vkSurface, FPX3D_VK_BAD_VULKAN_INSTANCE_ERROR);
  NULL_CHECK(ctx->logicalGpus, FPX3D_VK_NULLPTR_ERROR);

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
    _destroy_lgpu(ctx, &new_lgpu);

    return FPX3D_MEMORY_ERROR;
  }

  VkFenceCreateInfo f_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
  };

  if (FPX3D_SUCCESS != _construct_command_pool(&new_lgpu,
                                               &new_lgpu.inFlightCommandPool,
                                               GRAPHICS_POOL)) {
    _destroy_lgpu(ctx, &new_lgpu);
    return FPX3D_VK_ERROR;
  }

  {
    new_lgpu.inFlightCommandPool.buffers = (VkCommandBuffer *)calloc(
        ctx->constants.maxFramesInFlight, sizeof(VkCommandBuffer));

    if (NULL == new_lgpu.inFlightCommandPool.buffers) {
      perror("calloc()");

      _destroy_lgpu(ctx, &new_lgpu);
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
      _destroy_lgpu(ctx, &new_lgpu);
    }

    new_lgpu.inFlightCommandPool.bufferCount = ctx->constants.maxFramesInFlight;
  }

  for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
    if (VK_SUCCESS != vkCreateFence(new_lgpu.handle, &f_info, NULL,
                                    &new_lgpu.inFlightFences[i])) {
      _destroy_lgpu(ctx, &new_lgpu);
      return FPX3D_VK_ERROR;
    }
  }

  if (FPX3D_SUCCESS != _create_all_available_queues(&new_lgpu)) {
    _destroy_lgpu(ctx, &new_lgpu);
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
  NULL_CHECK(ctx->logicalGpus, FPX3D_VK_NULLPTR_ERROR);

  if (ctx->logicalGpuCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  LGPU_CHECK(&ctx->logicalGpus[index], FPX3D_VK_LGPU_INVALID_ERROR);

  _destroy_lgpu(ctx, &ctx->logicalGpus[index]);

  return FPX3D_SUCCESS;
}

VkQueue *fpx3d_vk_get_queue_at(Fpx3d_Vk_LogicalGpu *lgpu, size_t index,
                               Fpx3d_Vk_E_QueueType q_type) {
  NULL_CHECK(lgpu, NULL);
  LGPU_CHECK(lgpu, NULL);

  struct fpx3d_vulkan_queues *queues = _get_queues_ptr_by_type(lgpu, q_type);

  NULL_CHECK(queues, NULL);

  if (queues->count <= index)
    return NULL;

  return &queues->queues[index];
}

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
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

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
  NULL_CHECK(layout, retval);
  NULL_CHECK(ctx, retval);
  NULL_CHECK(lgpu, retval);
  LGPU_CHECK(lgpu, retval);

  if (false == layout->isValid)
    return retval;

  Fpx3d_E_Result alloc_success =
      _realloc_array((void **)&retval.bindings, sizeof(retval.bindings[0]),
                     binding_count, &retval.bindingCount);

  if (FPX3D_SUCCESS != alloc_success)
    return retval;

  size_t total_mem_size = 0;

#define POOL_TYPE_COUNT 1

  VkDescriptorPoolSize p_sizes[POOL_TYPE_COUNT] = {0};
  VkDescriptorPoolSize *uniforms = &p_sizes[0];
  uniforms->type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

  for (size_t i = 0; i < binding_count; ++i) {
    if (bindings[i].type == DESC_UNIFORM)
      uniforms->descriptorCount += bindings[i].elementCount;

    retval.bindings[i].bindingProperties = bindings[i];
    retval.bindings[i].dataOffset = total_mem_size;

    total_mem_size +=
        bindings[i].elementCount *
        _align_up(bindings[i].elementSize, ctx->constants.bufferAlignment);
  }
  retval.bindingCount = binding_count;

  VkDescriptorPoolCreateInfo p_info = {0};
  p_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  p_info.poolSizeCount = POOL_TYPE_COUNT;
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

  _new_buffer(
      ctx->physicalGpu, lgpu, total_mem_size,
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT /* TODO: make not hard-coded */,
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      VK_SHARING_MODE_EXCLUSIVE, &retval.buffer);

  if (retval.buffer.isValid) {
    if (VK_SUCCESS != vkMapMemory(lgpu->handle, retval.buffer.memory, 0,
                                  VK_WHOLE_SIZE, 0,
                                  &retval.buffer.mapped_memory)) {
      _destroy_buffer_object(lgpu, &retval.buffer);
    } else {
      memset(retval.buffer.mapped_memory, 0, total_mem_size);
      retval.buffer.objectCount = 1;
      retval.buffer.stride = total_mem_size;
    }
  }

  if (false == retval.buffer.isValid) {
    FREE_SAFE(retval.bindings);
    vkDestroyDescriptorPool(lgpu->handle, retval.pool, NULL);
  } else if (FPX3D_SUCCESS != _bind_descriptors_to_buffer(&retval, ctx, lgpu)) {
    FREE_SAFE(retval.bindings);
    _destroy_buffer_object(lgpu, &retval.buffer);

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
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE != set->pool && set->isValid) {
    vkDestroyDescriptorPool(lgpu->handle, set->pool, NULL);
    FREE_SAFE(set->bindings);
  }

  _destroy_buffer_object(lgpu, &set->buffer);

  memset(set, 0, sizeof(*set));

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_allocate_renderpasses(Fpx3d_Vk_LogicalGpu *lgpu,
                                              size_t count) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);

  return _realloc_array((void **)&lgpu->renderPasses, sizeof(VkRenderPass),
                        count, &lgpu->renderPassCapacity);
}

Fpx3d_E_Result fpx3d_vk_create_renderpass_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                             size_t index) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->renderPasses, FPX3D_VK_NULLPTR_ERROR);

  if (lgpu->renderPassCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  if (VK_FORMAT_UNDEFINED == lgpu->currentSwapchain.imageFormat) {
    return FPX3D_VK_ERROR;
  }

  // TODO: make modular. currently hardcoded to follow tutorial
  // maybe have the programmer pass an array of color attachments,
  // or maybe subpasses

  FPX3D_TODO("Make fpx3d_vk_create_renderpass_at() modular");

  VkRenderPass pass = VK_NULL_HANDLE;

  VkAttachmentDescription color_attachment = {0};
  color_attachment.format = lgpu->currentSwapchain.imageFormat;
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
    int success = vkCreateRenderPass(lgpu->handle, &r_info, NULL, &pass);

    if (VK_SUCCESS != success) {
      return FPX3D_VK_ERROR;
    }
  }

  if (VK_NULL_HANDLE != lgpu->renderPasses[index])
    vkDestroyRenderPass(lgpu->handle, lgpu->renderPasses[index], NULL);

  lgpu->renderPasses[index] = pass;

  return FPX3D_SUCCESS;
}

VkRenderPass *fpx3d_vk_get_renderpass_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                         size_t index) {
  NULL_CHECK(lgpu, NULL);
  NULL_CHECK(lgpu->renderPasses, NULL);

  if (lgpu->renderPassCapacity <= index)
    return NULL;

  return &lgpu->renderPasses[index];
}

Fpx3d_E_Result fpx3d_vk_destroy_renderpass_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                              size_t index) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->renderPasses, FPX3D_VK_NULLPTR_ERROR);

  if (lgpu->renderPassCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  vkDestroyRenderPass(lgpu->handle, lgpu->renderPasses[index], NULL);

  lgpu->renderPasses[index] = VK_NULL_HANDLE;

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result
fpx3d_vk_create_swapchain(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu,
                          Fpx3d_Vk_SwapchainRequirements sc_reqs) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);
  NULL_CHECK(ctx->vkSurface, FPX3D_VK_BAD_VULKAN_SURFACE_ERROR);
  NULL_CHECK(ctx->windowContext, FPX3D_VK_BAD_WINDOW_CONTEXT_ERROR);

  NULL_CHECK(lgpu->graphicsQueues.queues, FPX3D_NO_CAPACITY_ERROR);
  NULL_CHECK(lgpu->presentQueues.queues, FPX3D_NO_CAPACITY_ERROR);

  Fpx3d_Vk_SwapchainProperties props =
      fpx3d_vk_get_swapchain_support(ctx, ctx->physicalGpu, sc_reqs);

  if (0 == lgpu->graphicsQueues.count || 0 == lgpu->graphicsQueues.count)
    return FPX3D_NO_CAPACITY_ERROR;

  if (false == props.presentModeValid || false == props.surfaceFormatValid)
    return FPX3D_VK_INVALID_SWAPCHAIN_PROPERTIES_ERROR;

  const VkSurfaceCapabilitiesKHR cap = props.surfaceCapabilities;

  VkSwapchainCreateInfoKHR s_info = {0};
  VkExtent2D extent = _new_swapchain_extent(ctx->windowContext, cap);

  uint32_t qf_indices[2] = {lgpu->graphicsQueues.queueFamilyIndex,
                            lgpu->presentQueues.queueFamilyIndex};

  {
    uint32_t image_count = cap.minImageCount + 1;
    if (cap.maxImageCount > 0 && image_count > cap.maxImageCount)
      image_count = cap.maxImageCount;

    s_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    s_info.surface = ctx->vkSurface;

    s_info.minImageCount = image_count;

    s_info.imageFormat = props.surfaceFormat.format;
    s_info.imageColorSpace = props.surfaceFormat.colorSpace;

    s_info.imageExtent = extent;
    s_info.imageArrayLayers = 1;
    s_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (lgpu->graphicsQueues.queueFamilyIndex !=
        lgpu->presentQueues.queueFamilyIndex) {
      s_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      s_info.queueFamilyIndexCount = 2;
      s_info.pQueueFamilyIndices = qf_indices;
    } else {
      s_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      s_info.queueFamilyIndexCount = 0;
      s_info.pQueueFamilyIndices = NULL;
    }

    s_info.preTransform = cap.currentTransform;
    s_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    s_info.presentMode = props.presentMode;
    s_info.clipped = VK_TRUE;

    s_info.oldSwapchain = lgpu->currentSwapchain.swapchain;
  }

  VkSwapchainKHR new_swapchain = {0};

  VkResult success =
      vkCreateSwapchainKHR(lgpu->handle, &s_info, NULL, &new_swapchain);

  if (VK_SUCCESS != success) {
    FPX3D_ERROR("Error while creating a new swapchain. Code: %d.", success);
    return FPX3D_VK_SWAPCHAIN_CREATE_ERROR;
  }

  // whatever happens here, if there was an old_swapchain, it is now
  // retired.
  if (VK_NULL_HANDLE != lgpu->currentSwapchain.swapchain)
    if (FPX3D_SUCCESS != _retire_current_swapchain(lgpu)) {
      _destroy_swapchain(lgpu, &lgpu->currentSwapchain, true);
      return FPX3D_VK_ERROR;
    }

  uint32_t frame_count = 0;
  VkImage *images = NULL;
  vkGetSwapchainImagesKHR(lgpu->handle, new_swapchain, &frame_count, NULL);

  images = (VkImage *)calloc(frame_count, sizeof(VkImage));
  if (NULL == images) {
    perror("calloc()");

    vkDestroySwapchainKHR(lgpu->handle, new_swapchain, NULL);

    return FPX3D_MEMORY_ERROR;
  }

  VkImageView *views = (VkImageView *)calloc(frame_count, sizeof(VkImageView));

  if (NULL == views) {
    perror("calloc()");

    vkDestroySwapchainKHR(lgpu->handle, new_swapchain, NULL);

    FREE_SAFE(images);
    return FPX3D_MEMORY_ERROR;
  }

  vkGetSwapchainImagesKHR(lgpu->handle, new_swapchain, &frame_count, images);

#define DEINITIALIZE_SWAPCHAIN                                                 \
  for (size_t _iter = 0; _iter < frame_count; ++_iter) {                       \
    if (VK_NULL_HANDLE != views[_iter])                                        \
      vkDestroyImageView(lgpu->handle, views[_iter], NULL);                    \
    FREE_SAFE(images[_iter]);                                                  \
    FREE_SAFE(views[_iter]);                                                   \
    vkDestroySwapchainKHR(lgpu->handle, new_swapchain, NULL);                  \
  }

  for (uint32_t i = 0; i < frame_count; ++i) {
    VkImageViewCreateInfo v_info = {0};

    v_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    v_info.image = images[i];
    v_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v_info.format = props.surfaceFormat.format;

    v_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    v_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    v_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    v_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    v_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    v_info.subresourceRange.baseMipLevel = 0;
    v_info.subresourceRange.levelCount = 1;
    v_info.subresourceRange.baseArrayLayer = 0;
    v_info.subresourceRange.layerCount = 1;

    VkResult success =
        vkCreateImageView(lgpu->handle, &v_info, NULL, &views[i]);

    if (VK_SUCCESS != success) {
      FPX3D_ERROR("Error while setting up Vulkan swap chain. Code: %d.",
                  success);

      DEINITIALIZE_SWAPCHAIN;

      return FPX3D_VK_ERROR;
    }
  }

  Fpx3d_Vk_SwapchainFrame *frames = (Fpx3d_Vk_SwapchainFrame *)calloc(
      frame_count, sizeof(Fpx3d_Vk_SwapchainFrame));
  if (NULL == frames) {
    perror("calloc()");

    DEINITIALIZE_SWAPCHAIN;

    return FPX3D_MEMORY_ERROR;
  }

  {
    VkSemaphoreCreateInfo sema_info = {0};
    sema_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo f_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                .flags = VK_FENCE_CREATE_SIGNALED_BIT};

    for (uint32_t i = 0; i < frame_count; ++i) {
      if (VK_SUCCESS != vkCreateSemaphore(lgpu->handle, &sema_info, NULL,
                                          &frames[i].writeAvailable) ||
          VK_SUCCESS != vkCreateSemaphore(lgpu->handle, &sema_info, NULL,
                                          &frames[i].renderFinished)) {

        if (VK_NULL_HANDLE == frames[i].writeAvailable)
          vkDestroySemaphore(lgpu->handle, frames[i].writeAvailable, NULL);

        if (VK_NULL_HANDLE == frames[i].renderFinished)
          vkDestroySemaphore(lgpu->handle, frames[i].renderFinished, NULL);

        FREE_SAFE(frames);

        DEINITIALIZE_SWAPCHAIN;

        return FPX3D_VK_ERROR;
      }

      vkCreateFence(lgpu->handle, &f_info, NULL, &frames[i].idleFence);

      frames[i].image = images[i];
      frames[i].view = views[i];
    }

    VkSemaphore sema = VK_NULL_HANDLE;

    if (VK_NULL_HANDLE == lgpu->currentSwapchain.acquireSemaphore) {
      if (VK_SUCCESS !=
          vkCreateSemaphore(lgpu->handle, &sema_info, NULL, &sema)) {
        FREE_SAFE(frames);

        DEINITIALIZE_SWAPCHAIN;

        return FPX3D_VK_ERROR;
      }

      lgpu->currentSwapchain.acquireSemaphore = sema;
    }
  }

#undef DEINITIALIZE_SWAPCHAIN

  lgpu->currentSwapchain.requirements = sc_reqs;
  lgpu->currentSwapchain.properties = props;
  lgpu->currentSwapchain.imageFormat = props.surfaceFormat.format;
  lgpu->currentSwapchain.swapchain = new_swapchain;
  lgpu->currentSwapchain.swapchainExtent = extent;
  lgpu->currentSwapchain.frames = frames;
  lgpu->currentSwapchain.frameCount = frame_count;

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_Swapchain *fpx3d_vk_get_current_swapchain(Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(lgpu, NULL);

  return &lgpu->currentSwapchain;
}

Fpx3d_E_Result fpx3d_vk_destroy_current_swapchain(Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  return _destroy_swapchain(lgpu, &lgpu->currentSwapchain, false);
}

Fpx3d_E_Result fpx3d_vk_refresh_current_swapchain(Fpx3d_Vk_Context *ctx,
                                                  Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(ctx->windowContext, FPX3D_VK_BAD_WINDOW_CONTEXT_ERROR);
  NULL_CHECK(ctx->windowContext->glfwWindow, FPX3D_WND_BAD_WINDOW_HANDLE_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  int width = 0, height = 0;
  glfwGetFramebufferSize(ctx->windowContext->glfwWindow, &width, &height);

  while (0 == width || 0 == height) {
    glfwGetFramebufferSize(ctx->windowContext->glfwWindow, &width, &height);
    glfwWaitEvents();
  }

  Fpx3d_Vk_Swapchain *old_chain = fpx3d_vk_get_current_swapchain(lgpu);
  VkRenderPass *pass = old_chain->renderPassReference;
  vkDeviceWaitIdle(lgpu->handle);
  fpx3d_vk_create_swapchain(ctx, lgpu, lgpu->currentSwapchain.requirements);
  Fpx3d_Vk_Swapchain *new_chain = fpx3d_vk_get_current_swapchain(lgpu);
  fpx3d_vk_create_framebuffers(new_chain, lgpu, pass);

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_SwapchainFrame *fpx3d_vk_get_swapchain_frame_at(Fpx3d_Vk_Swapchain *sc,
                                                         size_t index) {
  NULL_CHECK(sc, NULL);
  NULL_CHECK(sc->frames, NULL);

  if (sc->frameCount <= index)
    return NULL;

  return &sc->frames[index];
}

Fpx3d_E_Result fpx3d_vk_present_swapchain_frame_at(Fpx3d_Vk_Swapchain *sc,
                                                   size_t index,
                                                   VkQueue *present_queue) {
  NULL_CHECK(sc, FPX3D_ARGS_ERROR);
  NULL_CHECK(present_queue, FPX3D_ARGS_ERROR);

  NULL_CHECK(sc->frames, FPX3D_VK_NULLPTR_ERROR);
  NULL_CHECK(sc->swapchain, FPX3D_VK_SWAPCHAIN_INVALID_ERROR);

  if (sc->frameCount <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  uint32_t idx = (uint32_t)index;

  VkPresentInfoKHR p_info = {0};
  p_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  p_info.waitSemaphoreCount = 1;
  p_info.pWaitSemaphores = &sc->frames[index].renderFinished;

  p_info.swapchainCount = 1;
  p_info.pSwapchains = &sc->swapchain;
  p_info.pImageIndices = &idx;

  p_info.pResults = NULL;

  {
    VkResult success = vkQueuePresentKHR(*present_queue, &p_info);

    if (VK_ERROR_OUT_OF_DATE_KHR == success) {
      return FPX3D_VK_FRAME_OUT_OF_DATE_ERROR;
    } else if (VK_SUBOPTIMAL_KHR == success) {
      return FPX3D_VK_FRAME_SUBOPTIMAL_ERROR;
    }
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_create_framebuffers(Fpx3d_Vk_Swapchain *sc,
                                            Fpx3d_Vk_LogicalGpu *lgpu,
                                            VkRenderPass *render_pass) {
  NULL_CHECK(sc, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(render_pass, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(sc->frames, FPX3D_VK_NULLPTR_ERROR);

  for (size_t i = 0; i < sc->frameCount; ++i) {
    VkFramebuffer new_fb = VK_NULL_HANDLE;

    VkFramebufferCreateInfo fb_info = {0};
    fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_info.renderPass = *render_pass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &sc->frames[i].view;
    fb_info.width = sc->swapchainExtent.width;
    fb_info.height = sc->swapchainExtent.height;
    fb_info.layers = 1;

    {
      if (VK_SUCCESS !=
          vkCreateFramebuffer(lgpu->handle, &fb_info, NULL, &new_fb))
        return FPX3D_VK_ERROR;
    }

    sc->frames[i].framebuffer = new_fb;
  }

  sc->renderPassReference = render_pass;

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_PipelineLayout
fpx3d_vk_create_pipeline_layout(Fpx3d_Vk_DescriptorSetLayout *ds_layouts,
                                size_t ds_layout_count,
                                Fpx3d_Vk_LogicalGpu *lgpu) {
  Fpx3d_Vk_PipelineLayout retval = {0};

  NULL_CHECK(lgpu, retval);
  LGPU_CHECK(lgpu, retval);

  if (NULL == ds_layouts || 1 > ds_layout_count) {
    ds_layouts = NULL;
    ds_layout_count = 0;
  } else {
    for (size_t i = 0; i < ds_layout_count; ++i) {
      if (false == ds_layouts[i].isValid) {
        return retval;
      }
    }
  }

  VkDescriptorSetLayout *ds_layout_handles = NULL;

  if (0 < ds_layout_count) {

    Fpx3d_E_Result alloc_success =
        _realloc_array((void **)&retval.descriptorSetLayouts,
                       sizeof(retval.descriptorSetLayouts[0]), ds_layout_count,
                       &retval.descriptorSetLayoutCount);

    if (FPX3D_SUCCESS != alloc_success)
      return retval;

    ds_layout_handles = malloc(ds_layout_count * sizeof(VkDescriptorSetLayout));

    if (NULL == ds_layout_handles) {
      perror("malloc()");
      return retval;
    }

    for (size_t i = 0; i < ds_layout_count; ++i) {
      ds_layout_handles[i] = ds_layouts[i].handle;
    }

    memcpy(retval.descriptorSetLayouts, ds_layouts,
           ds_layout_count * sizeof(*ds_layouts));
  }

  VkPipelineLayoutCreateInfo pl_info = {0};
  pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pl_info.setLayoutCount = ds_layout_count;
  pl_info.pSetLayouts = ds_layout_handles;
  pl_info.pushConstantRangeCount = 0;
  pl_info.pPushConstantRanges = NULL;

  int result =
      vkCreatePipelineLayout(lgpu->handle, &pl_info, NULL, &retval.handle);

  FREE_SAFE(ds_layout_handles);

  if (VK_SUCCESS != result) {
    FREE_SAFE(retval.descriptorSetLayouts);
    return retval;
  }

  FREE_SAFE(ds_layout_handles);

  retval.isValid = true;

  return retval;
}

Fpx3d_E_Result fpx3d_vk_destroy_pipeline_layout(Fpx3d_Vk_PipelineLayout *layout,
                                                Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(layout, FPX3D_ARGS_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE != layout->handle && layout->isValid) {
    vkDestroyPipelineLayout(lgpu->handle, layout->handle, NULL);
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_allocate_pipelines(Fpx3d_Vk_LogicalGpu *lgpu,
                                           size_t amount) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  return _realloc_array((void **)&lgpu->pipelines, sizeof(lgpu->pipelines[0]),
                        amount, &lgpu->pipelineCapacity);
}

Fpx3d_E_Result fpx3d_vk_create_graphics_pipeline_at(
    Fpx3d_Vk_LogicalGpu *lgpu, size_t index, Fpx3d_Vk_PipelineLayout *p_layout,
    VkRenderPass *render_pass, Fpx3d_Vk_ShaderModuleSet *shaders,
    Fpx3d_Vk_VertexBinding *vertex_bindings, size_t vertex_bind_count) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(shaders, FPX3D_ARGS_ERROR);
  NULL_CHECK(render_pass, FPX3D_ARGS_ERROR);
  NULL_CHECK(*render_pass, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->pipelines, FPX3D_VK_NULLPTR_ERROR);

  if (0 == ((ptrdiff_t)shaders->vertex.handle |
            (ptrdiff_t)shaders->tesselationControl.handle |
            (ptrdiff_t)shaders->tesselationEvaluation.handle |
            (ptrdiff_t)shaders->geometry.handle |
            (ptrdiff_t)shaders->fragment.handle))
    return FPX3D_VK_NO_SHADER_STAGES;

  if (lgpu->pipelineCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  uint32_t bind_count = 0;
  VkVertexInputBindingDescription *bindings = NULL;

  uint32_t attr_count = 0;
  VkVertexInputAttributeDescription *attributes = NULL;

  if (NULL != vertex_bindings && 0 < vertex_bind_count) {
    bind_count = vertex_bind_count;

    bindings = (VkVertexInputBindingDescription *)malloc(
        vertex_bind_count * sizeof(VkVertexInputBindingDescription));

    if (NULL == bindings) {
      perror("malloc()");
      return FPX3D_MEMORY_ERROR;
    }

    for (size_t i = 0; i < bind_count; ++i) {
      attr_count += vertex_bindings[i].attributeCount;
      bindings[i].binding = i;
      bindings[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      bindings[i].stride = vertex_bindings[i].sizePerVertex;
    }

    attributes = (VkVertexInputAttributeDescription *)malloc(
        attr_count * sizeof(VkVertexInputAttributeDescription));

    if (NULL == attributes) {
      perror("malloc()");

      FREE_SAFE(bindings);

      return FPX3D_MEMORY_ERROR;
    }

    size_t attrs_copied = 0;

    // format lookup table so we don't need switch/case HELL
    // this matches enum Fpx3d_Vk_VertexAttribute.format
    VkFormat possible_formats[] = {0,
                                   VK_FORMAT_R16G16_SFLOAT,
                                   VK_FORMAT_R16G16B16_SFLOAT,
                                   VK_FORMAT_R16G16B16A16_SFLOAT,
                                   VK_FORMAT_R32G32_SFLOAT,
                                   VK_FORMAT_R32G32B32_SFLOAT,
                                   VK_FORMAT_R32G32B32A32_SFLOAT,
                                   VK_FORMAT_R64G64_SFLOAT,
                                   VK_FORMAT_R64G64B64_SFLOAT,
                                   VK_FORMAT_R64G64B64A64_SFLOAT};

    for (size_t i = 0; i < bind_count; ++i) {
      for (size_t j = 0; j < vertex_bindings[i].attributeCount; ++j) {
        if (vertex_bindings[i].attributes[j].format >=
            FPX3D_VK_FORMAT_MAXVALUE) {
          // uh oh oopsie
          FREE_SAFE(bindings);
          FREE_SAFE(attributes);

          return FPX3D_VK_INVALID_FORMAT_ERROR;
        }

        attributes[attrs_copied].binding = i;
        attributes[attrs_copied].format =
            possible_formats[vertex_bindings[i].attributes[j].format];
        attributes[attrs_copied].location = j;
        attributes[attrs_copied].offset =
            vertex_bindings[i].attributes[j].dataOffsetBytes;

        ++attrs_copied;
      }
    }
  }

  VkPipelineVertexInputStateCreateInfo v_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = bind_count,
      .pVertexBindingDescriptions = bindings,

      .vertexAttributeDescriptionCount = attr_count,
      .pVertexAttributeDescriptions = attributes,
  };

  size_t infos_created = 0;
  VkPipelineShaderStageCreateInfo stage_infos[8] = {0};

#define PIPELINE_STAGE(mod, stage_bit)                                         \
  if (VK_NULL_HANDLE != mod.handle) {                                          \
    VkPipelineShaderStageCreateInfo s_info = {                                 \
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,          \
        .stage = stage_bit,                                                    \
        .module = mod.handle,                                                  \
        .pName = "main",                                                       \
    };                                                                         \
    stage_infos[infos_created++] = s_info;                                     \
  }

  PIPELINE_STAGE(shaders->vertex, VK_SHADER_STAGE_VERTEX_BIT);
  PIPELINE_STAGE(shaders->tesselationControl,
                 VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
  PIPELINE_STAGE(shaders->tesselationEvaluation,
                 VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
  PIPELINE_STAGE(shaders->geometry, VK_SHADER_STAGE_GEOMETRY_BIT);
  PIPELINE_STAGE(shaders->fragment, VK_SHADER_STAGE_FRAGMENT_BIT);

#undef PIPELINE_STAGE

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo d_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = sizeof(dynamic_states) / sizeof(*dynamic_states),
      .pDynamicStates = dynamic_states,
  };

  VkPipelineInputAssemblyStateCreateInfo a_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = VK_FALSE,
  };

  VkPipelineViewportStateCreateInfo vs_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
      // the viewport and scissor are dynamic, at render time.
      // we only need to specify how many there will be (1 and 1)
  };

  VkPipelineRasterizationStateCreateInfo rs_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,

      .polygonMode = VK_POLYGON_MODE_FILL,
      .lineWidth = 1.0f,

      .cullMode = VK_CULL_MODE_BACK_BIT,
      // .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,

      .depthBiasEnable = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp = 0.0f,
      .depthBiasSlopeFactor = 0.0f,
  };

  VkPipelineMultisampleStateCreateInfo ms_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .sampleShadingEnable = VK_FALSE,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .minSampleShading = 1.0f,
      .pSampleMask = NULL,
      .alphaToCoverageEnable = VK_FALSE,
      .alphaToOneEnable = VK_FALSE,
  };

  VkPipelineColorBlendAttachmentState cb_attach = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE,

      .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
      .colorBlendOp = VK_BLEND_OP_ADD,

      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp = VK_BLEND_OP_ADD,
  };

  VkPipelineColorBlendStateCreateInfo cb_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = VK_FALSE,
      .logicOp = VK_LOGIC_OP_COPY,
      .attachmentCount = 1,
      .pAttachments = &cb_attach,
      .blendConstants = {0.0f},
  };

  VkGraphicsPipelineCreateInfo p_info = {0};
  VkPipeline new_pipeline;

  {
    p_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    p_info.stageCount = infos_created;
    p_info.pStages = stage_infos;

    p_info.pVertexInputState = &v_info;
    p_info.pInputAssemblyState = &a_info;
    p_info.pViewportState = &vs_info;
    p_info.pRasterizationState = &rs_info;
    p_info.pMultisampleState = &ms_info;
    p_info.pDepthStencilState = NULL;
    p_info.pColorBlendState = &cb_info;
    p_info.pDynamicState = &d_info;

    p_info.layout = p_layout->handle;

    p_info.renderPass = *render_pass;
    p_info.subpass = 0;

    p_info.basePipelineHandle = VK_NULL_HANDLE;
    p_info.basePipelineIndex = -1;
  }

  Fpx3d_E_Result retval = FPX3D_SUCCESS;

  Fpx3d_Vk_Pipeline *p = &lgpu->pipelines[index];

  if (VK_SUCCESS != vkCreateGraphicsPipelines(lgpu->handle, VK_NULL_HANDLE, 1,
                                              &p_info, NULL, &new_pipeline)) {
    retval = FPX3D_VK_PIPELINE_CREATE_ERROR;
  } else {
    p->handle = new_pipeline;
    p->layout = *p_layout;
    p->type = GRAPHICS_PIPELINE;
    p->graphics.shapes = NULL;
    p->graphics.shapeCount = 0;
    p->graphics.renderPassReference = render_pass;
  }

  FREE_SAFE(bindings);
  FREE_SAFE(attributes);

  return retval;
}

Fpx3d_Vk_Pipeline *fpx3d_vk_get_pipeline_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                            size_t index) {
  NULL_CHECK(lgpu, NULL);
  NULL_CHECK(lgpu->pipelines, NULL);

  if (lgpu->pipelineCapacity <= index)
    return NULL;

  return &lgpu->pipelines[index];
}

Fpx3d_E_Result fpx3d_vk_destroy_pipeline_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                            size_t index,
                                            Fpx3d_Vk_Context *ctx) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->pipelines, FPX3D_VK_NULLPTR_ERROR);

  if (lgpu->pipelineCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  Fpx3d_Vk_Pipeline *p = &lgpu->pipelines[index];

  NULL_CHECK(p->handle, FPX3D_VK_PIPELINE_INVALID_ERROR);

  switch (p->type) {
  case GRAPHICS_PIPELINE:
    FREE_SAFE(p->graphics.shapes);
    p->graphics.shapeCount = 0;
    break;

  default:
    // hmmmmmmm what
    break;
  }

  {
    if (NULL != p->bindings.inFlightDescriptorSets)
      for (size_t i = 0; i < ctx->constants.maxFramesInFlight; ++i) {
        fpx3d_vk_destroy_descriptor_set(&p->bindings.inFlightDescriptorSets[i],
                                        lgpu);
      }

    FREE_SAFE(p->bindings.inFlightDescriptorSets);
    FREE_SAFE(p->bindings.rawData);
  }

  vkDestroyPipeline(lgpu->handle, p->handle, NULL);

  memset(p, 0, sizeof(*p));

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_allocate_commandpools(Fpx3d_Vk_LogicalGpu *lgpu,
                                              size_t amount) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  return _realloc_array((void **)&lgpu->commandPools,
                        sizeof(lgpu->commandPools[0]), amount,
                        &lgpu->commandPoolCapacity);
}

Fpx3d_E_Result fpx3d_create_commandpool_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                           size_t index,
                                           Fpx3d_Vk_E_CommandPoolType type) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->commandPools, FPX3D_VK_NULLPTR_ERROR);

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
  LGPU_CHECK(lgpu, NULL);

  NULL_CHECK(lgpu->commandPools, NULL);

  if (lgpu->commandPoolCapacity <= index)
    return NULL;

  return &lgpu->commandPools[index];
}

Fpx3d_E_Result fpx3d_vk_destroy_commandpool_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                               size_t index) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->commandPools, FPX3D_VK_NULLPTR_ERROR);

  if (lgpu->commandPoolCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  _destroy_command_pool(lgpu, &lgpu->commandPools[index]);

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result
fpx3d_vk_allocate_commandbuffers_at_pool(Fpx3d_Vk_LogicalGpu *lgpu,
                                         size_t cmd_pool_index, size_t amount) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->commandPools, FPX3D_VK_NULLPTR_ERROR);

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
  NULL_CHECK(pipeline->graphics.renderPassReference, FPX3D_VK_NULLPTR_ERROR);
  NULL_CHECK(swapchain->swapchain, FPX3D_VK_SWAPCHAIN_INVALID_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE == *buffer)
    return FPX3D_VK_BAD_BUFFER_HANDLE_ERROR;

  if (VK_NULL_HANDLE == *(pipeline->graphics.renderPassReference))
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
  r_info.renderPass = *(pipeline->graphics.renderPassReference);
  r_info.framebuffer = swapchain->frames[frame_index].framebuffer;

  r_info.renderArea.extent = swapchain->swapchainExtent;
  r_info.renderArea.offset.x = 0;
  r_info.renderArea.offset.y = 0;

  VkClearValue clear = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  r_info.clearValueCount = 1;
  r_info.pClearValues = &clear;

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

  VkDescriptorSet bind_sets[HIGHEST_DESCRIPTOR_SET_IDX + 1] = {0};
  bind_sets[PIPELINE_DESCRIPTOR_SET_IDX] =
      pipeline->bindings.inFlightDescriptorSets[lgpu->frameCounter].handle;

  vkCmdBindPipeline(*buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);

  vkCmdBindDescriptorSets(*buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline->layout.handle, PIPELINE_DESCRIPTOR_SET_IDX,
                          1, bind_sets, 0, NULL);

  Fpx3d_Vk_DescriptorSet *pipeline_ds =
      &pipeline->bindings.inFlightDescriptorSets[lgpu->frameCounter];
  memcpy(pipeline_ds->buffer.mapped_memory, pipeline->bindings.rawData,
         pipeline_ds->buffer.objectCount * pipeline_ds->buffer.stride);

  for (size_t i = 0; i < pipeline->graphics.shapeCount; ++i) {
    // TODO: fix hardcoded stuff like instanceCount, firstVertex and other args

    Fpx3d_Vk_Shape *shape = pipeline->graphics.shapes[i];

    if (false == shape->isValid)
      continue;

    if (NULL != shape->bindings.inFlightDescriptorSets &&
        NULL != shape->bindings.rawData) {
      Fpx3d_Vk_DescriptorSet *shape_ds =
          &shape->bindings.inFlightDescriptorSets[lgpu->frameCounter];

      bind_sets[OBJECT_DESCRIPTOR_SET_IDX] = shape_ds->handle;

      vkCmdBindDescriptorSets(
          *buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout.handle,
          PIPELINE_DESCRIPTOR_SET_IDX, 2, bind_sets, 0, NULL);

      memcpy(shape_ds->buffer.mapped_memory, shape->bindings.rawData,
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
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

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

Fpx3d_E_Result
fpx3d_vk_draw_frame(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu,
                    Fpx3d_Vk_Pipeline *pipelines, size_t pipeline_count,
                    VkQueue *graphics_queue, VkQueue *present_queue) {
  NULL_CHECK(ctx, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(pipelines, FPX3D_ARGS_ERROR);
  NULL_CHECK(graphics_queue, FPX3D_ARGS_ERROR);
  NULL_CHECK(present_queue, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(ctx->windowContext, FPX3D_WND_BAD_WINDOW_HANDLE_ERROR);

  uint32_t image_index = UINT32_MAX;

  vkWaitForFences(lgpu->handle, 1, &lgpu->inFlightFences[lgpu->frameCounter],
                  VK_TRUE, UINT64_MAX);

  {
    VkResult success = vkAcquireNextImageKHR(
        lgpu->handle, lgpu->currentSwapchain.swapchain, UINT64_MAX,
        lgpu->currentSwapchain.acquireSemaphore, VK_NULL_HANDLE, &image_index);

    // TODO: find another way for refreshing the window. AMD doesn't like to
    // throw ERROR_OUT_OF_DATE, but nVidia does it all the time. so rely on the
    // windowing system's resize event instead
    if (VK_ERROR_OUT_OF_DATE_KHR == success) {
      return fpx3d_vk_refresh_current_swapchain(ctx, lgpu);
    } else if (VK_SUCCESS != success && VK_SUBOPTIMAL_KHR != success) {
      FPX3D_WARN("Could not retrieve next image in swap chain");
      return FPX3D_VK_ERROR;
    }
  }

  vkResetFences(lgpu->handle, 1, &lgpu->inFlightFences[lgpu->frameCounter]);

  if (UINT32_MAX == image_index) {
    // uhhhh
    FPX3D_WARN("Failed to retrieve swapchain image index");
    return FPX3D_VK_ERROR;
  }

  for (size_t i = 0; i < pipeline_count; ++i) {

    vkResetCommandBuffer(lgpu->inFlightCommandPool.buffers[lgpu->frameCounter],
                         0);

    {
      Fpx3d_E_Result success = fpx3d_vk_record_drawing_commandbuffer(
          &lgpu->inFlightCommandPool.buffers[lgpu->frameCounter], &pipelines[i],
          &lgpu->currentSwapchain, image_index, lgpu);

      if (FPX3D_SUCCESS != success)
        return success;
    }

    if (FPX3D_SUCCESS !=
        fpx3d_vk_submit_commandbuffer(
            &lgpu->inFlightCommandPool.buffers[lgpu->frameCounter], ctx, lgpu,
            image_index, graphics_queue))
      return FPX3D_VK_ERROR;
  }

  {
    VkSemaphore temp =
        lgpu->currentSwapchain.frames[image_index].writeAvailable;

    lgpu->currentSwapchain.frames[image_index].writeAvailable =
        lgpu->currentSwapchain.acquireSemaphore;

    lgpu->currentSwapchain.acquireSemaphore = temp;
  }

  {
    Fpx3d_E_Result success = fpx3d_vk_present_swapchain_frame_at(
        &lgpu->currentSwapchain, image_index, present_queue);

    if (FPX3D_VK_FRAME_OUT_OF_DATE_ERROR == success ||
        FPX3D_VK_FRAME_SUBOPTIMAL_ERROR == success) {
      fpx3d_vk_refresh_current_swapchain(ctx, lgpu);

      ctx->windowContext->resized = false;

      return FPX3D_SUCCESS;
    } else if (FPX3D_SUCCESS != success) {
      FPX3D_WARN("Could not present image");
      return FPX3D_VK_ERROR;
    }
  }

  return FPX3D_SUCCESS;
}

//
//   END OF LIBRARY IMPLEMENTATION
//

// -----------------------------------------------------

//
//  START OF STATIC FUNCTION DEFINITIONS
//

static size_t _align_up(size_t number, size_t alignment) {
  size_t new_number = number + (alignment - (number % alignment));
  if (number % alignment == 0)
    new_number -= alignment;

  return new_number;
}

static VkCommandBuffer _begin_temp_command_buffer(VkCommandPool graphics_pool,
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

static Fpx3d_E_Result _end_temp_command_buffer(VkCommandBuffer buf,
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

static Fpx3d_E_Result _new_memory(VkPhysicalDevice dev,
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

static Fpx3d_E_Result
_new_buffer(VkPhysicalDevice dev, Fpx3d_Vk_LogicalGpu *lgpu, VkDeviceSize size,
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
      _new_memory(dev, lgpu, mem_flags, mem_reqs, &new_mem);
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

static Fpx3d_E_Result _data_to_buffer(Fpx3d_Vk_LogicalGpu *lgpu,
                                      Fpx3d_Vk_Buffer *buf, void *data,
                                      VkDeviceSize size) {
  void *mapped = NULL;
  if (VK_SUCCESS != vkMapMemory(lgpu->handle, buf->memory, 0, size, 0, &mapped))
    return FPX3D_VK_ERROR;

  memcpy(mapped, data, size);

  vkUnmapMemory(lgpu->handle, buf->memory);

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _vk_bufcopy(VkDevice lgpu, VkQueue transfer_queue,
                                  Fpx3d_Vk_Buffer *src, Fpx3d_Vk_Buffer *dst,
                                  VkDeviceSize size,
                                  VkCommandPool transfer_cmd_pool) {
  VkCommandBuffer cbuffer = _begin_temp_command_buffer(transfer_cmd_pool, lgpu);

  VkBufferCopy region = {0};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = size;

  vkCmdCopyBuffer(cbuffer, src->buffer, dst->buffer, 1, &region);

  _end_temp_command_buffer(cbuffer, transfer_cmd_pool, transfer_queue, lgpu);

  return FPX3D_SUCCESS;
}

static Fpx3d_Vk_Buffer _new_buffer_with_data(VkPhysicalDevice dev,
                                             Fpx3d_Vk_LogicalGpu *lgpu,
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
    SELECT_POOL_OF_TYPE(GRAPHICS_POOL, lgpu, graphics_pool);
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
    _new_buffer(dev, lgpu, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                VK_SHARING_MODE_CONCURRENT, &s_buf);

    if (false == s_buf.isValid)
      use_staging = false;
    else {
      u_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      s_mode = VK_SHARING_MODE_CONCURRENT;
      m_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

      _data_to_buffer(lgpu, &s_buf, data, size);
    }
  }

  _new_buffer(dev, lgpu, size, u_flags, m_flags, s_mode, &return_buf);

  if (use_staging) {
    _vk_bufcopy(lgpu->handle, *graphics_queue, &s_buf, &return_buf, size,
                *graphics_pool);
    _destroy_buffer_object(lgpu, &s_buf);
  } else {
    _data_to_buffer(lgpu, &return_buf, data, size);
  }

  return return_buf;
}

static void _destroy_buffer_object(Fpx3d_Vk_LogicalGpu *lgpu,
                                   Fpx3d_Vk_Buffer *buffer) {
  if (VK_NULL_HANDLE != buffer->buffer)
    vkDestroyBuffer(lgpu->handle, buffer->buffer, NULL);

  if (NULL != buffer->mapped_memory)
    vkUnmapMemory(lgpu->handle, buffer->memory);

  if (VK_NULL_HANDLE != buffer->memory)
    vkFreeMemory(lgpu->handle, buffer->memory, NULL);

  memset(buffer, 0, sizeof(*buffer));
}

static Fpx3d_Vk_Buffer _new_vertex_buffer(VkPhysicalDevice dev,
                                          Fpx3d_Vk_LogicalGpu *lgpu,
                                          Fpx3d_Vk_VertexBundle *v_bundle) {
  Fpx3d_Vk_Buffer new_buf =
      _new_buffer_with_data(dev, lgpu, v_bundle->vertices,
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
  Fpx3d_Vk_Buffer new_buf =
      _new_buffer_with_data(dev, lgpu, v_bundle->indices,
                            v_bundle->indexCount * sizeof(v_bundle->indices[0]),
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  if (false == new_buf.isValid)
    return new_buf;

  new_buf.objectCount = v_bundle->indexCount;
  new_buf.stride = sizeof(v_bundle->indices[0]);

  return new_buf;
}

static Fpx3d_E_Result
_transition_image_layout(VkImage img, VkFormat fmt, VkImageLayout *old,
                         VkImageLayout new, VkImageSubresourceRange s_range,
                         VkCommandPool graphics_pool, VkQueue graphics_queue,
                         VkDevice lgpu) {
  UNUSED(fmt);

  VkCommandBuffer cbuf = _begin_temp_command_buffer(graphics_pool, lgpu);

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

  default:
    FPX3D_ERROR("Image layout transition to %u not implemented", new);
    return FPX3D_GENERIC_ERROR;
    break;
  }

  // https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-access-types-supported
  vkCmdPipelineBarrier(cbuf, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1,
                       &barrier);

  Fpx3d_E_Result success =
      _end_temp_command_buffer(cbuf, graphics_pool, graphics_queue, lgpu);
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
  VkCommandBuffer cbuf = _begin_temp_command_buffer(graphics_pool, lgpu);

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

  return _end_temp_command_buffer(cbuf, graphics_pool, graphics_queue, lgpu);
}

static struct fpx3d_vulkan_pool_queue_pair
_graphics_pool_and_queue(Fpx3d_Vk_LogicalGpu *lgpu) {
  struct fpx3d_vulkan_pool_queue_pair pair = {0};

  NULL_CHECK(lgpu, pair);
  LGPU_CHECK(lgpu, pair);

  VkQueue *graphics_queue = VK_NULL_HANDLE;
  VkCommandPool *graphics_pool = VK_NULL_HANDLE;
  if (NULL != lgpu->commandPools) {
    SELECT_POOL_OF_TYPE(GRAPHICS_POOL, lgpu, graphics_pool);
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

static Fpx3d_E_Result _new_image(VkPhysicalDevice dev,
                                 Fpx3d_Vk_LogicalGpu *lgpu,
                                 Fpx3d_Vk_ImageDimensions dimensions,
                                 VkFormat fmt, VkImageTiling tiling,
                                 VkImageUsageFlags usage,
                                 Fpx3d_Vk_Image *output) {
  NULL_CHECK(output, FPX3D_ARGS_ERROR);
  NULL_CHECK(dev, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

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

  m_flags &= ~(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  m_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

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

  VkImageSubresourceRange s_range = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .baseArrayLayer = 0,
                                     .baseMipLevel = 0,
                                     .layerCount = 1,
                                     .levelCount = 1};

  VkMemoryRequirements mem_reqs = {0};
  vkGetImageMemoryRequirements(lgpu->handle, new_img, &mem_reqs);

  FPX3D_ONFAIL(_new_memory(dev, lgpu, m_flags, mem_reqs, &new_mem), mem_success,
               vkDestroyImage(lgpu->handle, new_img, NULL);
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
      _new_buffer(dev, lgpu, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  VK_SHARING_MODE_CONCURRENT, &staging_buf),
      success,
      FPX3D_ERROR("Failed to create staging buffer for image transfer");
      return success;);

  FPX3D_ONFAIL(_data_to_buffer(lgpu, &staging_buf, data, size), success,
               FPX3D_ERROR("Failed to fill staging buffer with image data");
               return success;);

  FPX3D_ONFAIL(_vk_buf_to_image(staging_buf.buffer, image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                image->subresourceRange, *pair.pool,
                                *pair.queue, lgpu->handle),
               success, FPX3D_ERROR("Failed to copy staging memory into image");
               return success;);

  _destroy_buffer_object(lgpu, &staging_buf);

  return FPX3D_SUCCESS;
}

// can return NULL
static VkShaderModule *_select_module_stage(Fpx3d_Vk_ShaderModuleSet *set,
                                            Fpx3d_Vk_E_ShaderStage stage) {
  VkShaderModule *module = NULL;

  switch (stage) {
  case SHADER_STAGE_VERTEX:
    module = &set->vertex.handle;
    break;
  case SHADER_STAGE_TESSELATION_CONTROL:
    module = &set->tesselationControl.handle;
    break;
  case SHADER_STAGE_TESSELATION_EVALUATION:
    module = &set->tesselationEvaluation.handle;
    break;
  case SHADER_STAGE_GEOMETRY:
    module = &set->geometry.handle;
    break;
  case SHADER_STAGE_FRAGMENT:
    module = &set->fragment.handle;
    break;

  default:
    // error
    break;
  }

  return module;
}

static VkShaderModule _new_shader_module(Fpx3d_Vk_LogicalGpu *lgpu,
                                         Fpx3d_Vk_SpirvFile *spirv) {
  VkShaderModule new_module = VK_NULL_HANDLE;

  VkShaderModuleCreateInfo m_info = {0};
  m_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  m_info.codeSize = spirv->filesize;
  m_info.pCode = (uint32_t *)spirv->buffer;

  if (VK_SUCCESS !=
      vkCreateShaderModule(lgpu->handle, &m_info, NULL, &new_module))
    return VK_NULL_HANDLE;

  return new_module;
}

static void _glfw_resize_callback(GLFWwindow *window, int width, int height) {
  Fpx3d_Wnd_Context *w_ctx =
      (Fpx3d_Wnd_Context *)glfwGetWindowUserPointer(window);

  if (NULL == w_ctx) {
    FPX3D_WARN("GLFW resize callback called, but could not retrieve "
               "matching window_context");
    return;
  }

  w_ctx->windowDimensions[0] = (uint16_t)width;
  w_ctx->windowDimensions[1] = (uint16_t)height;

  w_ctx->resized = true;
}

static void _destroy_lgpu(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(ctx, );
  NULL_CHECK(ctx, );
  LGPU_CHECK(lgpu, );

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

  _destroy_command_pool(lgpu, &lgpu->inFlightCommandPool);
  FREE_SAFE(lgpu->inFlightCommandPool.buffers);

  FPX3D_DEBUG(" - command pools destroyed");

  fpx3d_vk_destroy_current_swapchain(lgpu);

  for (Fpx3d_Vk_Swapchain *ptr = lgpu->oldSwapchainsList; NULL != ptr;) {
    Fpx3d_Vk_Swapchain *temp = ptr->nextInList;
    _destroy_swapchain(lgpu, ptr, true);
    ptr = temp;
  }

  FPX3D_DEBUG(" - swapchains destroyed");

  if (NULL != lgpu->renderPasses)
    for (size_t i = 0; i < lgpu->renderPassCapacity; ++i) {
      if (VK_NULL_HANDLE != lgpu->renderPasses[i])
        fpx3d_vk_destroy_renderpass_at(lgpu, i);
    }

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

static void _destroy_command_pool(Fpx3d_Vk_LogicalGpu *lgpu,
                                  Fpx3d_Vk_CommandPool *pool) {
  if (VK_NULL_HANDLE == pool->pool)
    return;

  vkDestroyCommandPool(lgpu->handle, pool->pool, NULL);

  FREE_SAFE(pool->buffers);

  memset(pool, 0, sizeof(*pool));

  return;
}

static Fpx3d_E_Result _realloc_array(void **arr, size_t obj_size, size_t amount,
                                     size_t *old_capacity) {
  if (1 > amount)
    return FPX3D_ARGS_ERROR;

  void *data = realloc(*arr, obj_size * amount);
  if (NULL == data) {
    perror("realloc()");
    return FPX3D_MEMORY_ERROR;
  }

  if (amount > *old_capacity) {
    memset((uint8_t *)data + *old_capacity, 0,
           (amount - *old_capacity) * obj_size);
  }

  *arr = data;
  *old_capacity = amount;

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

static struct fpx3d_vulkan_queues *
_get_queues_ptr_by_type(Fpx3d_Vk_LogicalGpu *lgpu, Fpx3d_Vk_E_QueueType type) {
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

static Fpx3d_E_Result _destroy_swapchain(Fpx3d_Vk_LogicalGpu *lgpu,
                                         Fpx3d_Vk_Swapchain *sc, bool force) {
  NULL_CHECK(sc, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  if (false == force)
    for (size_t i = 0; i < sc->frameCount; ++i) {
      // check states of fences on the frames:
      // if all signaled, all good.
      // Otherwise if force == false, return FPX3D_RESOURCE_BUSY_ERROR;
      if (VK_SUCCESS != vkGetFenceStatus(lgpu->handle, sc->frames[i].idleFence))
        return FPX3D_RESOURCE_BUSY_ERROR;
    }

  for (size_t i = 0; i < sc->frameCount; ++i) {
    if (VK_NULL_HANDLE != sc->frames[i].idleFence)
      vkDestroyFence(lgpu->handle, sc->frames[i].idleFence, NULL);
  }

  for (size_t i = 0; i < sc->frameCount; ++i) {
    if (VK_NULL_HANDLE != sc->frames[i].framebuffer)
      vkDestroyFramebuffer(lgpu->handle, sc->frames[i].framebuffer, NULL);

    if (VK_NULL_HANDLE != sc->frames[i].view)
      vkDestroyImageView(lgpu->handle, sc->frames[i].view, NULL);

    if (VK_NULL_HANDLE != sc->frames[i].writeAvailable)
      vkDestroySemaphore(lgpu->handle, sc->frames[i].writeAvailable, NULL);

    if (VK_NULL_HANDLE != sc->frames[i].renderFinished)
      vkDestroySemaphore(lgpu->handle, sc->frames[i].renderFinished, NULL);
  }
  FREE_SAFE(sc->frames);

  vkDestroySemaphore(lgpu->handle, sc->acquireSemaphore, NULL);

  if (VK_NULL_HANDLE != sc->swapchain)
    vkDestroySwapchainKHR(lgpu->handle, sc->swapchain, NULL);

  memset(sc, 0, sizeof(*sc));

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _retire_current_swapchain(Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);

  Fpx3d_Vk_Swapchain **the_pointer = NULL;

  if (NULL == lgpu->oldSwapchainsList) {
    the_pointer = &lgpu->oldSwapchainsList;
  } else if (NULL == lgpu->newestOldSwapchain) {
    for (the_pointer = &lgpu->oldSwapchainsList[0].nextInList;
         NULL != *the_pointer; the_pointer = &(*the_pointer)->nextInList)
      ;
  } else {
    the_pointer = &lgpu->newestOldSwapchain->nextInList;
  }

  *the_pointer = (Fpx3d_Vk_Swapchain *)calloc(1, sizeof(Fpx3d_Vk_Swapchain));

  if (NULL == *the_pointer) {
    perror("calloc()");
    return FPX3D_MEMORY_ERROR;
  }

  memcpy(*the_pointer, &lgpu->currentSwapchain, sizeof(Fpx3d_Vk_Swapchain));
  memset(&lgpu->currentSwapchain, 0, sizeof(lgpu->currentSwapchain));

  lgpu->newestOldSwapchain = *the_pointer;
  (*the_pointer)->nextInList = NULL;

  return FPX3D_SUCCESS;
}

static VkExtent2D _new_swapchain_extent(Fpx3d_Wnd_Context *wnd,
                                        VkSurfaceCapabilitiesKHR cap) {
  VkExtent2D retval = {0};
  NULL_CHECK(wnd, retval);

  if (cap.currentExtent.width != UINT32_MAX)
    return cap.currentExtent;

  int width, height;

  glfwGetFramebufferSize(wnd->glfwWindow, &width, &height);

  retval.height = CLAMP((uint32_t)height, cap.minImageExtent.height,
                        cap.maxImageExtent.height);
  retval.width = CLAMP((uint32_t)width, cap.minImageExtent.width,
                       cap.maxImageExtent.width);

  return retval;
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

static bool _surface_format_picker(VkPhysicalDevice dev, VkSurfaceKHR sfc,
                                   VkSurfaceFormatKHR *fmts, size_t fmt_count,
                                   VkSurfaceFormatKHR *output) {
  uint32_t formats_available = 0;
  VkSurfaceFormatKHR *formats = NULL;

  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, sfc, &formats_available, NULL);

  if (0 == formats_available)
    return false;

  formats = (VkSurfaceFormatKHR *)calloc(formats_available,
                                         sizeof(VkSurfaceFormatKHR));

  if (NULL == formats) {
    perror("calloc()");
    return false;
  }

  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, sfc, &formats_available, formats);

  if (0 == fmt_count) {
    *output = formats[0];
    FREE_SAFE(formats);
    return true;
  }

  for (size_t i = 0; i < fmt_count; ++i) {
    for (uint32_t j = 0; j < formats_available; ++j) {
      if (fmts[i].format == formats[j].format &&
          fmts[i].colorSpace == formats[j].colorSpace) {
        FREE_SAFE(formats);
        *output = fmts[i];
        return true;
      }
    }
  }

  FREE_SAFE(formats);
  return false;
}

static bool _present_mode_picker(VkPhysicalDevice dev, VkSurfaceKHR sfc,
                                 VkPresentModeKHR *mds, size_t md_count,
                                 VkPresentModeKHR *output) {
  uint32_t modes_available = 0;
  VkPresentModeKHR *modes = NULL;

  vkGetPhysicalDeviceSurfacePresentModesKHR(dev, sfc, &modes_available, NULL);

  if (0 == modes_available)
    return false;

  modes = (VkPresentModeKHR *)calloc(modes_available, sizeof(VkPresentModeKHR));

  if (NULL == modes) {
    perror("calloc()");
    return false;
  }

  vkGetPhysicalDeviceSurfacePresentModesKHR(dev, sfc, &modes_available, modes);

  if (0 == md_count) {
    *output = modes[0];
    FREE_SAFE(modes);
    return true;
  }

  for (size_t i = 0; i < md_count; ++i) {
    for (uint32_t j = 0; j < modes_available; ++j) {
      if (mds[i] == modes[j]) {
        FREE_SAFE(modes);
        *output = mds[i];
        return true;
      }
    }
  }

  FREE_SAFE(modes);
  return false;
}

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

static Fpx3d_E_Result _create_queues(Fpx3d_Vk_LogicalGpu *lgpu,
                                     Fpx3d_Vk_E_QueueType q_type,
                                     size_t count) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  struct fpx3d_vulkan_queues *queues = _get_queues_ptr_by_type(lgpu, q_type);

  NULL_CHECK(queues, FPX3D_VK_QUEUE_RETRIEVE_ERROR);

  if (count > queues->count)
    return FPX3D_NO_CAPACITY_ERROR;

  for (size_t i = 0; i < queues->count; ++i) {
    vkGetDeviceQueue(lgpu->handle, queues->queueFamilyIndex,
                     queues->offsetInFamily + i, &queues->queues[i]);
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result _create_all_available_queues(Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

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

static Fpx3d_E_Result _bind_descriptors_to_buffer(Fpx3d_Vk_DescriptorSet *set,
                                                  Fpx3d_Vk_Context *ctx,
                                                  Fpx3d_Vk_LogicalGpu *lgpu) {
  NULL_CHECK(set, FPX3D_ARGS_ERROR);

  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  LGPU_CHECK(lgpu, FPX3D_VK_LGPU_INVALID_ERROR);

  VkWriteDescriptorSet *w_sets = NULL;
  w_sets = (VkWriteDescriptorSet *)calloc(set->bindingCount,
                                          sizeof(VkWriteDescriptorSet));
  if (NULL == w_sets) {
    perror("calloc()");
    return FPX3D_MEMORY_ERROR;
  }

  VkDescriptorBufferInfo **b_infos = NULL;
  b_infos = (VkDescriptorBufferInfo **)calloc(set->bindingCount,
                                              sizeof(VkDescriptorBufferInfo *));
  if (NULL == b_infos) {
    perror("calloc()");
    FREE_SAFE(w_sets);
    return FPX3D_MEMORY_ERROR;
  }

  size_t offset_in_buffer = 0;
  for (size_t j = 0; j < set->bindingCount; ++j) {
    b_infos[j] = (VkDescriptorBufferInfo *)calloc(
        set->bindings[j].bindingProperties.elementCount,
        sizeof(VkDescriptorBufferInfo));

    if (NULL == b_infos[j]) {
      perror("calloc()");

      for (size_t k = 0; k < j; ++k)
        FREE_SAFE(b_infos[k]);

      FREE_SAFE(b_infos);
      FREE_SAFE(w_sets);

      return FPX3D_MEMORY_ERROR;
    }

    for (size_t k = 0; k < set->bindings[j].bindingProperties.elementCount;
         ++k) {
      b_infos[j][k].buffer = set->buffer.buffer;
      b_infos[j][k].offset = offset_in_buffer;
      b_infos[j][k].range = set->bindings[j].bindingProperties.elementSize;

      offset_in_buffer +=
          _align_up(set->bindings[j].bindingProperties.elementSize,
                    ctx->constants.bufferAlignment);
    }

    VkWriteDescriptorSet *write_set = &w_sets[j];
    write_set->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_set->dstSet = set->handle;
    write_set->dstBinding = j;
    write_set->dstArrayElement = 0;
    write_set->descriptorCount =
        set->bindings[j].bindingProperties.elementCount;
    write_set->descriptorType =
        (VkDescriptorType)set->bindings[j].bindingProperties.type;

    write_set->pBufferInfo = b_infos[j];
    write_set->pImageInfo = NULL;
    write_set->pTexelBufferView = NULL;
  }

  vkUpdateDescriptorSets(lgpu->handle, set->bindingCount, w_sets, 0, NULL);

  for (size_t i = 0; i < set->bindingCount; ++i) {
    FREE_SAFE(b_infos[i]);
  }
  FREE_SAFE(b_infos);
  FREE_SAFE(w_sets);

  return FPX3D_SUCCESS;
}

//
//  END OF STATIC FUNCTION DEFINITIONS
//

// if you're reading this you're cool! :)
