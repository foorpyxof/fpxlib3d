#include "vk.h"

#include <GLFW/glfw3.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>

#define LOGICAL_GPU_COUNT 1

window_context w_ctx = {.glfw_window = NULL,
                        .window_dimensions = {500, 500},
                        .window_title = "YAY VULKAN RENDERER"};

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

  fprintf(stderr, "%ssignal received. Exiting\n", type);
  destroy_vulkan_window(&w_ctx);

  exit(128 + signo);
}

VkSurfaceFormatKHR formats[] = {
    {.format = VK_FORMAT_B8G8R8A8_SRGB,
     .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};

VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR};

const struct swapchain_requirements sc_reqs = {
    .surface_formats = formats,
    .surface_formats_count = (sizeof(formats) / sizeof(*formats)),
    .present_modes = modes,
    .present_modes_count = (sizeof(modes) / sizeof(*modes))};

int gpu_suitability(vulkan_context *ctx, VkPhysicalDevice to_score) {
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
    struct swapchain_details sc_details = {0};

    sc_details = swapchain_compatibility(ctx, to_score);

    if (!(sc_details.swapchains_available && sc_details.surface_format_valid &&
          sc_details.present_mode_valid)) {
      score = 0;
      goto gpu_suitability_ret;
    }
  }

gpu_suitability_ret:
  return score * multiplier;
}

int setup_vulkan(window_context *w_ctx, vulkan_context *vk_ctx,
                 struct indices *indices) {
  w_ctx->vk_context = vk_ctx;

  {
    int success = create_vulkan_window(w_ctx);
    if (VK_SUCCESS != success) {
      fprintf(stderr,
              "Error creating Vulkan instance, window or surface. Code: %d\n",
              success);
      return success;
    }

#ifdef DEBUG
    fprintf(stderr,
            "Successfully created Vulkan instance, window and surface\n");
#endif
  }

  save_swapchain_requirements(vk_ctx, &sc_reqs);

  {
    int success = choose_vulkan_gpu(vk_ctx, gpu_suitability);

    if (0 != success) {
      // failed
      fprintf(stderr, "Could not find a suitable GPU to use.\n");
      return success;
    } else {
#ifdef DEBUG
      fprintf(stderr, "Successfully picked a GPU to use\n");
#endif
      VkPhysicalDeviceProperties prop;
      vkGetPhysicalDeviceProperties(vk_ctx->physical_gpu, &prop);
      fprintf(stderr, "Using Vulkan GPU \"%s\"\n", prop.deviceName);
    }
  }

  int lgpu_index = -1;

  {
    vk_ctx->logical_gpus = (struct logical_gpu_info *)calloc(
        LOGICAL_GPU_COUNT, sizeof(struct logical_gpu_info));
    if (NULL == vk_ctx->logical_gpus) {
      perror("calloc()");
      return EXIT_FAILURE;
    }
    vk_ctx->lg_capacity = LOGICAL_GPU_COUNT;

    VkPhysicalDeviceFeatures features = {0};
    lgpu_index = new_logical_vulkan_gpu(vk_ctx, 1.0f, features, 1, 1);

    if (0 > lgpu_index) {
      // failure
      fprintf(stderr, "Error while creating logical GPU. Error code: %d\n",
              lgpu_index);
      return lgpu_index;
    }

#ifdef DEBUG
    fprintf(stderr, "Successfully created a logical GPU device\n");
#endif
  }

  struct logical_gpu_info *lgpu = &vk_ctx->logical_gpus[lgpu_index];

  for (int i = 0; i < lgpu->render_capacity; ++i) {
    if (0 > new_vulkan_queue(vk_ctx, indices, RENDER))
      return EXIT_FAILURE;
  }

  for (int i = 0; i < lgpu->presentation_capacity; ++i) {
    if (0 > new_vulkan_queue(vk_ctx, indices, PRESENTATION))
      return EXIT_FAILURE;
  }

#ifdef DEBUG
  fprintf(stderr, "Successfully created Vulkan queues\n");
#endif

  {
    struct swapchain_details details =
        swapchain_compatibility(vk_ctx, vk_ctx->physical_gpu);
    int success = create_vulkan_swap_chain(w_ctx, &details, indices);

    if (VK_SUCCESS == success) {
#ifdef DEBUG
      fprintf(stderr, "Successfully created swap chain\n");
#endif
    }
  }

  struct spirv_file triangle_vertex_shader =
      read_spirv("shaders/triangle.vert.spv", VERTEX);

  struct spirv_file triangle_fragment_shader =
      read_spirv("shaders/triangle.frag.spv", FRAGMENT);

  struct spirv_file square_vertex_shader =
      read_spirv("shaders/square.vert.spv", VERTEX);

  struct spirv_file square_fragment_shader =
      read_spirv("shaders/square.frag.spv", FRAGMENT);

  if (NULL == triangle_vertex_shader.buffer ||
      NULL == triangle_fragment_shader.buffer) {
    fprintf(stderr, "Failed to load compiled shaders\n");
    return -1;
  }

  // allocate room for one pipeline
  if (0 > allocate_pipelines(vk_ctx, indices, 2))
    return -1;

  {
    VkRenderPass pass = create_render_pass(vk_ctx, indices);

    if (VK_NULL_HANDLE == pass) {
#ifdef DEBUG
      fprintf(stderr, "Could not create render pass\n");
#endif
      return -1;
    }

    {
      if (0 > allocate_render_passes(vk_ctx, indices, 1))
        return -1;
    }

    add_render_pass(vk_ctx, indices, pass);

    {
      struct spirv_file spirvs[] = {triangle_vertex_shader,
                                    triangle_fragment_shader};

      struct shader_set shader_set = {0};

      indices->pipeline = 0;
      int success = create_graphics_pipeline(vk_ctx, indices, spirvs,
                                             sizeof(spirvs) / sizeof(*spirvs),
                                             &shader_set, 3);

      if (0 != success) {
#ifdef DEBUG
        fprintf(stderr, "Could not create graphics pipelines\n");
#endif
        return success;
      }
    }

    {
      struct spirv_file spirvs[] = {square_vertex_shader,
                                    square_fragment_shader};

      struct shader_set shader_set = {0};

      indices->pipeline = 1;
      int success = create_graphics_pipeline(vk_ctx, indices, spirvs,
                                             sizeof(spirvs) / sizeof(*spirvs),
                                             &shader_set, 4);

      if (0 != success) {
#ifdef DEBUG
        fprintf(stderr, "Could not create graphics pipelines\n");
#endif
        return success;
      }
    }

#ifdef DEBUG
    fprintf(stderr, "Successfully created graphics pipeline\n");
#endif
  }

  {
    int success = create_framebuffers(vk_ctx, indices);
    if (0 != success) {
#ifdef DEBUG
      fprintf(stderr, "Could not create framebuffers for swapchain\n");
#endif
      return success;
    }
  }

  {
    int success = create_command_pool(vk_ctx, indices);
    if (0 != success) {
#ifdef DEBUG
      fprintf(stderr, "Could not create command pool\n");
#endif
      return success;
    }
  }

  {
    int success = create_command_buffers(vk_ctx, indices, 1);
    if (0 != success) {
#ifdef DEBUG
      fprintf(stderr, "Could not create command buffer\n");
#endif
      return success;
    }
  }

#ifdef DEBUG
  fprintf(stderr, "Successfully created framebuffers and command buffers\n");
#endif

  return 0;
}

