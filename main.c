#include <cglm/types.h>
#include <stddef.h>
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

const char *validation_layers[] = {"VK_LAYER_KHRONOS_validation"};

#ifdef DEBUG
#define FPX_VK_USE_VALIDATION_LAYERS 1
#else
#define FPX_VK_USE_VALIDATION_LAYERS 0
#endif

uint8_t validation_layers_supported(const char **requested_layers,
                                    size_t requested_layers_count) {
  uint32_t available_layers_count = 0;
  VkLayerProperties *available_layers = NULL;

  vkEnumerateInstanceLayerProperties(&available_layers_count, NULL);
  if (available_layers_count < 1)
    return FALSE;

  available_layers = (VkLayerProperties *)calloc(available_layers_count,
                                                 sizeof(VkLayerProperties));

  if (NULL == available_layers) {
    perror("calloc()");
    fprintf(stderr, "Error while checking for Vulkan validation layers.\n");
    return FALSE;
  }

  vkEnumerateInstanceLayerProperties(&available_layers_count, available_layers);

  for (int i = 0; i < requested_layers_count; ++i) {
    uint8_t found = FALSE;

    for (int j = 0; j < available_layers_count; ++j) {
      if (0 == strcmp(requested_layers[i], available_layers[j].layerName)) {
        found = TRUE;
        break;
      }
    }

    if (FALSE == found)
      return FALSE;
  }

  return TRUE;
}

struct vulkan_instance_details {
  GLFWwindow *window;
  VkInstance vk_instance;
  VkPhysicalDevice physical_gpu;
  VkDevice *logical_gpus;
  size_t logical_gpus_count;
  int qf_index;
};

int create_instance(struct vulkan_instance_details *vk_details) {
  if (FPX_VK_USE_VALIDATION_LAYERS &&
      !validation_layers_supported(
          validation_layers,
          (sizeof(validation_layers) / sizeof(*validation_layers)))) {
#ifdef DEBUG
    fprintf(stderr,
            "One or more requested instance layers were not available\n");
#endif
    return VK_ERROR_LAYER_NOT_PRESENT;
  }

  VkApplicationInfo appInfo = {0};
  VkInstanceCreateInfo createInfo = {0};

  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  vk_details->window =
      glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "sugma", NULL, NULL);

  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "BALLS N COCK";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "BALLS ENGINE";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

#ifdef FPX_VK_USE_VALIDATION_LAYERS
  createInfo.enabledLayerCount =
      sizeof(validation_layers) / sizeof(*validation_layers);
  createInfo.ppEnabledLayerNames = validation_layers;
#else
  createInfo.enabledLayerCount = 0;
#endif

  uint32_t glfw_extension_count = 0;
  const char **glfw_extensions = NULL;

  glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

#ifdef DEBUG
  fprintf(stderr, "%u extensions supported\n", glfw_extension_count);
#endif

  createInfo.enabledExtensionCount = glfw_extension_count;
  createInfo.ppEnabledExtensionNames = glfw_extensions;

  createInfo.enabledLayerCount = 0;

  VkResult res = vkCreateInstance(&createInfo, NULL, &vk_details->vk_instance);

  return res;
}

void destroy_instance(struct vulkan_instance_details *vk_details) {
  vkDestroyInstance(vk_details->vk_instance, NULL);
  for (int i = 0; i < vk_details->logical_gpus_count; ++i) {
    vkDestroyDevice(vk_details->logical_gpus[i], NULL);
  }

  glfwDestroyWindow(vk_details->window);
  glfwTerminate();

  if (NULL != vk_details->logical_gpus)
    free(vk_details->logical_gpus);

  VkPhysicalDevice dev = vk_details->physical_gpu;

  memset(vk_details, 0, sizeof(*vk_details));

  vk_details->physical_gpu = dev;
}

struct scored_gpu {
  VkPhysicalDevice gpu;
  uint16_t score;
};

struct scored_family {
  int family_index;
  uint8_t has_extra_features;
};

int queue_family(VkPhysicalDevice dev, VkQueueFlags required_flags,
                 VkQueueFlags wanted_flags) {
  int retval = 0;

  VkQueueFamilyProperties *families = NULL;
  struct scored_family *scored_families = NULL;
  uint32_t family_count = 0;

  vkGetPhysicalDeviceQueueFamilyProperties(dev, &family_count, NULL);

  if (family_count < 1) {
    // error
    retval = -1;
    goto queue_family_ret;
  }

  families = (VkQueueFamilyProperties *)calloc(family_count,
                                               sizeof(VkQueueFamilyProperties));
  if (NULL == families) {
    perror("calloc()");
    retval = -2;
    goto queue_family_ret;
  }

  scored_families = (struct scored_family *)calloc(
      family_count, sizeof(struct scored_family));
  if (NULL == scored_families) {
    perror("calloc()");
    retval = -2;
    goto queue_family_ret;
  }

  vkGetPhysicalDeviceQueueFamilyProperties(dev, &family_count, families);

#ifdef DEBUG
  fprintf(stderr, "Found %u queue families\n", family_count);
#endif

  int scored_list_index = 0;
  uint8_t found_atleast_one = FALSE;
  for (int i = 0; i < family_count; ++i) {
    if (required_flags != (families[i].queueFlags & required_flags))
      continue;

    scored_families[scored_list_index].family_index = i;
    found_atleast_one = TRUE;

    if (0 != (families[i].queueFlags & wanted_flags)) {
      // there are flags that we would like to use
      // so we return this index
      retval = i;
      goto queue_family_ret;
    }

    ++scored_list_index;
  }

  // if we got here, that means that we did not find
  // a queue family with the extra flags, so what we
  // do now is just taking the first queue family in
  // the 'scored_families' array
  if (FALSE == found_atleast_one) {
    // no queue family matching the required flags
    // was found....... return -3
    retval = -3;
  } else {
    retval = scored_families[0].family_index;
#ifdef DEBUG
    fprintf(stderr, "Selecting queue family %d\n", retval);
#endif
  }
  goto queue_family_ret;

queue_family_ret:
  if (NULL != families)
    free(families);
  if (NULL != scored_families)
    free(scored_families);

  return retval;
}

