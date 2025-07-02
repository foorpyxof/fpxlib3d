#include <cglm/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cglm/mat4.h>
#include <cglm/vec4.h>

#define WINDOW_WIDTH 500
#define WINDOW_HEIGHT 500

#define TRUE 1
#define FALSE 0

GLFWwindow *window = NULL;
VkInstance instance = NULL;
VkPhysicalDevice gpu = VK_NULL_HANDLE;
VkDevice log_gpu = VK_NULL_HANDLE;

int create_instance() {
  VkApplicationInfo appInfo = {0};
  VkInstanceCreateInfo createInfo = {0};

  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "sugma", NULL, NULL);

  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "BALLS N COCK";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "BALLS ENGINE";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  uint32_t glfw_extension_count = 0;
  const char **glfw_extensions = NULL;

  glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
  fprintf(stderr, "%u extensions supported\n", glfw_extension_count);

  createInfo.enabledExtensionCount = glfw_extension_count;
  createInfo.ppEnabledExtensionNames = glfw_extensions;

  createInfo.enabledLayerCount = 0;

  VkResult res = vkCreateInstance(&createInfo, NULL, &instance);

  return res;
}

void destroy_instance() {
  vkDestroyInstance(instance, NULL);
  vkDestroyDevice(log_gpu, NULL);

  glfwDestroyWindow(window);
  glfwTerminate();
}

struct scored_gpu {
  VkPhysicalDevice gpu;
  uint16_t score;
};

uint16_t gpu_suitability(VkPhysicalDevice dev) {
  uint16_t score = 200;
  uint8_t mul = 1;

  VkPhysicalDeviceProperties dev_properties;
  VkPhysicalDeviceFeatures dev_features;
  vkGetPhysicalDeviceProperties(dev, &dev_properties);
  vkGetPhysicalDeviceFeatures(dev, &dev_features);

  fprintf(stderr, "Found Vulkan compatible GPU \"%s\"\n",
          dev_properties.deviceName);

  // score multiplier, to favour discrete GPU's
  if (dev_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    mul = 2;

  // must-have features
  if (!dev_features.geometryShader)
    score = 0;

  return score * mul;
}

// takes as an argument: function that takes
// a VkPhysicalDevices, and rates it into a
// score that is then returned
int pick_gpu(uint16_t (*scoring_function)(VkPhysicalDevice)) {
  int retval = 0;
  VkPhysicalDevice *devices = NULL;
  struct scored_gpu *scored_devices = NULL;

  // get GPU count
  uint32_t device_count = 0;
  int success = vkEnumeratePhysicalDevices(instance, &device_count, NULL);
  if (0 == device_count) {
    fprintf(stderr, "No Vulkan capable GPU's found!\n");
    retval = -1;
    goto pick_gpu_ret;
  }

  // make array to store GPU handles in
  devices = (VkPhysicalDevice *)calloc(device_count, sizeof(VkPhysicalDevice));
  if (NULL == devices) {
    perror("calloc()");
    retval = -2;
    goto pick_gpu_ret;
  }

  scored_devices =
      (struct scored_gpu *)calloc(device_count, sizeof(struct scored_gpu));
  if (NULL == devices) {
    perror("calloc()");
    retval = -2;
    goto pick_gpu_ret;
  }

  // put devices into array
  vkEnumeratePhysicalDevices(instance, &device_count, devices);

  VkPhysicalDevice most_suitable = VK_NULL_HANDLE;
  int most_suitable_score = 0;

  for (int i = 0; i < device_count; ++i) {
    int score = scoring_function(devices[i]);

    for (int j = i; j >= 0; --j) {
      if (scored_devices[j].score < score && 0 < i)
        memcpy(&scored_devices[j], &scored_devices[j + 1],
               sizeof(*scored_devices));

      if (0 == j || scored_devices[j - 1].score >= score) {
        scored_devices[j].gpu = devices[i];
        scored_devices[j].score = score;
      }
    }
  }

  // take the highest scoring GPU and set it as the active card
  gpu = scored_devices[0].gpu;

  if (VK_NULL_HANDLE == gpu) {
    // no suitable GPU's
    fprintf(stderr, "No suitable GPU's were found!\n");
    retval = -3;
    goto pick_gpu_ret;
  }

pick_gpu_ret:
  if (NULL != devices)
    free(devices);
  if (NULL != scored_devices)
    free(scored_devices);

  return retval;
}

int main() {

  mat4 matrix;
  vec4 vec;

  vec4 test;
  glm_mat4_mulv(matrix, vec, test);

  {
    int success = create_instance();
    if (VK_SUCCESS != success) {
      fprintf(stderr, "Error while creating Vulkan instance. Code: %d\n",
              success);
      exit(success);
    }
  }

  {
    int success = pick_gpu(gpu_suitability);
    switch (success) {
    case -1:
      // no Vulkan GPU's
    case -2:
      // failed to allocate array for physical devices
    }
  }

  fprintf(stderr, "Vulkan surface created successfully.\n");

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
  }

  destroy_instance();

  return 0;
}
