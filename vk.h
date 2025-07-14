#ifndef FPX_VK_H
#define FPX_VK_H

#include <cglm/types.h>
#include <stdatomic.h>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <sys/types.h>

#define TRUE 1
#define FALSE 0

struct vertex {
  vec2 position;
  vec3 color;
};

struct vertex_attributes {
  size_t binding_count;
  VkVertexInputBindingDescription *bindings;

  size_t attribute_count;
  VkVertexInputAttributeDescription *attributes;
};

struct vertex_bundle {
  size_t vertex_count;
  size_t vertex_capacity;
  struct vertex *vertices;

  uint16_t *indices;
  size_t index_count;
};

struct shape_buffer {
  int vertex_count;
  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_buffer_memory;

  int index_count;
  VkBuffer index_buffer;
  VkDeviceMemory index_buffer_memory;
};

struct logical_gpu_info;

#define INDEX_TYPE uint8_t
struct indices {
  INDEX_TYPE logical_gpu;
  INDEX_TYPE pipeline;
  INDEX_TYPE swapchain_frame;
  INDEX_TYPE command_pool;
  INDEX_TYPE command_buffer;
  INDEX_TYPE render_pass;
  INDEX_TYPE render_queue;
  INDEX_TYPE present_queue;
  INDEX_TYPE transfer_queue;
};
#undef INDEX_TYPE

struct swapchain_frame {
  VkImage image;
  VkImageView view;
  VkFramebuffer framebuffer;

  VkSemaphore write_available;
  VkSemaphore render_finished;
};

struct active_swapchain {
  VkSwapchainKHR swapchain;
  VkExtent2D swapchain_extent;

  VkSemaphore aquire_semaphore;
  VkFence in_flight_fence;

  VkFormat image_format;
  struct swapchain_frame *frames;
  size_t frame_count;
};

struct swapchain_requirements {
  VkSurfaceCapabilitiesKHR surface_capabilities;

  VkSurfaceFormatKHR *surface_formats;
  size_t surface_formats_count;

  VkPresentModeKHR *present_modes;
  size_t present_modes_count;
};

typedef struct {
  VkPhysicalDevice physical_gpu;
  struct logical_gpu_info *logical_gpus;
  size_t lg_count, lg_capacity;

  const char **lgpu_extensions;
  size_t lgpu_extension_count;

  VkInstance vk_instance;
  VkSurfaceKHR vk_surface;

  struct swapchain_requirements swapchain_requirements;

  uint8_t teardown;

  VkApplicationInfo app_info;

  const char **validation_layers;
  size_t validation_layers_count;
} vulkan_context;

typedef struct {
  GLFWwindow *glfw_window;
  const char *window_title;
  uint16_t window_dimensions[2];

  uint8_t resized;

  vulkan_context *vk_context;
} window_context;

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
    TRANSFER_ONLY = 2,
  } type;
};

struct queue_info {
  VkQueue queue;
  VkQueueFlags queue_flags;
};

struct pipeline {
  VkPipeline pipeline;
  VkPipelineLayout layout;

  struct shape_buffer *shapes;
  size_t shape_count;
};

struct logical_gpu_info {
  VkDevice gpu;
  VkPhysicalDeviceFeatures features;

  struct active_swapchain current_swapchain;

  struct command_pool *command_pools;
  size_t command_pool_capacity;

  struct pipeline *pipelines;
  size_t pipeline_capacity;

  VkRenderPass *render_passes;
  size_t render_pass_capacity;

  VkQueue *render_queues;
  size_t render_count;
  size_t render_capacity;
  int render_qf;

  VkQueue *presentation_queues;
  size_t presentation_count;
  size_t presentation_capacity;
  int presentation_qf;

  VkQueue *transfer_queues;
  size_t transfer_count;
  size_t transfer_capacity;
  int transfer_qf;

  ssize_t render_transfer_overlap;
};

struct swapchain_details {
  uint8_t swapchains_available;

  VkSurfaceCapabilitiesKHR surface_capabilities;

  uint8_t surface_format_valid;
  uint8_t present_mode_valid;

  VkSurfaceFormatKHR surface_format;
  VkPresentModeKHR present_mode;
};

enum shader_stage {
  INVALID = 0,
  // INPUT = 1,
  VERTEX = 2,
  TESSELATION_CONTROL = 3,
  TESSELATION_EVALUATION = 4,
  GEOMETRY = 5,
  // RASTERIZATION = 6,
  FRAGMENT = 7,
  // BLENDING = 8,
};

struct spirv_file {
  uint8_t *buffer;
  size_t filesize;

  const char *version;

  enum shader_stage stage;

  enum source_language {
    UNKNOWN = 0,
    ESSL = 1,
    GLSL = 2,
    OPENCL_C = 3,
    OPENCL_CPP = 4,
    HLSL = 5,
    CPP_FOR_OPENCL = 6,
    SYCL = 7,
    HERO_C = 8,
    NZSL = 9,
    WGSL = 10,
    SLANG = 11,
    ZIG = 12,
    RUST = 13
  } source_language;
};

