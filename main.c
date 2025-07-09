#include "vk.h"

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGICAL_GPU_COUNT 1

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

    sc_details = swapchain_compatibility(to_score, ctx->vk_surface, &sc_reqs);

    if (!(sc_details.swapchains_available && sc_details.surface_format_valid &&
          sc_details.present_mode_valid)) {
      score = 0;
      goto gpu_suitability_ret;
    }
  }

gpu_suitability_ret:
  return score * multiplier;
}

int setup_vulkan(window_context *w_ctx, vulkan_context *vk_ctx) {
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
    fprintf(stderr, "Successfully created Vulkan instance + window\n");
#endif
  }

  {
    int success = choose_vulkan_gpu(vk_ctx, gpu_suitability);

    if (0 != success) {
      // failed
      fprintf(stderr, "Could not find a suitable GPU to use.\n");
      return success;
    } else {
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
    lgpu_index = new_logical_vulkan_gpu(vk_ctx, 1.0f, &features, 1, 1);

    if (0 > lgpu_index) {
      // failure
      fprintf(stderr, "Error while creating logical GPU. Error code: %d\n",
              lgpu_index);
      return lgpu_index;
    }
  }

  struct logical_gpu_info *lgpu = &vk_ctx->logical_gpus[lgpu_index];

  for (int i = 0; i < lgpu->render_capacity; ++i) {
    if (0 > new_vulkan_queue(vk_ctx, lgpu, RENDER))
      return EXIT_FAILURE;
  }

  for (int i = 0; i < lgpu->presentation_capacity; ++i) {
    if (0 > new_vulkan_queue(vk_ctx, lgpu, PRESENTATION))
      return EXIT_FAILURE;
  }

  {
    struct swapchain_details details = swapchain_compatibility(
        vk_ctx->physical_gpu, vk_ctx->vk_surface, &sc_reqs);
    int success = create_vulkan_swap_chain(w_ctx, &details, lgpu_index);

    if (VK_SUCCESS == success) {
#ifdef DEBUG
      fprintf(stderr, "Swap chain created successfully.\n");
#endif
    }
  }

  return 0;
}

int main() {
  const char *val_layers[] = {"VK_LAYER_KHRONOS_validation"};
  const char *ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  vulkan_context vk_ctx = {0};

  init_vulkan_context(&vk_ctx);

  vk_ctx.validation_layers = val_layers;
  vk_ctx.validation_layers_count = sizeof(val_layers) / sizeof(*val_layers);

  vk_ctx.lgpu_extensions = ext;
  vk_ctx.lgpu_extension_count = sizeof(ext) / sizeof(*ext);

  window_context w_ctx = {.glfw_window = NULL,
                          .window_dimensions = {500, 500},
                          .window_title = "sugma balls"};

  int setup_result = setup_vulkan(&w_ctx, &vk_ctx);
  if (0 != setup_result) {
    fprintf(stderr,
            "Errors occured while setting up Vulkan environment. Quitting.\n");
    exit(setup_result);
  }

#ifdef DEBUG
  fprintf(stderr, "Vulkan environment set up successfully.\n");
#endif

  while (!glfwWindowShouldClose(w_ctx.glfw_window)) {
  }

  destroy_vulkan_window(&w_ctx);

  return EXIT_SUCCESS;
}
