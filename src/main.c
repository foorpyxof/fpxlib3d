/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifdef DEBUG
#define FPX3D_DEBUG_ENABLE
#endif

#include "main.h"
#include "debug.h"
#include "fpx3d.h"
#include "vk.h"
#include "window.h"

#include <signal.h>

#if defined(_WIN32) || defined(_WIN64)

#include <pthread_time.h>
#include <windows.h>

#define SIGKILL SIGTERM

#endif

#include <GLFW/glfw3.h>
#include <cglm/affine.h>
#include <cglm/cam.h>
#include <cglm/io.h>
#include <cglm/mat4.h>
#include <cglm/util.h>
#include <cglm/vec3.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>

#define LOGICAL_GPU_COUNT 1

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(*array))

// clang-format off
#define PRINT_FAILURE(res) if (FPX3D_SUCCESS != res) { FPX3D_ERROR("A FUNCTION FAILED!"); }
#define FATAL_FAIL(res) if (FPX3D_SUCCESS != res) { FPX3D_ERROR("FATAL FAILURE"); raise(SIGKILL); }
// clang-format on

vec3 camera_pos = {0.7f, 3.3f, 2.6f};
vec3 camera_target = {0.0f, 0.0f, 0.0f};

float fov = 80.0f;

float move_speed = 2.0f;

struct model_descriptor triangle_model = {0};
struct model_descriptor square_model = {0};
struct model_descriptor square_dupe_model = {0};
struct model_descriptor pyramid_model = {0};

bool key_states[GLFW_KEY_LAST + 1] = {0};
void key_cb(GLFWwindow *wnd, int key, int scancode, int action, int mods) {
  if (NULL == wnd && scancode == 0 && mods == 0) {
  }

  if (key < 0 || key > GLFW_KEY_LAST)
    return;

  if (action & (GLFW_PRESS | GLFW_REPEAT)) {
    key_states[key] = true;

    if (key == GLFW_KEY_EQUAL)
      move_speed += 0.5f;
    else if (key == GLFW_KEY_MINUS)
      move_speed -= 0.5f;
    else if (key == GLFW_KEY_R)
      glm_mat4_identity(square_model.model);

  } else {
    key_states[key] = false;
  }
}

void handle_inputs(float delta) {
  if (0 >= move_speed)
    return;

  if (key_states[GLFW_KEY_W]) {
    camera_pos[1] -= move_speed * delta;
  }

  if (key_states[GLFW_KEY_A]) {
    camera_pos[0] += move_speed * delta;
  }

  if (key_states[GLFW_KEY_S]) {
    camera_pos[1] += move_speed * delta;
  }

  if (key_states[GLFW_KEY_D]) {
    camera_pos[0] -= move_speed * delta;
  }

  if (key_states[GLFW_KEY_Q]) {
    camera_pos[2] -= move_speed * delta;
  }

  if (key_states[GLFW_KEY_E]) {
    camera_pos[2] += move_speed * delta;
  }

  if (key_states[GLFW_KEY_O]) {
    fov += 20.0f * delta;
  }

  if (key_states[GLFW_KEY_P]) {
    fov -= 20.0f * delta;
  }
}

void dest_callback(void *);

Fpx3d_Vk_Context vk_ctx = {0};

const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

struct shape_buffer *shapes = NULL;

