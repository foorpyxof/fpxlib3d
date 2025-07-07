#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define TRUE 1
#define FALSE 0

struct logical_gpu_info;

typedef struct {
  VkPhysicalDevice physical_gpu;
  struct logical_gpu_info *logical_gpus;
  size_t lg_count, lg_capacity;

  VkInstance vk_instance;
  VkSurfaceKHR vk_surface;

  int *unique_qf_list;

  VkApplicationInfo app_info;

  const char **validation_layers;
  size_t validation_layers_count;
} vulkan_context;

typedef struct {
  GLFWwindow *glfw_window;
  const char *window_title;
  uint16_t window_dimensions[2];

  vulkan_context *vk_context;
} window_context;

// returns 0 on success
VkResult init_vulkan_context(vulkan_context *);

uint8_t validation_layers_supported(const char **layers, size_t layer_count);

// returns either a VkResult or a separate error code.
// if the error code is a valid VkResult, assume so.
// otherwise it is separate
VkResult create_vulkan_window(window_context *);

// puts the most suitable Vulkan GPU into the passed vulkan_context struct.
// the scoring function must return a score that is higher for a more suitable
// GPU.
// return 0: success
// return -1: no vulkan GPU's. return -2: memory allocation failed.
int choose_vulkan_gpu(vulkan_context *,
                      int (*scoring_function)(vulkan_context *,
                                              VkPhysicalDevice));

struct queue_family_info {
  int64_t index;
  VkQueueFamilyProperties family;
};

struct queue_family_requirements {
  union req_union {
    struct {
      uint32_t qf_flags;
    } render;

    struct {
      VkSurfaceKHR surface;
      VkPhysicalDevice gpu;
    } present;
  } reqs;

  int minimum_queues;

  enum queue_family_type {
    RENDER = 0,
    PRESENTATION = 1,
  } type;
};

struct queue_family_info
vulkan_queue_family(vulkan_context *, struct queue_family_requirements *);

struct queue_info {
  VkQueue queue;
  VkQueueFlags queue_flags;
};

struct logical_gpu_info {
  VkDevice gpu;

  struct queue_info *render_queues;
  size_t render_count;

  struct queue_info *presentation_queues;
  size_t presentation_count;
};

// return -1: error while querying queue families
// return -2: memory allocation error
// return -3: error while creating logical device
int new_logical_vulkan_gpu(vulkan_context *, float priority,
                           VkPhysicalDeviceFeatures *, int render_queues,
                           int presentation_queues);
