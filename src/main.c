/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include "debug.h"
#include "fpx3d.h"
#include "vk.h"
#include "window.h"

#include <GLFW/glfw3.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>

#define LOGICAL_GPU_COUNT 1

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(*array))

// clang-format off
#define PRINT_FAILURE(res)                                                     \
  if (FPX3D_SUCCESS != res) { FPX3D_ERROR("A FUNCTION FAILED!"); }
// clang-format on

void dest_callback(void *);

Fpx3d_Vk_Context vk_ctx = {0};

const char *val_layers[] = {"VK_LAYER_KHRONOS_validation"};
const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

struct shape_buffer *shapes = NULL;

static void sig_catcher(int signo) {
  const char *type = NULL;
  switch (signo) {
  case SIGINT:
    type = "Interrupt ";
    break;
  case SIGKILL:
    type = "Kill ";
    break;

  case SIGABRT:
    type = "Abort ";
    break;

  default:
    return;
  }

  fprintf(stderr, "\n%ssignal received. Exiting\n", type);
  fpx3d_vk_destroy_window(&vk_ctx, dest_callback);

  exit(128 + signo);
}

VkSurfaceFormatKHR formats[] = {
    {.format = VK_FORMAT_B8G8R8A8_SRGB,
     .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};

VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR};

Fpx3d_Vk_SwapchainRequirements sc_reqs = {0};

int gpu_suitability(Fpx3d_Vk_Context *ctx, VkPhysicalDevice to_score) {
  uint8_t score = 200;
  float multiplier;

  VkPhysicalDeviceProperties dev_properties = {0};
  VkPhysicalDeviceFeatures dev_features = {0};

  vkGetPhysicalDeviceProperties(to_score, &dev_properties);
  vkGetPhysicalDeviceFeatures(to_score, &dev_features);

  switch (dev_properties.deviceType) {

  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    multiplier = 0.2f;
    break;

  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    multiplier = 2.0f;
    break;

  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    multiplier = 1.2f;
    break;

  default:
    multiplier = 1.0f;
    break;
  }

  // some must-have features
  // not having these is a dealbreaker
  if (!dev_features.geometryShader) {
    score = 0;
    goto gpu_suitability_ret;
  }

  {
    Fpx3d_Vk_SwapchainProperties props =
        fpx3d_vk_get_swapchain_support(ctx, to_score, sc_reqs);

    if (!(props.surfaceFormatValid && props.presentModeValid)) {
      score = 0;
      goto gpu_suitability_ret;
    }
  }

gpu_suitability_ret:
  return score * multiplier;
}

Fpx3d_Vk_ShaderModuleSet modules = {0};

Fpx3d_Vk_LogicalGpu *lgpu = NULL;

Fpx3d_Vk_ShapeBuffer triangle_shape = {0};
Fpx3d_Vk_ShapeBuffer square_shape = {0};

VkQueue *graphics_queue = NULL;
VkQueue *present_queue = NULL;
VkQueue *transfer_queue = NULL;

VkRenderPass *render_pass = NULL;

VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
Fpx3d_Vk_Pipeline *pipeline = NULL;

Fpx3d_Vk_CommandPool *transfer_pool = NULL;
VkCommandBuffer *transfer_buffer = NULL;

Fpx3d_Vk_SwapchainProperties swapchain_properties = {0};

