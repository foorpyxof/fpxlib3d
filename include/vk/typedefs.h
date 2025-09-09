/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_TYPEDEFS_H
#define FPX_VK_TYPEDEFS_H

#include <vulkan/vulkan_core.h>

typedef struct _fpx3d_vk_context Fpx3d_Vk_Context;

typedef struct _fpx3d_vk_lgpu Fpx3d_Vk_LogicalGpu;

typedef struct _fpx3d_vk_render_pass Fpx3d_Vk_RenderPass;

typedef struct _fpx3d_vk_image_dimensions Fpx3d_Vk_ImageDimensions;
typedef struct _fpx3d_vk_image Fpx3d_Vk_Image;
typedef struct _fpx3d_vk_image_sampler Fpx3d_Vk_ImageSampler;
typedef struct _fpx3d_vk_texture Fpx3d_Vk_Texture;

typedef struct _fpx3d_vk_sc Fpx3d_Vk_Swapchain;
typedef struct _fpx3d_vk_sc_frame Fpx3d_Vk_SwapchainFrame;
typedef struct _fpx3d_vk_sc_req Fpx3d_Vk_SwapchainRequirements;
typedef struct _fpx3d_vk_sc_prop Fpx3d_Vk_SwapchainProperties;

typedef enum {
  GRAPHICS_QUEUE = 0,
  PRESENT_QUEUE = 1,
  TRANSFER_QUEUE = 2, // transfer ONLY! no graphics
} Fpx3d_Vk_E_QueueType;
typedef struct _fpx3d_vk_qf Fpx3d_Vk_QueueFamily;
typedef struct _fpx3d_vk_qf_req Fpx3d_Vk_QueueFamilyRequirements;

typedef enum {
  SHADER_STAGE_INVALID = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM,
  SHADER_STAGE_VERTEX = VK_SHADER_STAGE_VERTEX_BIT,
  SHADER_STAGE_TESSELATION_CONTROL = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
  SHADER_STAGE_TESSELATION_EVALUATION =
      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
  SHADER_STAGE_GEOMETRY = VK_SHADER_STAGE_GEOMETRY_BIT,
  SHADER_STAGE_FRAGMENT = VK_SHADER_STAGE_FRAGMENT_BIT,
  SHADER_STAGE_ALL = VK_SHADER_STAGE_ALL,
} Fpx3d_Vk_E_ShaderStage;
typedef struct _fpx3d_vk_spirv Fpx3d_Vk_SpirvFile;
typedef struct _fpx3d_vk_shader_modules Fpx3d_Vk_ShaderModuleSet;

typedef struct _fpx3d_vk_buffer Fpx3d_Vk_Buffer;

typedef enum {
  DESC_INVALID = VK_DESCRIPTOR_TYPE_MAX_ENUM,
  DESC_UNIFORM = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
  DESC_IMAGE_SAMPLER = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
} Fpx3d_Vk_E_DescriptorType;
typedef enum {
  DESCRIPTOR_SET_INDEX_PIPELINE = 0,
  DESCRIPTOR_SET_INDEX_OBJECT = 1,
  DESCRIPTOR_SET_INDEX_MAX_VALUE
} Fpx3d_Vk_E_DescriptorSetIndex;
typedef struct _fpx3d_vk_descriptor_set_layout Fpx3d_Vk_DescriptorSetLayout;
typedef struct _fpx3d_vk_descriptor_set_binding Fpx3d_Vk_DescriptorSetBinding;
typedef struct _fpx3d_vk_descriptor_set Fpx3d_Vk_DescriptorSet;

typedef struct _fpx3d_vk_vertex_bundle Fpx3d_Vk_VertexBundle;
typedef struct _fpx3d_vk_vertex_binding Fpx3d_Vk_VertexBinding;
typedef struct _fpx3d_vk_vertex_attr Fpx3d_Vk_VertexAttribute;

typedef struct _fpx3d_vk_shapebuffer Fpx3d_Vk_ShapeBuffer;
typedef struct _fpx3d_vk_shape Fpx3d_Vk_Shape;

typedef enum {
  GRAPHICS_PIPELINE = 0,
  COMPUTE_PIPELINE = 1,
} Fpx3d_Vk_E_PipelineType;
typedef struct _fpx3d_vk_pipeline_layout Fpx3d_Vk_PipelineLayout;
typedef struct _fpx3d_vk_pipeline Fpx3d_Vk_Pipeline;

typedef enum {
  GRAPHICS_POOL = 0,
  TRANSFER_POOL = 1,
} Fpx3d_Vk_E_CommandPoolType;
typedef struct _fpx3d_vk_command_pool Fpx3d_Vk_CommandPool;

#endif // FPX_VK_TYPEDEFS_H
