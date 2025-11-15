/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "vulkan/vulkan_core.h"

#include "macros.h"
#include "vk/context.h"
#include "vk/descriptors.h"
#include "vk/logical_gpu.h"
#include "vk/renderpass.h"
#include "vk/shaders.h"
#include "vk/typedefs.h"
#include "vk/vertex.h"
#include "volk/volk.h"

#include "vk/pipeline.h"

extern Fpx3d_E_Result __fpx3d_realloc_array(void **array_ptr, size_t obj_size,
                                            size_t amount,
                                            size_t *old_capacity);

Fpx3d_Vk_PipelineLayout
fpx3d_vk_create_pipeline_layout(Fpx3d_Vk_DescriptorSetLayout *ds_layouts,
                                size_t ds_layout_count,
                                Fpx3d_Vk_LogicalGpu *lgpu) {
  Fpx3d_Vk_PipelineLayout retval = {0};

  NULL_CHECK(lgpu, retval);
  NULL_CHECK(lgpu->handle, retval);

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

    Fpx3d_E_Result alloc_success = __fpx3d_realloc_array(
        (void **)&retval.descriptorSetLayouts,
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
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  if (VK_NULL_HANDLE != layout->handle && layout->isValid) {
    vkDestroyPipelineLayout(lgpu->handle, layout->handle, NULL);
  }

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_allocate_pipelines(Fpx3d_Vk_LogicalGpu *lgpu,
                                           size_t amount) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  return __fpx3d_realloc_array((void **)&lgpu->pipelines,
                               sizeof(lgpu->pipelines[0]), amount,
                               &lgpu->pipelineCapacity);
}

Fpx3d_E_Result fpx3d_vk_create_graphics_pipeline_at(
    Fpx3d_Vk_LogicalGpu *lgpu, size_t index, Fpx3d_Vk_PipelineLayout *p_layout,
    Fpx3d_Vk_RenderPass *render_pass, Fpx3d_Vk_ShaderModuleSet *shaders,
    Fpx3d_Vk_VertexBinding *vertex_bindings, size_t vertex_bind_count) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(shaders, FPX3D_ARGS_ERROR);
  NULL_CHECK(render_pass, FPX3D_ARGS_ERROR);
  NULL_CHECK(render_pass->handle, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->pipelines, FPX3D_NULLPTR_ERROR);

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

  VkPipelineDepthStencilStateCreateInfo ds_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .minDepthBounds = 0.0f,
      .maxDepthBounds = 0.0f,
      .stencilTestEnable = VK_FALSE};

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
    p_info.pDepthStencilState = &ds_info;
    p_info.pColorBlendState = &cb_info;
    p_info.pDynamicState = &d_info;

    p_info.layout = p_layout->handle;

    p_info.renderPass = render_pass->handle;
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
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->pipelines, FPX3D_NULLPTR_ERROR);

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
    FREE_SAFE(p->bindings.rawBufferData);
  }

  vkDestroyPipeline(lgpu->handle, p->handle, NULL);

  memset(p, 0, sizeof(*p));

  return FPX3D_SUCCESS;
}

Fpx3d_E_Result fpx3d_vk_assign_shapes_to_pipeline(Fpx3d_Vk_Shape **shapes,
                                                  size_t count,
                                                  Fpx3d_Vk_Pipeline *pipeline) {
  NULL_CHECK(pipeline, FPX3D_ARGS_ERROR);

  if (1 > count)
    return FPX3D_SUCCESS;

  NULL_CHECK(shapes, FPX3D_ARGS_ERROR);

  __fpx3d_realloc_array((void **)&pipeline->graphics.shapes, sizeof(*shapes),
                        count, &pipeline->graphics.shapeCount);

  if (NULL == pipeline->graphics.shapes) {
    perror("realloc()");
    return FPX3D_MEMORY_ERROR;
  }

  memcpy(pipeline->graphics.shapes, shapes, count * sizeof(*shapes));

  pipeline->graphics.shapeCount = count;

  return FPX3D_SUCCESS;
}