void dest_callback(void *custom_ptr) {
  fprintf(stderr, "%s\n", (const char *)custom_ptr);

  if (VK_NULL_HANDLE == lgpu->handle)
    return;

  vkDeviceWaitIdle(lgpu->handle);

  PRINT_FAILURE(fpx3d_vk_free_shapebuffer(lgpu, &triangle_shape));
  PRINT_FAILURE(fpx3d_vk_free_shapebuffer(lgpu, &square_shape));
  PRINT_FAILURE(fpx3d_vk_destroy_shadermodules(&modules, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_pipeline_layout(lgpu, pipeline_layout));
}

int main(void) {

  signal(SIGINT, sig_catcher);
  signal(SIGABRT, sig_catcher);
  signal(SIGKILL, sig_catcher);

  // VULKAN SETUP

  VkPhysicalDeviceFeatures lgpu_features = {
      .geometryShader = VK_TRUE,
  };

  fpx3d_vk_set_required_surfaceformats(&sc_reqs, formats, ARRAY_SIZE(formats));
  fpx3d_vk_set_required_presentmodes(&sc_reqs, modes, ARRAY_SIZE(modes));

  Fpx3d_Wnd_Context wnd_ctx = {.glfwWindow = NULL,
                               .windowDimensions = {800, 800},
                               .windowTitle = "YAY",
                               .resized = false};

  {
    VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                  .apiVersion = VK_API_VERSION_1_0,
                                  .pApplicationName = "GG_ENGINE_TEST",
                                  .applicationVersion =
                                      VK_MAKE_VERSION(0, 1, 0),
                                  .pEngineName = "GOODGIRL",
                                  .engineVersion = VK_MAKE_VERSION(0, 2, 0),
                                  .pNext = NULL};

    vk_ctx.appInfo = app_info;

    vk_ctx.validationLayers = val_layers;
    vk_ctx.validationLayersCount = ARRAY_SIZE(val_layers);
    vk_ctx.lgpuExtensions = extensions;
    vk_ctx.lgpuExtensionCount = ARRAY_SIZE(extensions);

    PRINT_FAILURE(fpx3d_vk_set_custom_pointer(&vk_ctx, "🏳️‍⚧️"));
  }

  // --------------- SHADERS --------------

  Fpx3d_Vk_SpirvFile vertex_shader =
      fpx3d_vk_read_spirv_file("shaders/default.vert.spv", SHADER_STAGE_VERTEX);
  Fpx3d_Vk_SpirvFile fragment_shader = fpx3d_vk_read_spirv_file(
      "shaders/default.frag.spv", SHADER_STAGE_FRAGMENT);

  Fpx3d_Vk_SpirvFile shaders[] = {vertex_shader, fragment_shader};

  // ----------- END OF SHADERS -----------

  // --------------- VERTICES --------------

  Fpx3d_Vk_Vertex triangle[] = {
      {.position = {-0.15f, -0.65f}, .color = {1.0f, 0.0f, 0.0f}},
      {.position = {0.35f, 0.35f}, .color = {0.0f, 1.0f, 0.0f}},
      {.position = {-0.65f, 0.35f}, .color = {0.0f, 0.0f, 1.0f}}};

  Fpx3d_Vk_Vertex square[] = {
      {.position = {0.15f, 0.15f}, .color = {1.0f, 0.0f, 0.0f}},
      {.position = {0.65f, 0.15f}, .color = {0.0f, 1.0f, 0.0f}},
      {.position = {0.65f, 0.65f}, .color = {0.0f, 0.0f, 1.0f}},
      {.position = {0.15f, 0.65f}, .color = {1.0f, 1.0f, 1.0f}}};

  Fpx3d_Vk_VertexBundle triangle_bundle = {0};
  Fpx3d_Vk_VertexBundle square_bundle = {0};

  PRINT_FAILURE(fpx3d_vk_allocate_vertices(
      &triangle_bundle, ARRAY_SIZE(triangle), sizeof(triangle[0])));
  PRINT_FAILURE(fpx3d_vk_allocate_vertices(&square_bundle, ARRAY_SIZE(square),
                                           sizeof(square[0])));

  PRINT_FAILURE(fpx3d_vk_append_vertices(&triangle_bundle, triangle,
                                         ARRAY_SIZE(triangle)));
  PRINT_FAILURE(
      fpx3d_vk_append_vertices(&square_bundle, square, ARRAY_SIZE(square)));

  uint32_t square_indices[] = {0, 1, 2, 2, 0, 3};

  PRINT_FAILURE(fpx3d_vk_set_indices(&square_bundle, square_indices,
                                     ARRAY_SIZE(square_indices)));

  Fpx3d_Vk_VertexAttribute vertex_attributes[] = {
      {.format = VEC2_32BIT_SFLOAT,
       .dataOffsetBytes = offsetof(Fpx3d_Vk_Vertex, position)},
      {.format = VEC3_32BIT_SFLOAT,
       .dataOffsetBytes = offsetof(Fpx3d_Vk_Vertex, color)}};

  Fpx3d_Vk_VertexBinding vertex_binding = {0};
  vertex_binding.sizePerVertex = sizeof(Fpx3d_Vk_Vertex);
  vertex_binding.attributeCount = 2;
  vertex_binding.attributes = vertex_attributes;

  // ----------- END OF VERTICES -----------

  PRINT_FAILURE(fpx3d_vk_init_context(&vk_ctx, &wnd_ctx));
  PRINT_FAILURE(fpx3d_vk_create_window(&vk_ctx));

  PRINT_FAILURE(fpx3d_vk_select_gpu(&vk_ctx, gpu_suitability));

  PRINT_FAILURE(fpx3d_vk_allocate_logicalgpus(&vk_ctx, 2));
  PRINT_FAILURE(
      fpx3d_vk_create_logicalgpu_at(&vk_ctx, 1, lgpu_features, 1, 1, 1));
  lgpu = fpx3d_vk_get_logicalgpu_at(&vk_ctx, 1);

  graphics_queue = fpx3d_vk_get_queue_at(lgpu, 0, GRAPHICS_QUEUE);
  present_queue = fpx3d_vk_get_queue_at(lgpu, 0, PRESENT_QUEUE);
  transfer_queue = fpx3d_vk_get_queue_at(lgpu, 0, TRANSFER_QUEUE);

  swapchain_properties =
      fpx3d_vk_get_swapchain_support(&vk_ctx, vk_ctx.physicalGpu, sc_reqs);
  PRINT_FAILURE(fpx3d_vk_create_swapchain(&vk_ctx, lgpu, swapchain_properties));

  PRINT_FAILURE(fpx3d_vk_allocate_commandpools(lgpu, 1));
  PRINT_FAILURE(fpx3d_create_commandpool_at(lgpu, 0, TRANSFER_POOL));
  transfer_pool = fpx3d_vk_get_commandpool_at(lgpu, 0);

  PRINT_FAILURE(fpx3d_vk_allocate_commandbuffers_at_pool(lgpu, 0, 1));
  transfer_buffer = fpx3d_vk_get_commandbuffer_at(transfer_pool, 0);

  PRINT_FAILURE(fpx3d_vk_allocate_renderpasses(lgpu, 1));
  PRINT_FAILURE(fpx3d_vk_create_renderpass_at(lgpu, 0));
  render_pass = fpx3d_vk_get_renderpass_at(lgpu, 0);

  PRINT_FAILURE(fpx3d_vk_create_framebuffers(
      fpx3d_vk_get_current_swapchain(lgpu), lgpu, render_pass));

  pipeline_layout = fpx3d_vk_create_pipeline_layout(lgpu);

  PRINT_FAILURE(fpx3d_vk_create_shapebuffer(&vk_ctx, lgpu, &triangle_bundle,
                                            &triangle_shape));
  PRINT_FAILURE(fpx3d_vk_create_shapebuffer(&vk_ctx, lgpu, &square_bundle,
                                            &square_shape));

  PRINT_FAILURE(fpx3d_vk_load_shadermodules(shaders, ARRAY_SIZE(shaders), lgpu,
                                            &modules));

  PRINT_FAILURE(fpx3d_vk_allocate_pipelines(lgpu, 1));

  PRINT_FAILURE(fpx3d_vk_create_graphics_pipeline_at(
      lgpu, 0, pipeline_layout, render_pass, &modules, &vertex_binding, 1));
  pipeline = fpx3d_vk_get_pipeline_at(lgpu, 0);

  Fpx3d_Vk_ShapeBuffer shapes[] = {triangle_shape, square_shape};

  PRINT_FAILURE(
      fpx3d_vk_add_shapes_to_pipeline(shapes, ARRAY_SIZE(shapes), pipeline));

  if (NULL == vk_ctx.windowContext->glfwWindow)
    exit(EXIT_FAILURE);

  while (!glfwWindowShouldClose(vk_ctx.windowContext->glfwWindow)) {
    glfwPollEvents();
    PRINT_FAILURE(fpx3d_vk_draw_frame(&vk_ctx, lgpu, pipeline, 1,
                                      graphics_queue, present_queue));
  }

  PRINT_FAILURE(fpx3d_vk_destroy_window(&vk_ctx, dest_callback));
}