static void sig_catcher(int signo) {
  static bool called = false;
  if (called) {
    raise(signo);
  }
  called = true;

  signal(signo, SIG_DFL);

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

// ----- SHAPES

Fpx3d_Vk_ShapeBuffer triangle_buffer = {0};
Fpx3d_Vk_ShapeBuffer square_buffer = {0};
Fpx3d_Vk_ShapeBuffer pyramid_buffer = {0};
Fpx3d_Vk_Shape triangle_shape = {0};
Fpx3d_Vk_Shape square_shape = {0};
Fpx3d_Vk_Shape square_dupe = {0};
Fpx3d_Vk_Shape pyramid_shape = {0};

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

void dest_callback(void *custom_ptr) {
  fprintf(stderr, "%s\n", (const char *)custom_ptr);

  if (VK_NULL_HANDLE == lgpu->handle)
    return;

  vkDeviceWaitIdle(lgpu->handle);

  PRINT_FAILURE(fpx3d_vk_destroy_shape(&triangle_shape, &vk_ctx, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_shape(&square_shape, &vk_ctx, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_shape(&square_dupe, &vk_ctx, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_shape(&pyramid_shape, &vk_ctx, lgpu));

  PRINT_FAILURE(fpx3d_vk_destroy_shapebuffer(lgpu, &triangle_buffer));
  PRINT_FAILURE(fpx3d_vk_destroy_shapebuffer(lgpu, &square_buffer));
  PRINT_FAILURE(fpx3d_vk_destroy_shapebuffer(lgpu, &pyramid_buffer));

  PRINT_FAILURE(fpx3d_vk_destroy_descriptor_set_layout(ds_layout_global, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_descriptor_set_layout(ds_layout_shape, lgpu));

  PRINT_FAILURE(fpx3d_vk_destroy_shadermodules(&modules, lgpu));
  PRINT_FAILURE(fpx3d_vk_destroy_pipeline_layout(&pipeline_layout, lgpu));
}

VkPhysicalDeviceFeatures lgpu_features = {
    .geometryShader = VK_TRUE,
};

Fpx3d_Wnd_Context wnd_ctx = {.glfwWindow = NULL,
                             .windowDimensions = {800, 800},
                             .windowTitle = "YAY",
                             .resized = false};

void vulkan_setup(void) {
  fpx3d_vk_set_required_surfaceformats(&sc_reqs, formats, ARRAY_SIZE(formats));
  fpx3d_vk_set_required_presentmodes(&sc_reqs, modes, ARRAY_SIZE(modes));

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

    vk_ctx.instanceLayers = NULL;
    vk_ctx.instanceLayerCount = 0;
    vk_ctx.lgpuExtensions = extensions;
    vk_ctx.lgpuExtensionCount = ARRAY_SIZE(extensions);

    PRINT_FAILURE(fpx3d_vk_set_custom_pointer(&vk_ctx, "🏳️‍⚧️"));
  }

  FATAL_FAIL(fpx3d_vk_init_context(&vk_ctx, &wnd_ctx));
  vk_ctx.constants.maxFramesInFlight = 2;

  FATAL_FAIL(fpx3d_vk_create_window(&vk_ctx));

  FATAL_FAIL(fpx3d_vk_select_gpu(&vk_ctx, gpu_suitability));

  FATAL_FAIL(fpx3d_vk_allocate_logicalgpus(&vk_ctx, 1));
  FATAL_FAIL(fpx3d_vk_create_logicalgpu_at(&vk_ctx, 0, lgpu_features, 1, 1, 1));
  lgpu = fpx3d_vk_get_logicalgpu_at(&vk_ctx, 0);

  graphics_queue = fpx3d_vk_get_queue_at(lgpu, 0, GRAPHICS_QUEUE);
  present_queue = fpx3d_vk_get_queue_at(lgpu, 0, PRESENT_QUEUE);

  FATAL_FAIL(fpx3d_vk_create_swapchain(&vk_ctx, lgpu, sc_reqs));

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
}

Fpx3d_Vk_SpirvFile shaders[] = {{0}, {0}};
Fpx3d_Vk_SpirvFile vertex_shader = {0};
Fpx3d_Vk_SpirvFile fragment_shader = {0};
void load_shaders(void) {
  // DEFAULT VERTEX SHADER
  shaders[0] =
      fpx3d_vk_read_spirv_file("shaders/default.vert.spv", SHADER_STAGE_VERTEX);

  // DEFAULT FRAGMENT SHADER
  shaders[1] = fpx3d_vk_read_spirv_file("shaders/default.frag.spv",
                                        SHADER_STAGE_FRAGMENT);

  FATAL_FAIL(fpx3d_vk_load_shadermodules(shaders, ARRAY_SIZE(shaders), lgpu,
                                         &modules));
}

Fpx3d_Vk_VertexAttribute vertex_attributes[] = {
    {.format = VEC3_32BIT_SFLOAT,
     .dataOffsetBytes = offsetof(Fpx3d_Vk_Vertex, position)},
    {.format = VEC3_32BIT_SFLOAT,
     .dataOffsetBytes = offsetof(Fpx3d_Vk_Vertex, color)}};

Fpx3d_Vk_VertexBinding vertex_binding = {0};
void create_shapes(void) {
  Fpx3d_Vk_Vertex triangle[] = {
      {.position = {0.0f, -0.5f, 0.0f}, .color = {1.0f, 0.0f, 0.0f}},
      {.position = {0.5f, 0.5f, 0.0f}, .color = {0.0f, 1.0f, 0.0f}},
      {.position = {-0.5f, 0.5f, 0.0f}, .color = {0.0f, 0.0f, 1.0f}}};

  Fpx3d_Vk_Vertex square[] = {
      {.position = {-0.25f, -0.25f, 0.0f}, .color = {1.0f, 0.0f, 0.0f}},
      {.position = {0.25f, -0.25f, 0.0f}, .color = {0.0f, 1.0f, 0.0f}},
      {.position = {0.25f, 0.25f, 0.0f}, .color = {0.0f, 0.0f, 1.0f}},
      {.position = {-0.25f, 0.25f, 0.0f}, .color = {1.0f, 1.0f, 1.0f}}};

  Fpx3d_Vk_Vertex pyramid[] = {
      {.position = {0.0f, 0.0f, 1.5f}, .color = {1.0f, 1.0f, 1.0f}},
      {.position = {-0.5f, -0.5f, 0.5f}, .color = {0.0f, 0.0f, 0.0f}},
      {.position = {-0.5f, 0.5f, 0.5f}, .color = {0.0f, 0.0f, 1.0f}},
      {.position = {0.5f, 0.5f, 0.5f}, .color = {0.0f, 1.0f, 0.0f}},
      {.position = {0.5f, -0.5f, 0.5f}, .color = {1.0f, 0.0f, 0.0f}}};

  Fpx3d_Vk_VertexBundle triangle_bundle = {0};
  Fpx3d_Vk_VertexBundle square_bundle = {0};
  Fpx3d_Vk_VertexBundle pyramid_bundle = {0};

  PRINT_FAILURE(fpx3d_vk_allocate_vertices(
      &triangle_bundle, ARRAY_SIZE(triangle), sizeof(triangle[0])));
  PRINT_FAILURE(fpx3d_vk_allocate_vertices(&square_bundle, ARRAY_SIZE(square),
                                           sizeof(square[0])));
  PRINT_FAILURE(fpx3d_vk_allocate_vertices(&pyramid_bundle, ARRAY_SIZE(pyramid),
                                           sizeof(pyramid[0])));

  PRINT_FAILURE(fpx3d_vk_append_vertices(&triangle_bundle, triangle,
                                         ARRAY_SIZE(triangle)));
  PRINT_FAILURE(
      fpx3d_vk_append_vertices(&square_bundle, square, ARRAY_SIZE(square)));
  PRINT_FAILURE(
      fpx3d_vk_append_vertices(&pyramid_bundle, pyramid, ARRAY_SIZE(pyramid)));

  uint32_t square_indices[] = {0, 1, 2, 2, 0, 3};
  uint32_t pyramid_indices[] = {1, 2, 3, 3, 1, 4, 1, 0, 2,
                                3, 0, 2, 3, 0, 4, 1, 0, 4};

  PRINT_FAILURE(fpx3d_vk_set_indices(&square_bundle, square_indices,
                                     ARRAY_SIZE(square_indices)));
  PRINT_FAILURE(fpx3d_vk_set_indices(&pyramid_bundle, pyramid_indices,
                                     ARRAY_SIZE(pyramid_indices)));

  FATAL_FAIL(fpx3d_vk_create_shapebuffer(&vk_ctx, lgpu, &triangle_bundle,
                                         &triangle_buffer));
  FATAL_FAIL(fpx3d_vk_create_shapebuffer(&vk_ctx, lgpu, &square_bundle,
                                         &square_buffer));
  FATAL_FAIL(fpx3d_vk_create_shapebuffer(&vk_ctx, lgpu, &pyramid_bundle,
                                         &pyramid_buffer));

  vertex_binding.sizePerVertex = sizeof(Fpx3d_Vk_Vertex);
  vertex_binding.attributeCount = ARRAY_SIZE(vertex_attributes);
  vertex_binding.attributes = vertex_attributes;

  triangle_shape = fpx3d_vk_create_shape(&triangle_buffer);
  square_shape = fpx3d_vk_create_shape(&square_buffer);
  pyramid_shape = fpx3d_vk_create_shape(&pyramid_buffer);

  if (!(triangle_shape.isValid && square_shape.isValid &&
        pyramid_shape.isValid)) {
    FPX3D_ERROR("BAD SHAPE CREATION");
    raise(SIGTERM);
  }
}

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
void create_descriptor_sets(void) {
  *ds_layout_shape = fpx3d_vk_create_descriptor_set_layout(
      object_ds_bindings, ARRAY_SIZE(object_ds_bindings), lgpu);
  *ds_layout_global = fpx3d_vk_create_descriptor_set_layout(
      pipeline_ds_bindings, ARRAY_SIZE(pipeline_ds_bindings), lgpu);
}

void create_pipelines(void) {
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
}

void create_shape_descriptors(void) {
  FATAL_FAIL(fpx3d_vk_create_shape_descriptors(
      &triangle_shape, object_ds_bindings, ARRAY_SIZE(object_ds_bindings),
      ds_layout_shape, &vk_ctx, lgpu));
  FATAL_FAIL(fpx3d_vk_create_shape_descriptors(
      &square_shape, object_ds_bindings, ARRAY_SIZE(object_ds_bindings),
      ds_layout_shape, &vk_ctx, lgpu));
  FATAL_FAIL(fpx3d_vk_create_shape_descriptors(
      &pyramid_shape, object_ds_bindings, ARRAY_SIZE(object_ds_bindings),
      ds_layout_shape, &vk_ctx, lgpu));
}

void create_duplicate_shapes(void) {
  square_dupe = fpx3d_vk_duplicate_shape(&square_shape, &vk_ctx, lgpu);

  if (!(square_dupe.isValid)) {
    FPX3D_ERROR("BAD SHAPE DUPLICATION");
    raise(SIGTERM);
  }
}

void create_pipeline_descriptors(void) {
  FATAL_FAIL(fpx3d_vk_create_pipeline_descriptors(
      pipeline, pipeline_ds_bindings, ARRAY_SIZE(pipeline_ds_bindings), &vk_ctx,
      lgpu));
}

void destroy_vulkan(void) {
  fpx3d_vk_destroy_window(&vk_ctx, dest_callback);
  exit(EXIT_SUCCESS);
}

int main(int argc, const char **argv) {
  signal(SIGINT, sig_catcher);
  signal(SIGABRT, sig_catcher);
  signal(SIGTERM, sig_catcher);

  vulkan_setup();
  load_shaders();
  create_shapes();
  create_descriptor_sets();
  create_pipelines();
  create_shape_descriptors();
  create_duplicate_shapes();
  create_pipeline_descriptors();

  Fpx3d_Vk_Shape *shapes[] = {&pyramid_shape, &triangle_shape, &square_shape,
                              &square_dupe};

  PRINT_FAILURE(
      fpx3d_vk_assign_shapes_to_pipeline(shapes, ARRAY_SIZE(shapes), pipeline));

  struct vp_descriptor view_projection = {0};

  // define directions
  vec3 up = {0.0f, 0.0f, 1.0f};
  vec3 right = {-1.0f, 0.0f, 0.0f};
  // vec3 forward = {0.0f, -1.0f, 0.0f};

  glm_mat4_identity(view_projection.view);
  glm_mat4_identity(view_projection.projection);

  fpx3d_vk_update_pipeline_descriptor(pipeline, 0, 0, &view_projection,
                                      &vk_ctx);

  glm_mat4_identity(triangle_model.model);
  glm_mat4_identity(square_model.model);
  glm_mat4_identity(square_dupe_model.model);
  glm_mat4_identity(pyramid_model.model);

  vec3 dupe_pos = {-0.3f, 0.5f, 0.1f};
  glm_translate(square_dupe_model.model, dupe_pos);

  vec3 pyramid_pos = {0.0f, 0.0f, 1.0f};
  glm_translate(pyramid_model.model, pyramid_pos);

  fpx3d_vk_update_shape_descriptor(&triangle_shape, 0, 0, &triangle_model,
                                   &vk_ctx);
  fpx3d_vk_update_shape_descriptor(&square_shape, 0, 0, &square_model, &vk_ctx);
  fpx3d_vk_update_shape_descriptor(&square_dupe, 0, 0, &square_dupe_model,
                                   &vk_ctx);
  fpx3d_vk_update_shape_descriptor(&pyramid_shape, 0, 0, &pyramid_model,
                                   &vk_ctx);

  if (NULL == vk_ctx.windowContext->glfwWindow)
    exit(EXIT_FAILURE);

  glfwSetKeyCallback(wnd_ctx.glfwWindow, key_cb);

  float delta = 0.02f;

#if !(defined(_WIN32) || defined(_WIN64))
  struct timespec frametimes[2] = {0};

  clock_gettime(CLOCK_REALTIME, &frametimes[0]);
#endif

  float plane_speed = 1.0f;
  float sign = 1;

  if (argc > 1 && NULL != argv) {
    fprintf(stderr, "\033[2J\033[H");
    fprintf(stderr, "Delta: %f\n", delta);
    fprintf(stderr, "FPS: %d\n", (int)(1 / delta));
    fprintf(stderr, "\n");
    fprintf(stderr, "Move speed: %f\n", move_speed);
    fprintf(stderr, "FOV: %ddeg\n", (int)fov);
    fprintf(stderr, "\n");
    fprintf(stderr, "Camera position:\n");
    glm_vec3_print(camera_pos, stdout);
    fprintf(stderr, "\n");
    fprintf(stderr, "Movement keys:\n"
                    "     [W]\n"
                    "  [A][S][D]\n"
                    "  For moving the camera's absolute position along the "
                    "horizontal plane\n"
                    "  Please note that the camera is permanently focused on "
                    "(0, 0, 0)\n  and the camera moves on fixed axes. Not "
                    "towards the direction it faces\n"
                    "\n"
                    "  [Q]   [E]\n"
                    "  For moving up and down\n"
                    "\n"
                    "  [O][P]\n"
                    "  For increasing and decreasing the FOV respectively\n"
                    "\n"
                    "  [-][+]\n"
                    "  For decreasing/increasing camera movement speed\n");
  }

  while (!glfwWindowShouldClose(vk_ctx.windowContext->glfwWindow)) {
    glfwPollEvents();

    if (argc > 1 && NULL != argv) {
      fprintf(stderr, "\033[H");
      fprintf(stderr, "\033[2KDelta: %f\n", delta);
      fprintf(stderr, "\033[2KFPS: %d\n", (int)(1 / delta));
      fprintf(stderr, "\033[2K\n");
      fprintf(stderr, "\033[2KMove speed: %f\n", move_speed);
      fprintf(stderr, "\033[2KFOV: %ddeg\n", (int)fov);
      fprintf(stderr, "\033[2K\n");
      fprintf(stderr, "\033[2KCamera position:\n");
      glm_vec3_print(camera_pos, stdout);
    }

    handle_inputs(delta);

    Fpx3d_Vk_Swapchain *csc = fpx3d_vk_get_current_swapchain(lgpu);

    glm_lookat(camera_pos, camera_target, up, view_projection.view);
    glm_perspective(glm_rad(fov),
                    (float)((float)csc->swapchainExtent.width /
                            (float)csc->swapchainExtent.height),
                    0.1f, 20.0f, view_projection.projection);

    // for Vulkan, bcs Y is inverted compared to GL
    view_projection.projection[1][1] *= -1;

    fpx3d_vk_update_pipeline_descriptor(pipeline, 0, 0, &view_projection,
                                        &vk_ctx);

#define PLANE_MAX_SPEED 8.0f

    vec3 tran = {0.0f, -plane_speed, 0.0f};
    plane_speed += 2.5f * delta * sign;
    if (plane_speed > PLANE_MAX_SPEED)
      sign = -1.0f;
    else if (plane_speed < -PLANE_MAX_SPEED)
      sign = 1.0f;

    glm_vec3_scale(tran, delta * 3.0f, tran);
    glm_translate(square_model.model, tran);
    glm_rotate(square_model.model, glm_rad(1080.0f * delta), right);
    fpx3d_vk_update_shape_descriptor(&square_shape, 0, 0, &square_model,
                                     &vk_ctx);

    glm_rotate(square_dupe_model.model, glm_rad(180.0f * delta), up);
    fpx3d_vk_update_shape_descriptor(&square_dupe, 0, 0, &square_dupe_model,
                                     &vk_ctx);

    glm_rotate(pyramid_model.model, glm_rad(45.0f * delta), up);
    fpx3d_vk_update_shape_descriptor(&pyramid_shape, 0, 0, &pyramid_model,
                                     &vk_ctx);

    PRINT_FAILURE(fpx3d_vk_draw_frame(&vk_ctx, lgpu, pipeline, 1,
                                      graphics_queue, present_queue));

#if !(defined(_WIN32) || defined(_WIN64))
    clock_gettime(CLOCK_REALTIME, &frametimes[1]);
    delta =
        (float)(frametimes[1].tv_sec - frametimes[0].tv_sec) +
        (float)(frametimes[1].tv_nsec / 1.0e9 - frametimes[0].tv_nsec / 1.0e9);
    clock_gettime(CLOCK_REALTIME, &frametimes[0]);
#endif
  }

  PRINT_FAILURE(fpx3d_vk_destroy_window(&vk_ctx, dest_callback));
}
