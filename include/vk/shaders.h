/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_SHADERS_H
#define FPX_VK_SHADERS_H

#include "../fpx3d.h"

#include "./typedefs.h"

struct _fpx3d_vk_spirv {
  uint8_t *buffer;
  size_t filesize;

  // const char *version;

  Fpx3d_Vk_E_ShaderStage stage;

  // enum {
  //   UNKNOWN = 0,
  //   ESSL = 1,
  //   GLSL = 2,
  //   OPENCL_C = 3,
  //   OPENCL_CPP = 4,
  //   HLSL = 5,
  //   CPP_FOR_OPENCL = 6,
  //   SYCL = 7,
  //   HERO_C = 8,
  //   NZSL = 9,
  //   WGSL = 10,
  //   SLANG = 11,
  //   ZIG = 12,
  //   RUST = 13
  // } source_language;
};

struct fpx3d_vulkan_shader_module {
  VkShaderModule handle;
};

struct _fpx3d_vk_shader_modules {
  struct fpx3d_vulkan_shader_module vertex;
  struct fpx3d_vulkan_shader_module tesselationControl;
  struct fpx3d_vulkan_shader_module tesselationEvaluation;
  struct fpx3d_vulkan_shader_module geometry;
  struct fpx3d_vulkan_shader_module fragment;
};

Fpx3d_Vk_SpirvFile fpx3d_vk_read_spirv_data(const uint8_t *spirv_bytes,
                                            size_t spirv_length,
                                            Fpx3d_Vk_E_ShaderStage stage);
Fpx3d_Vk_SpirvFile fpx3d_vk_read_spirv_file(const char *filename,
                                            Fpx3d_Vk_E_ShaderStage stage);
Fpx3d_E_Result fpx3d_vk_destroy_spirv_file(Fpx3d_Vk_SpirvFile *);

Fpx3d_E_Result fpx3d_vk_load_shadermodules(Fpx3d_Vk_SpirvFile *spirv_files,
                                           size_t spirv_count,
                                           Fpx3d_Vk_LogicalGpu *,
                                           Fpx3d_Vk_ShaderModuleSet *output);

Fpx3d_E_Result fpx3d_vk_destroy_shadermodules(Fpx3d_Vk_ShaderModuleSet *,
                                              Fpx3d_Vk_LogicalGpu *);

#endif // FPX_VK_SHADERS_H
