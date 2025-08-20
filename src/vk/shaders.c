/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "vk/logical_gpu.h"
#include "volk/volk.h"

#include "vk/shaders.h"

// static declarations --------------------------------------------
static VkShaderModule _new_shader_module(Fpx3d_Vk_LogicalGpu *lgpu,
                                         Fpx3d_Vk_SpirvFile *spirv);
static VkShaderModule *_select_module_stage(Fpx3d_Vk_ShaderModuleSet *set,
                                            Fpx3d_Vk_E_ShaderStage stage);
// end of static declarations -------------------------------------

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
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

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

// STATIC FUNCTIONS -----------------------------------------------
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
// END OF STATIC FUNCTIONS ----------------------------------------
