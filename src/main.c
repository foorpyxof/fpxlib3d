/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include "debug.h"
#include "fpx3d.h"
#include "vk.h"
#include "window.h"

#include <GLFW/glfw3.h>
#include <cglm/affine.h>
#include <cglm/cam.h>
#include <cglm/io.h>
#include <cglm/mat4.h>
#include <cglm/util.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>

#define LOGICAL_GPU_COUNT 1

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(*array))

// clang-format off
#define PRINT_FAILURE(res) if (FPX3D_SUCCESS != res) { FPX3D_ERROR("A FUNCTION FAILED!"); }
#define FATAL_FAIL(res) if (FPX3D_SUCCESS != res) { FPX3D_ERROR("FATAL FAILURE"); raise(SIGKILL); }
// clang-format on

vec3 eye = {0.0f, 0.0f, 2.0f};
vec3 center = {0.0f, 0.0f, 0.0f};

float fov = 45.0f;

void key_cb(GLFWwindow *wnd, int key, int scancode, int action, int mods) {
  if (NULL == wnd && scancode == 0 && mods == 0) {
  }

  if (action & (GLFW_PRESS | GLFW_REPEAT)) {
    switch (key) {
    case GLFW_KEY_Q:
      eye[0] -= 0.1f;
      break;
    case GLFW_KEY_A:
      eye[0] += 0.1f;
      break;
    case GLFW_KEY_W:
      eye[2] -= 0.1f;
      break;
    case GLFW_KEY_S:
      eye[2] += 0.1f;
      break;
    case GLFW_KEY_E:
      eye[1] -= 0.1f;
      break;
    case GLFW_KEY_D:
      eye[1] += 0.1f;
      break;
    case GLFW_KEY_O:
      fov += 1.0f;
      break;
    case GLFW_KEY_P:
      fov -= 1.0f;
      break;
    }
  }
}

void dest_callback(void *);

Fpx3d_Vk_Context vk_ctx = {0};

const char *val_layers[] = {"VK_LAYER_KHRONOS_validation"};
const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

struct shape_buffer *shapes = NULL;

