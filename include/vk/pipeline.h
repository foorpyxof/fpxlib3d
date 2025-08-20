/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_PIPELINE_H
#define FPX_VK_PIPELINE_H

#include <stdbool.h>
#include <stdint.h>

#include "fpx3d.h"

#include "vk/typedefs.h"

struct _fpx3d_vk_pipeline_layout {
  VkPipelineLayout handle;

  Fpx3d_Vk_DescriptorSetLayout *descriptorSetLayouts;
  size_t descriptorSetLayoutCount;

  bool isValid;
};

struct _fpx3d_vk_pipeline {
  VkPipeline handle;
  Fpx3d_Vk_PipelineLayout layout;

  Fpx3d_Vk_E_PipelineType type;

  union {
    struct {
      Fpx3d_Vk_Shape **shapes;
      size_t shapeCount;

      Fpx3d_Vk_RenderPass *renderPassReference;
    } graphics;
  };

  struct {
    Fpx3d_Vk_DescriptorSet *inFlightDescriptorSets;
    void *rawBufferData;
  } bindings;
};

// DESCRIPTOR SET LAYOUT INDICES:
//  - index 0 for pipeline-level bindings (view- and projection-matrices)
//  - index 1 for shape-level bindings (model-matrix, etc.)
//  using higher descriptor sets is not currently supported
//
Fpx3d_Vk_PipelineLayout
fpx3d_vk_create_pipeline_layout(Fpx3d_Vk_DescriptorSetLayout *ds_layouts,
                                size_t ds_layout_count, Fpx3d_Vk_LogicalGpu *);
Fpx3d_E_Result fpx3d_vk_destroy_pipeline_layout(Fpx3d_Vk_PipelineLayout *,
                                                Fpx3d_Vk_LogicalGpu *);

Fpx3d_E_Result fpx3d_vk_allocate_pipelines(Fpx3d_Vk_LogicalGpu *,
                                           size_t amount);

Fpx3d_E_Result fpx3d_vk_create_graphics_pipeline_at(
    Fpx3d_Vk_LogicalGpu *, size_t index, Fpx3d_Vk_PipelineLayout *p_layout,
    Fpx3d_Vk_RenderPass *render_pass, Fpx3d_Vk_ShaderModuleSet *shaders,
    Fpx3d_Vk_VertexBinding *vertex_bindings, size_t vertex_bind_count);
Fpx3d_Vk_Pipeline *fpx3d_vk_get_pipeline_at(Fpx3d_Vk_LogicalGpu *,
                                            size_t index);
Fpx3d_E_Result fpx3d_vk_destroy_pipeline_at(Fpx3d_Vk_LogicalGpu *, size_t index,
                                            Fpx3d_Vk_Context *);

Fpx3d_E_Result fpx3d_vk_assign_shapes_to_pipeline(Fpx3d_Vk_Shape **shapes,
                                                  size_t count,
                                                  Fpx3d_Vk_Pipeline *);

#endif // FPX_VK_PIPELINE_H