struct shader_set {
  // VkShaderModule input;
  VkShaderModule vertex;
  VkShaderModule tesselation_control;
  VkShaderModule tesselation_evaluation;
  VkShaderModule geometry;
  // VkShaderModule rasterization;
  VkShaderModule fragment;
  // VkShaderModule blending;
};

struct command_pool {
  enum command_pool_type {
    RENDER_POOL = 0,
    TRANSFER_POOL = 1,
  } type;

  VkCommandPool pool;

  VkCommandBuffer *buffers;
  size_t buffer_count;
};

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

struct queue_family_info
vulkan_queue_family(vulkan_context *, struct queue_family_requirements *);

// return -1: logical gpu count in vulkan_context
//  is greater than or equal to  logical gpu capacity
// return -2: error while querying queue families
// return -3: error while selecting a queue family
// return -4: error while allocating memory
// return -5: error while creating the logical gpu
int new_logical_vulkan_gpu(vulkan_context *, float priority,
                           VkPhysicalDeviceFeatures, int render_queues,
                           int presentation_queues, int transfer_queues);

/*
 * INDICES READ:
 * - logical_gpu
 */
int new_vulkan_queue(vulkan_context *, const struct indices *,
                     enum queue_family_type);

void save_swapchain_requirements(vulkan_context *,
                                 const struct swapchain_requirements *);

struct swapchain_details swapchain_compatibility(vulkan_context *,
                                                 VkPhysicalDevice);

/*
 * INDICES READ:
 * - logical_gpu
 */
int create_vulkan_swap_chain(window_context *, const struct swapchain_details *,
                             const struct indices *);

int refresh_vulkan_swap_chain(window_context *,
                              const struct swapchain_details *,
                              const struct indices *);

struct spirv_file read_spirv(const char *filename, enum shader_stage);

/*
 * INDICES READ:
 * - logical_gpu
 */
int fill_shader_module(vulkan_context *, const struct indices *,
                       const struct spirv_file, struct shader_set *);

/*
 * INDICES READ:
 * - logical_gpu
 */
VkRenderPass create_render_pass(vulkan_context *, const struct indices *);

/*
 * INDICES READ:
 * - logical_gpu
 */
int allocate_render_passes(vulkan_context *, const struct indices *,
                           size_t amount);

/*
 * INDICES READ:
 * - logical_gpu
 * - render_pass
 */
int add_render_pass(vulkan_context *, const struct indices *, VkRenderPass);

int init_vertices_struct(struct vertex_bundle *);

int allocate_vertices(struct vertex_bundle *, size_t);

void free_vertices(struct vertex_bundle *);

int append_vertex(struct vertex_bundle *, struct vertex);

void shape_buffer_init(struct shape_buffer *);

int shape_buffer_from_vertices(vulkan_context *, struct indices *,
                               struct shape_buffer *output_shape,
                               struct vertex_bundle *input_bundle);

void free_shape_buffer(vulkan_context *, struct indices *,
                       struct shape_buffer *shape);

/*
 * INDICES READ:
 * - logical_gpu
 */
// will realloc() if already has capacity > 0
int allocate_pipelines(vulkan_context *, const struct indices *, size_t amount);

/*
 * INDICES READ:
 * - logical_gpu
 * - pipeline
 */
int create_graphics_pipeline(vulkan_context *, const struct indices *,
                             const struct spirv_file *spirvs,
                             size_t spirv_count, struct shader_set *set,
                             struct vertex_attributes *attr,
                             struct shape_buffer *shapes, size_t shape_count);

/*
 * INDICES READ:
 * - logical_gpu
 * - render_pass
 */
int create_framebuffers(vulkan_context *, const struct indices *);

/*
 * INDICES READ:
 * - logical_gpu
 */
int allocate_command_pools(vulkan_context *, const struct indices *,
                           size_t amount);

/*
 * INDICES READ:
 * - logical_gpu
 */
int create_command_pool(vulkan_context *, const struct indices *,
                        enum command_pool_type);

/*
 * INDICES READ:
 * - logical_gpu
 */
int create_command_buffers(vulkan_context *, const struct indices *,
                           size_t amount);

/*
 * INDICES READ:
 * - logical_gpu
 * - command_buffer
 * - render_pass
 * - swapchain_frame
 */
int record_command_buffer(vulkan_context *, const struct indices *,
                          size_t pipeline_index);

/*
 * INDICES READ:
 * - logical_gpu
 * - command_buffer
 * - swapchain_frame
 * - render_queue
 */
int submit_command_buffer(vulkan_context *, const struct indices *);

/*
 * INDICES READ:
 * - logical_gpu
 */
int present_swap_frame(vulkan_context *, const struct indices *);

/*
 * INDICES READ:
 * - logical_gpu
 * - command_buffer
 * - render_pass
 * - render_queue
 * - present_queue
 */
// this function calls:
//  record_command_buffer()
//  submit_command_buffer()
//  present_swap_frame()
// to draw to the screen
void draw_frame(window_context *, struct indices *, uint8_t *pipeline_indices,
                size_t pipeline_count);

#endif