static void sig_catcher(int signo) {
  static bool called = false;
  if (called)
    return;
  called = true;

  const char *type = NULL;
  switch (signo) {
  case SIGINT:
    type = "Interrupt ";
    break;
  case SIGTERM:
    type = "Terminate ";
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

Fpx3d_Vk_LogicalGpu *lgpu = NULL;

// ----- SHADER STUFF

Fpx3d_Vk_ShaderModuleSet modules = {0};

Fpx3d_Vk_DescriptorSetLayout ds_layouts[2] = {0};

Fpx3d_Vk_DescriptorSetLayout *ds_layout_global = &ds_layouts[0];
Fpx3d_Vk_DescriptorSetLayout *ds_layout_shape = &ds_layouts[1];

struct model_descriptor {
  mat4 model;
};
struct vp_descriptor {
  mat4 view;
  mat4 projection;
};

// ----- SHAPES

Fpx3d_Vk_ShapeBuffer triangle_buffer = {0};
Fpx3d_Vk_ShapeBuffer square_buffer = {0};
Fpx3d_Vk_Shape triangle_shape = {0};
Fpx3d_Vk_Shape square_shape = {0};
Fpx3d_Vk_Shape square_dupe = {0};

// ----- QUEUES

VkQueue *graphics_queue = NULL;
VkQueue *present_queue = NULL;
VkQueue *transfer_queue = NULL;

// ----- PIPELINE STUFF

Fpx3d_Vk_PipelineLayout pipeline_layout = {0};
Fpx3d_Vk_Pipeline *pipeline = NULL;

// ----- COMMAND STUFF

Fpx3d_Vk_CommandPool *transfer_pool = NULL;
VkCommandBuffer *transfer_buffer = NULL;

// ----- OTHER

VkRenderPass *render_pass = NULL;

Fpx3d_Vk_SwapchainProperties swapchain_properties = {0};

void dest_callback(void *custom_ptr) {
  fprintf(stderr, "%s\n", (const char *)custom_ptr);

  if (VK_NULL_HANDLE == lgpu->handle)
    return;

  vkDeviceWaitIdle(lgpu->handle);

  PRINT_FAILURE(fpx3d_vk_destroy_shape(&triangle_shape, &vk_ctx, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_shape(&square_shape, &vk_ctx, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_shape(&square_dupe, &vk_ctx, lgpu));

  PRINT_FAILURE(fpx3d_vk_destroy_shapebuffer(lgpu, &triangle_buffer));
  PRINT_FAILURE(fpx3d_vk_destroy_shapebuffer(lgpu, &square_buffer));

  PRINT_FAILURE(fpx3d_vk_destroy_descriptor_set_layout(ds_layout_global, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_descriptor_set_layout(ds_layout_shape, lgpu));

  PRINT_FAILURE(fpx3d_vk_destroy_shadermodules(&modules, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_pipeline_layout(&pipeline_layout, lgpu));
}

int main(void) {
  signal(SIGINT, sig_catcher);
  signal(SIGABRT, sig_catcher);
  signal(SIGTERM, sig_catcher);

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

  FATAL_FAIL(fpx3d_vk_init_context(&vk_ctx, &wnd_ctx));
  FATAL_FAIL(fpx3d_vk_create_window(&vk_ctx));

  FATAL_FAIL(fpx3d_vk_select_gpu(&vk_ctx, gpu_suitability));

  FATAL_FAIL(fpx3d_vk_allocate_logicalgpus(&vk_ctx, 1));
  FATAL_FAIL(fpx3d_vk_create_logicalgpu_at(&vk_ctx, 0, lgpu_features, 1, 1, 1));
  lgpu = fpx3d_vk_get_logicalgpu_at(&vk_ctx, 0);

  graphics_queue = fpx3d_vk_get_queue_at(lgpu, 0, GRAPHICS_QUEUE);
  present_queue = fpx3d_vk_get_queue_at(lgpu, 0, PRESENT_QUEUE);

  swapchain_properties =
      fpx3d_vk_get_swapchain_support(&vk_ctx, vk_ctx.physicalGpu, sc_reqs);
  FATAL_FAIL(fpx3d_vk_create_swapchain(&vk_ctx, lgpu, swapchain_properties));

  PRINT_FAILURE(fpx3d_vk_allocate_commandpools(lgpu, 1));
  PRINT_FAILURE(fpx3d_create_commandpool_at(lgpu, 0, TRANSFER_POOL));
  transfer_pool = fpx3d_vk_get_commandpool_at(lgpu, 0);

  PRINT_FAILURE(fpx3d_vk_allocate_commandbuffers_at_pool(lgpu, 0, 1));
  transfer_buffer = fpx3d_vk_get_commandbuffer_at(transfer_pool, 0);

  FATAL_FAIL(fpx3d_vk_allocate_renderpasses(lgpu, 1));
  FATAL_FAIL(fpx3d_vk_create_renderpass_at(lgpu, 0));
  render_pass = fpx3d_vk_get_renderpass_at(lgpu, 0);

  FATAL_FAIL(fpx3d_vk_create_framebuffers(fpx3d_vk_get_current_swapchain(lgpu),
                                          lgpu, render_pass));

  Fpx3d_Vk_DescriptorSetBinding pipeline_ds_bindings[] = {
      {.elementCount = 1,
       .elementSize = sizeof(struct vp_descriptor),
       .type = DESC_UNIFORM,
       .shaderStages = SHADER_STAGE_VERTEX}};

  Fpx3d_Vk_DescriptorSetBinding object_ds_bindings[] = {
      {.elementCount = 1,
       .elementSize = sizeof(struct model_descriptor),
       .type = DESC_UNIFORM,
       .shaderStages = SHADER_STAGE_VERTEX}};

  *ds_layout_shape = fpx3d_vk_create_descriptor_set_layout(
      object_ds_bindings, ARRAY_SIZE(object_ds_bindings), lgpu);
  *ds_layout_global = fpx3d_vk_create_descriptor_set_layout(
      pipeline_ds_bindings, ARRAY_SIZE(pipeline_ds_bindings), lgpu);

  FATAL_FAIL(fpx3d_vk_create_shapebuffer(&vk_ctx, lgpu, &triangle_bundle,
                                         &triangle_buffer));
  FATAL_FAIL(fpx3d_vk_create_shapebuffer(&vk_ctx, lgpu, &square_bundle,
                                         &square_buffer));

  triangle_shape = fpx3d_vk_create_shape(&triangle_buffer);
  square_shape = fpx3d_vk_create_shape(&square_buffer);

  if (!(triangle_shape.isValid && square_shape.isValid)) {
    FPX3D_ERROR("BAD SHAPE CREATION");
    raise(SIGTERM);
  }

  FATAL_FAIL(fpx3d_vk_load_shadermodules(shaders, ARRAY_SIZE(shaders), lgpu,
                                         &modules));

  pipeline_layout =
      fpx3d_vk_create_pipeline_layout(ds_layouts, ARRAY_SIZE(ds_layouts), lgpu);

  if (false == pipeline_layout.isValid) {
    FPX3D_ERROR("BAD PIPELINE LAYOUT CREATION");
    raise(SIGTERM);
  }

  FATAL_FAIL(fpx3d_vk_allocate_pipelines(lgpu, 1));

  FATAL_FAIL(fpx3d_vk_create_graphics_pipeline_at(
      lgpu, 0, &pipeline_layout, render_pass, &modules, &vertex_binding, 1));
  pipeline = fpx3d_vk_get_pipeline_at(lgpu, 0);

  // ----------------- DESCRIPTOR SETS

  FATAL_FAIL(fpx3d_vk_create_shape_descriptors(
      &triangle_shape, object_ds_bindings, ARRAY_SIZE(object_ds_bindings),
      ds_layout_shape, &vk_ctx, lgpu));
  FATAL_FAIL(fpx3d_vk_create_shape_descriptors(
      &square_shape, object_ds_bindings, ARRAY_SIZE(object_ds_bindings),
      ds_layout_shape, &vk_ctx, lgpu));

  FATAL_FAIL(fpx3d_vk_create_pipeline_descriptors(
      pipeline, pipeline_ds_bindings, ARRAY_SIZE(pipeline_ds_bindings), &vk_ctx,
      lgpu));

  // ---------- END OF DESCRIPTOR SETS

  square_dupe = fpx3d_vk_duplicate_shape(&square_shape, &vk_ctx, lgpu);

  Fpx3d_Vk_Shape *shapes[] = {&triangle_shape, &square_shape, &square_dupe};

  PRINT_FAILURE(
      fpx3d_vk_assign_shapes_to_pipeline(shapes, ARRAY_SIZE(shapes), pipeline));

  struct model_descriptor triangle_model = {0};
  struct model_descriptor square_model = {0};
  struct model_descriptor square_dupe_model = {0};

  struct vp_descriptor view_project = {0};

  vec3 up = {0.0f, 0.0f, 1.0f};

  glm_mat4_identity(triangle_model.model);
  memcpy(square_model.model, triangle_model.model, sizeof(square_model.model));
  memcpy(square_dupe_model.model, triangle_model.model,
         sizeof(square_dupe_model.model));

  glm_rotate(square_dupe_model.model, glm_rad(45.0f), up);

  fpx3d_vk_update_shape_descriptor(&triangle_shape, 0, 0, &triangle_model,
                                   &vk_ctx);
  fpx3d_vk_update_shape_descriptor(&square_shape, 0, 0, &square_model, &vk_ctx);
  fpx3d_vk_update_shape_descriptor(&square_dupe, 0, 0, &square_dupe_model,
                                   &vk_ctx);

  if (NULL == vk_ctx.windowContext->glfwWindow)
    exit(EXIT_FAILURE);

  glfwSetKeyCallback(wnd_ctx.glfwWindow, key_cb);

  while (!glfwWindowShouldClose(vk_ctx.windowContext->glfwWindow)) {
    glfwPollEvents();

    Fpx3d_Vk_Swapchain *csc = fpx3d_vk_get_current_swapchain(lgpu);

    glm_lookat(eye, center, up, view_project.view);
    glm_perspective(glm_rad(fov),
                    (float)((float)csc->swapchainExtent.width /
                            (float)csc->swapchainExtent.height),
                    0.1f, 10.0f, view_project.projection);

    // for Vulkan
    view_project.projection[1][1] *= -1;

    fpx3d_vk_update_pipeline_descriptor(pipeline, 0, 0, &view_project, &vk_ctx);

    vec3 tran = {0.1f, 0.1f, 0.1f};
    glm_translate(square_model.model, tran);
    fpx3d_vk_update_shape_descriptor(&square_shape, 0, 0, &square_model,
                                     &vk_ctx);

    // sleep(1);
    // printf("\033[2J\033[H");
    // glm_mat4_print(
    //     square_shape.bindings.inFlightDescriptorSets[lgpu->frameCounter]
    //         .buffer.mapped_memory,
    //     stdout);

    PRINT_FAILURE(fpx3d_vk_draw_frame(&vk_ctx, lgpu, pipeline, 1,
                                      graphics_queue, present_queue));
  }

  PRINT_FAILURE(fpx3d_vk_destroy_window(&vk_ctx, dest_callback));
}
