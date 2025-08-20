/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include "debug.h"
#include "fpx3d.h"
#include "vk/logical_gpu.h"
#include "volk/volk.h"

#include "vk/renderpass.h"

extern Fpx3d_E_Result __fpx3d_realloc_array(void **array_ptr, size_t obj_size,
                                            size_t amount,
                                            size_t *old_capacity);

Fpx3d_E_Result fpx3d_vk_allocate_renderpasses(Fpx3d_Vk_LogicalGpu *lgpu,
                                              size_t count) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);

  return __fpx3d_realloc_array((void **)&lgpu->renderPasses,
                               sizeof(lgpu->renderPasses[0]), count,
                               &lgpu->renderPassCapacity);
}

Fpx3d_E_Result fpx3d_vk_create_renderpass_at(Fpx3d_Vk_LogicalGpu *lgpu,
                                             size_t index,
                                             Fpx3d_Vk_Context *ctx) {
  NULL_CHECK(lgpu, FPX3D_ARGS_ERROR);
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->renderPasses, FPX3D_VK_NULLPTR_ERROR);

  UNUSED(ctx);

  if (lgpu->renderPassCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  if (VK_FORMAT_UNDEFINED ==
      lgpu->currentSwapchain.properties.surfaceFormat.format) {
    return FPX3D_VK_ERROR;
  }

  // TODO: make modular. currently hardcoded to follow tutorial
  // maybe have the programmer pass an array of color attachments,
  // or maybe subpasses

  FPX3D_TODO("Make fpx3d_vk_create_renderpass_at() modular");

  VkRenderPass pass = VK_NULL_HANDLE;

  VkAttachmentDescription color_attachment = {0};
  color_attachment.format =
      lgpu->currentSwapchain.properties.surfaceFormat.format;
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

  if (VK_NULL_HANDLE != lgpu->renderPasses[index].handle)
    vkDestroyRenderPass(lgpu->handle, lgpu->renderPasses[index].handle, NULL);

  lgpu->renderPasses[index].handle = pass;

  return FPX3D_SUCCESS;
}

Fpx3d_Vk_RenderPass *fpx3d_vk_get_renderpass_at(Fpx3d_Vk_LogicalGpu *lgpu,
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
  NULL_CHECK(lgpu->handle, FPX3D_VK_LGPU_INVALID_ERROR);

  NULL_CHECK(lgpu->renderPasses, FPX3D_VK_NULLPTR_ERROR);

  if (lgpu->renderPassCapacity <= index)
    return FPX3D_INDEX_OUT_OF_RANGE_ERROR;

  if (VK_NULL_HANDLE != lgpu->renderPasses[index].handle)
    vkDestroyRenderPass(lgpu->handle, lgpu->renderPasses[index].handle, NULL);

  lgpu->renderPasses[index].handle = VK_NULL_HANDLE;

  return FPX3D_SUCCESS;
}