uint16_t gpu_suitability(VkPhysicalDevice dev) {
  uint16_t score = 200;
  uint8_t mul = 1;

  VkPhysicalDeviceProperties dev_properties;
  VkPhysicalDeviceFeatures dev_features;
  vkGetPhysicalDeviceProperties(dev, &dev_properties);
  vkGetPhysicalDeviceFeatures(dev, &dev_features);

  // score multiplier, to favour discrete GPU's
  if (dev_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    mul = 2;

  // must-have features
  if (!dev_features.geometryShader)
    score = 0;

  score *= mul;

#ifdef DEBUG
  fprintf(stderr,
          "Found Vulkan compatible GPU \"%s\" (suitability score: %hu)\n",
          dev_properties.deviceName, score);
#endif

  return score;
}

// takes as an argument: function that takes
// a VkPhysicalDevices, and rates it into a
// score that is then returned
// returns: -1, -2, -3 on error
int pick_gpu(struct vulkan_instance_details *vk_details,
             uint16_t (*scoring_function)(VkPhysicalDevice)) {
  int retval = 0;
  VkPhysicalDevice *devices = NULL;
  struct scored_gpu *scored_devices = NULL;

  // get GPU count
  uint32_t device_count = 0;
  int success =
      vkEnumeratePhysicalDevices(vk_details->vk_instance, &device_count, NULL);
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
  vkEnumeratePhysicalDevices(vk_details->vk_instance, &device_count, devices);

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

  // pick GPU, also checking for if the proper
  // queue families are available
  for (int i = 0; i < device_count && scored_devices[i].score > 0; ++i) {
    int queue_family_index =
        queue_family(scored_devices[i].gpu, VK_QUEUE_GRAPHICS_BIT, 0);

    if (-1 < queue_family_index) {
      vk_details->physical_gpu = scored_devices[i].gpu;
      vk_details->qf_index = queue_family_index;
      break;
    }
  }

  if (VK_NULL_HANDLE == vk_details->physical_gpu) {
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

int new_logical_gpu(struct vulkan_instance_details *vk_details,
                    VkPhysicalDeviceFeatures *features) {
  VkDeviceQueueCreateInfo q_info;
  float prio = 1.0f;

  q_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  q_info.queueFamilyIndex = vk_details->qf_index;
  q_info.queueCount = 1;
  q_info.pQueuePriorities = &prio;

  VkDeviceCreateInfo d_info;

  d_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  d_info.pQueueCreateInfos = &q_info;
  d_info.queueCreateInfoCount = 1;
  d_info.pEnabledFeatures = features;

// no longer required, but used for
// compatibility with older impl. of Vulkan
#ifdef FPX_VK_USE_VALIDATION_LAYERS
  d_info.enabledLayerCount =
      sizeof(validation_layers) / sizeof(*validation_layers);
  d_info.ppEnabledLayerNames = validation_layers;
#else
  d_info.enabledLayerCount = 0;
#endif

  // TODO: allocate vk_details->logical_gpus

  VkResult res =
      vkCreateDevice(vk_details->physical_gpu, &d_info, NULL,
                     &vk_details->logical_gpus[vk_details->logical_gpus_count]);
  return res;
}

int main() {
  struct vulkan_instance_details vk_details = {.window = NULL,
                                               .vk_instance = NULL,
                                               .physical_gpu = VK_NULL_HANDLE,
                                               .logical_gpus = NULL,
                                               .logical_gpus_count = 0,
                                               .qf_index = -1};

  mat4 matrix;
  vec4 vec;

  vec4 test;
  glm_mat4_mulv(matrix, vec, test);

  {
    int success = create_instance(&vk_details);
    if (VK_SUCCESS != success) {
      fprintf(stderr, "Error while creating Vulkan instance. Code: %d\n",
              success);
      exit(success);
    }
  }

  {
    int success = pick_gpu(&vk_details, gpu_suitability);
    switch (success) {
    case -1:
      // no Vulkan GPU's
    case -2:
      // failed to allocate array for physical devices
    case -3:
      // no suitable GPU's
      exit(EXIT_FAILURE);
      break;
    default:
      // everything is okay; we found a suitable GPU
      {
        // print some properties of the picked GPU:
        // name e.g.
        VkPhysicalDeviceProperties prop;
        vkGetPhysicalDeviceProperties(vk_details.physical_gpu, &prop);
        fprintf(stderr, "Using GPU \"%s\"\n", prop.deviceName);
      }
      break;
    }
  }

#ifdef DEBUG
  fprintf(stderr, "Vulkan surface created successfully.\n");
#endif

  {
    VkPhysicalDeviceFeatures features;
    int success = new_logical_gpu(&vk_details, &features);
    if (VK_SUCCESS != success) {
      fprintf(stderr,
              "Error while creating logical graphics device. Code: %d\n",
              success);
      exit(success);
    }
  }

  VkQueue queue;
  // vkGetDeviceQueue(vk_details.logical_gpus[0], vk_details.qf_index, 0,
  // &queue);

  while (!glfwWindowShouldClose(vk_details.window)) {
    glfwPollEvents();
  }

  destroy_instance(&vk_details);

  return 0;
}
