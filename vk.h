#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define TRUE 1
#define FALSE 0

struct logical_gpu_info;

struct active_swapchain {
  VkSwapchainKHR swapchain;
  VkExtent2D swapchain_extent;

  VkSemaphore image_available;
  VkSemaphore render_finished;
  VkFence in_flight;

  VkImage *images;
  size_t image_count;
  VkFormat image_format;

  VkImageView *views;
  size_t view_count;

  VkFramebuffer *framebuffers;
  size_t framebuffer_count;
};

typedef struct {
  VkPhysicalDevice physical_gpu;
  struct logical_gpu_info *logical_gpus;
  size_t lg_count, lg_capacity;

  const char **lgpu_extensions;
  size_t lgpu_extension_count;

  VkInstance vk_instance;
  VkSurfaceKHR vk_surface;

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
uint8_t device_extensions_supported(VkPhysicalDevice, const char **extensions,
                                    size_t extension_count);

// returns either a VkResult or a separate error code.
// if the error code is a valid VkResult, assume so.
// otherwise it is separate
VkResult create_vulkan_window(window_context *);

VkResult destroy_vulkan_window(window_context *);

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
  uint8_t is_valid;
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

struct pipeline {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  VkRenderPass render_pass;
};

struct logical_gpu_info {
  VkDevice gpu;
  VkPhysicalDeviceFeatures features;

  struct active_swapchain current_swapchain;

  VkCommandPool command_pool;
  VkCommandBuffer *command_buffers;
  size_t command_buffer_count;

  struct pipeline *pipelines;
  size_t pipeline_capacity;

  VkQueue *render_queues;
  size_t render_count;
  size_t render_capacity;
  int render_qf;

  VkQueue *presentation_queues;
  size_t presentation_count;
  size_t presentation_capacity;
  int presentation_qf;
};

// return -1: logical gpu count in vulkan_context
//  is greater than or equal to  logical gpu capacity
// return -2: error while querying queue families
// return -3: error while selecting a queue family
// return -4: error while allocating memory
// return -5: error while creating the logical gpu
int new_logical_vulkan_gpu(vulkan_context *, float priority,
                           VkPhysicalDeviceFeatures, int render_queues,
                           int presentation_queues);

int new_vulkan_queue(vulkan_context *, struct logical_gpu_info *,
                     enum queue_family_type);

struct swapchain_requirements {
  VkSurfaceCapabilitiesKHR surface_capabilities;

  VkSurfaceFormatKHR *surface_formats;
  size_t surface_formats_count;

  VkPresentModeKHR *present_modes;
  size_t present_modes_count;
};

struct swapchain_details {
  uint8_t swapchains_available;

  VkSurfaceCapabilitiesKHR surface_capabilities;

  uint8_t surface_format_valid;
  uint8_t present_mode_valid;

  VkSurfaceFormatKHR surface_format;
  VkPresentModeKHR present_mode;
};

struct swapchain_details
swapchain_compatibility(VkPhysicalDevice, VkSurfaceKHR,
                        const struct swapchain_requirements *);

int create_vulkan_swap_chain(window_context *, const struct swapchain_details *,
                             size_t logical_gpu_index);

enum shader_stage {
  INVALID = 0,
  // INPUT = 1,
  VERTEX = 2,
  TESSELATION = 3,
  GEOMETRY = 4,
  // RASTERIZATION = 5,
  FRAGMENT = 6,
  // BLENDING = 7,
};

struct spirv_file {
  uint8_t *buffer;
  size_t filesize;

  enum shader_stage stage;
};

struct spirv_file read_spirv(const char *filename, enum shader_stage);

struct shader_set {
  // VkShaderModule input;
  VkShaderModule vertex;
  VkShaderModule tesselation;
  VkShaderModule geometry;
  // VkShaderModule rasterization;
  VkShaderModule fragment;
  // VkShaderModule blending;
};

int fill_shader_module(const struct logical_gpu_info *lgpu,
                       const struct spirv_file, struct shader_set *);

VkRenderPass create_render_pass(vulkan_context *, size_t lgpu_index);

int set_render_pass(vulkan_context *, size_t lgpu_index, size_t pipeline_idx,
                    VkRenderPass);

// will realloc() if already has capacity > 0
int allocate_pipelines(vulkan_context *, size_t lgpu_idx, size_t amount);

int create_graphics_pipeline(vulkan_context *, size_t lgpu_index,
                             size_t pipeline_index,
                             const struct spirv_file *spirvs,
                             size_t spirv_count, struct shader_set *);

int create_framebuffers(vulkan_context *, size_t lgpu_index,
                        size_t pipeline_index);

int create_command_pool(vulkan_context *, size_t lgpu_index);

int create_command_buffers(vulkan_context *, size_t lgpu_index, size_t amount);

int apply_command_buffer(vulkan_context *, size_t lgpu_index,
                         size_t pipeline_index, size_t buffer_index,
                         size_t image_index);

void draw_frame(vulkan_context *, size_t lgpu_index, size_t pipeline_index,
                size_t command_buffer_index, size_t render_queue_index,
                size_t present_queue_index);
