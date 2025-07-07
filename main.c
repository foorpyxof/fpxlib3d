#include "vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

#define LOGICAL_GPU_COUNT 1

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
  if (!dev_features.geometryShader)
    score = 0;

  return score * multiplier;
}

int main() {
  vulkan_context vk_ctx = {0};

  init_vulkan_context(&vk_ctx);

  window_context w_ctx = {.glfw_window = NULL,
                          .window_dimensions = {500, 500},
                          .window_title = "sugma balls",
                          .vk_context = &vk_ctx};

  {
    int success = create_vulkan_window(&w_ctx);
    if (VK_SUCCESS != success) {
      fprintf(stderr,
              "Error creating Vulkan instance, window or surface. Code: %d\n",
              success);
      exit(success);
    }

#ifdef DEBUG
    fprintf(stderr, "Successfully created Vulkan instance + window\n");
#endif
  }

  {
    int success = choose_vulkan_gpu(&vk_ctx, gpu_suitability);

    if (0 != success) {
      // failed
#ifdef DEBUG
      fprintf(stderr, "Could not find a suitable GPU to use.\n");
      exit(success);
#endif
    } else {
      VkPhysicalDeviceProperties prop;
      vkGetPhysicalDeviceProperties(vk_ctx.physical_gpu, &prop);
      fprintf(stderr, "Using Vulkan GPU \"%s\"\n", prop.deviceName);
    }
  }

  {
    vk_ctx.logical_gpus = (struct logical_gpu_info *)calloc(
        LOGICAL_GPU_COUNT, sizeof(struct logical_gpu_info));
    if (NULL == vk_ctx.logical_gpus) {
      perror("calloc()");
      exit(EXIT_FAILURE);
    }
    vk_ctx.lg_capacity = LOGICAL_GPU_COUNT;

    VkPhysicalDeviceFeatures features = {0};
    int lgpu_index = new_logical_vulkan_gpu(&vk_ctx, 1.0f, &features, 1, 1);

    if (0 > lgpu_index) {
      // failure
      fprintf(stderr, "Error while creating logical GPU. Error code: %d\n",
              lgpu_index);
      exit(lgpu_index);
    }
  }
}