int main() {

  signal(SIGINT, sig_catcher);
  signal(SIGABRT, sig_catcher);
  signal(SIGKILL, sig_catcher);

  const char *val_layers[] = {"VK_LAYER_KHRONOS_validation"};
  const char *ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  vulkan_context vk_ctx = {0};

  init_vulkan_context(&vk_ctx);

  VkApplicationInfo app_info = {.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                .apiVersion = VK_API_VERSION_1_0,
                                .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                .pEngineName = "GOODGIRL",
                                .pApplicationName = "GG_ENDING_TEST",
                                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                .pNext = NULL};

  vk_ctx.app_info = app_info;

  vk_ctx.validation_layers = val_layers;
  vk_ctx.validation_layers_count = sizeof(val_layers) / sizeof(*val_layers);

  vk_ctx.lgpu_extensions = ext;
  vk_ctx.lgpu_extension_count = sizeof(ext) / sizeof(*ext);

  struct indices indices = {0};

  int setup_result = setup_vulkan(&w_ctx, &vk_ctx, &indices);
  if (0 != setup_result) {
    fprintf(stderr,
            "Errors occured while setting up Vulkan environment. Quitting.\n");
    exit(setup_result);
  }

#ifdef DEBUG
  fprintf(stderr, "Vulkan environment set up and ready to go\n");
#endif

  // sleep(1);

  uint8_t p_idx[] = {0};

  while (!glfwWindowShouldClose(w_ctx.glfw_window)) {
    glfwPollEvents();
    draw_frame(&w_ctx, &indices, p_idx, sizeof(p_idx) / sizeof(*p_idx));
  }

  destroy_vulkan_window(&w_ctx);

  return EXIT_SUCCESS;
}
