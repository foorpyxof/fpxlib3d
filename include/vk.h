/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_H
#define FPX_VK_H

#include <cglm/types.h>
#include <sys/types.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include "fpx3d.h"
#include "window.h"

#define TRUE 1
#define FALSE 0

//
//  START OF MACROS
//

#define ABS(x) ((x < 0) ? (x * -1) : (x))
#define MAX(x, y) ((x > y) ? x : y)
#define MIN(x, y) ((x < y) ? x : y)
#define CLAMP(v, x, y) ((v < x) ? x : (v > y) ? y : v)
#define CONDITIONAL(cond, then, else) ((cond) ? (then) : (else))

#define FPX3D_ON_NOT_SUCCESS(result, to_execute)                               \
  {                                                                            \
    if (FPX3D_SUCCESS != result) {                                             \
      to_execute;                                                              \
    }                                                                          \
  }

#define FPX3D_RETURN_ON_ERROR(call) FPX3D_ON_NOT_SUCCESS(call, return -1)

//
//  END OF MACROS
//

//
//  START OF TYPEDEFS
//

typedef struct _fpx3d_vk_sc Fpx3d_Vk_Swapchain;
typedef struct _fpx3d_vk_sc_frame Fpx3d_Vk_SwapchainFrame;
typedef struct _fpx3d_vk_sc_req Fpx3d_Vk_SwapchainRequirements;
typedef struct _fpx3d_vk_sc_prop Fpx3d_Vk_SwapchainProperties;

typedef enum {
  GRAPHICS_QUEUE = 0,
  PRESENT_QUEUE = 1,
  TRANSFER_QUEUE = 2, // transfer ONLY! no graphics
} Fpx3d_Vk_E_QueueType;
typedef struct _fpx3d_vk_qf Fpx3d_Vk_QueueFamily;
typedef struct _fpx3d_vk_qf_req Fpx3d_Vk_QueueFamilyRequirements;

typedef struct _fpx3d_vk_vertex Fpx3d_Vk_Vertex;
typedef struct _fpx3d_vk_vertex_bundle Fpx3d_Vk_VertexBundle;
typedef struct _fpx3d_vk_vertex_binding Fpx3d_Vk_VertexBinding;
typedef struct _fpx3d_vk_vertex_attr Fpx3d_Vk_VertexAttribute;

typedef struct _fpx3d_vk_shapebuffer Fpx3d_Vk_ShapeBuffer;

typedef enum {
  GRAPHICS_PIPELINE = 0,
  COMPUTE_PIPELINE = 1,
} Fpx3d_Vk_E_PipelineType;
typedef struct _fpx3d_vk_pipeline Fpx3d_Vk_Pipeline;

typedef enum {
  SHADER_STAGE_INVALID = 0,
  SHADER_STAGE_VERTEX = 2,
  SHADER_STAGE_TESSELATION_CONTROL = 3,
  SHADER_STAGE_TESSELATION_EVALUATION = 4,
  SHADER_STAGE_GEOMETRY = 5,
  SHADER_STAGE_FRAGMENT = 7,
} Fpx3d_Vk_E_ShaderStage;
typedef struct _fpx3d_vk_spirv Fpx3d_Vk_SpirvFile;
typedef struct _fpx3d_vk_shader_modules Fpx3d_Vk_ShaderModuleSet;

typedef enum {
  GRAPHICS_POOL = 0,
  TRANSFER_POOL = 1,
} Fpx3d_Vk_E_CommandPoolType;
typedef struct _fpx3d_vk_command_pool Fpx3d_Vk_CommandPool;

typedef struct _fpx3d_vk_lgpu Fpx3d_Vk_LogicalGpu;

typedef struct _fpx3d_vk_context Fpx3d_Vk_Context;

//
//  END OF TYPEDEFS
//

struct _fpx3d_vk_sc_frame {
  VkImage image;
  VkImageView view;
  VkFramebuffer framebuffer;

  VkSemaphore writeAvailable;
  VkSemaphore renderFinished;

  VkFence idleFence;
};

struct _fpx3d_vk_sc_prop {
  VkSurfaceCapabilitiesKHR surfaceCapabilities;

  bool surfaceFormatValid;
  bool presentModeValid;

  VkSurfaceFormatKHR surfaceFormat;
  VkPresentModeKHR presentMode;
};

struct _fpx3d_vk_sc {
  VkSwapchainKHR swapchain;
  VkExtent2D swapchainExtent;

  Fpx3d_Vk_SwapchainProperties properties;

  VkSemaphore acquireSemaphore;

  VkRenderPass *renderPassReference;

  VkFormat imageFormat;
  Fpx3d_Vk_SwapchainFrame *frames;
  size_t frameCount;

  Fpx3d_Vk_Swapchain *nextInList;
};

// TODO: rethink(?)
struct _fpx3d_vk_sc_req {
  VkSurfaceCapabilitiesKHR surfaceCapabilities;

  VkSurfaceFormatKHR *surfaceFormats;
  size_t surfaceFormatsCount;

  VkPresentModeKHR *presentModes;
  size_t presentModesCount;
};

Fpx3d_E_Result
fpx3d_vk_set_required_surfaceformats(Fpx3d_Vk_SwapchainRequirements *,
                                     VkSurfaceFormatKHR *formats, size_t count);

Fpx3d_E_Result
fpx3d_vk_set_required_presentmodes(Fpx3d_Vk_SwapchainRequirements *,
                                   VkPresentModeKHR *modes, size_t count);

struct _fpx3d_vk_qf {
  ssize_t qfIndex;
  size_t firstQueueIndex;
  VkQueueFamilyProperties properties;
  Fpx3d_Vk_E_QueueType type;
  bool isValid;
};

struct _fpx3d_vk_qf_req {
  union {
    struct {
      uint32_t requiredFlags;
    } graphics;

    struct {
      VkSurfaceKHR surface;
      VkPhysicalDevice gpu;
    } present;
  };

  size_t minimumQueues;

  // supports up to 64 qf indices. should be fine
  uint64_t indexBlacklistBits;

  Fpx3d_Vk_E_QueueType type;
};

// index <= 63
Fpx3d_E_Result
fpx3d_vk_blacklist_queuefamily_index(Fpx3d_Vk_QueueFamilyRequirements *,
                                     size_t index);

struct _fpx3d_vk_vertex {
  vec2 position;
  vec3 color;
};

Fpx3d_E_Result fpx3d_set_vertex_position(Fpx3d_Vk_Vertex *, vec2 pos);
Fpx3d_E_Result fpx3d_set_vertex_color(Fpx3d_Vk_Vertex *, vec3 color);

struct _fpx3d_vk_vertex_bundle {
  // size of the Vertex data in bytes (for a single vertex).
  // If you use a struct, just do sizeof([your struct])
  size_t vertexDataSize;

  void *vertices;
  size_t vertexCount;
  size_t vertexCapacity;

  uint32_t *indices;
  size_t indexCount; // if 0, use vertices as is
};

// will *only* zero-initialize if this is an initial allocation,
// not when reallocating using this function
Fpx3d_E_Result fpx3d_vk_allocate_vertices(Fpx3d_Vk_VertexBundle *,
                                          size_t amount,
                                          size_t single_vertex_size);
Fpx3d_E_Result fpx3d_vk_append_vertices(Fpx3d_Vk_VertexBundle *,
                                        Fpx3d_Vk_Vertex *vertices,
                                        size_t amount);
Fpx3d_E_Result fpx3d_vk_set_indices(Fpx3d_Vk_VertexBundle *, uint32_t *indices,
                                    size_t amount);

// also frees indices, if these were allocated
Fpx3d_E_Result fpx3d_vk_free_vertices(Fpx3d_Vk_VertexBundle *);

// these will be used in pipeline creation
struct _fpx3d_vk_vertex_attr {
  enum {
    FPX3D_VK_FORMAT_INVALID = 0,

    VEC2_16BIT_SFLOAT = 1,
    VEC3_16BIT_SFLOAT = 2,
    VEC4_16BIT_SFLOAT = 3,

    VEC2_32BIT_SFLOAT = 4,
    VEC3_32BIT_SFLOAT = 5,
    VEC4_32BIT_SFLOAT = 6,

    VEC2_64BIT_SFLOAT = 7,
    VEC3_64BIT_SFLOAT = 8,
    VEC4_64BIT_SFLOAT = 9,

    FPX3D_VK_FORMAT_MAXVALUE,
  } format;

  size_t dataOffsetBytes;
};

struct _fpx3d_vk_vertex_binding {
  Fpx3d_Vk_VertexAttribute *attributes;
  size_t attributeCount;

  size_t sizePerVertex;
};

struct fpx3d_vulkan_buffer {
  size_t objectCount;
  size_t stride;

  VkBuffer buffer;
  VkDeviceMemory memory;

  void *mapped_memory;

  VkSharingMode sharingMode;

  bool isValid;
};

struct _fpx3d_vk_shapebuffer {
  struct fpx3d_vulkan_buffer vertexBuffer;

  // if `isValid` bool is set to `false`, we assume we want to use the vertices
  // as-is, instead of ordering them using an index buffer
  struct fpx3d_vulkan_buffer indexBuffer;
}; // added to the Pipeline struct after that Pipeline has
   // already been created

Fpx3d_E_Result fpx3d_vk_create_shapebuffer(Fpx3d_Vk_Context *,
                                           Fpx3d_Vk_LogicalGpu *,
                                           Fpx3d_Vk_VertexBundle *,
                                           Fpx3d_Vk_ShapeBuffer *output);

Fpx3d_E_Result fpx3d_vk_free_shapebuffer(Fpx3d_Vk_LogicalGpu *,
                                         Fpx3d_Vk_ShapeBuffer *);

Fpx3d_E_Result fpx3d_vk_add_shapes_to_pipeline(Fpx3d_Vk_ShapeBuffer *,
                                               size_t count,
                                               Fpx3d_Vk_Pipeline *pipeline);

struct _fpx3d_vk_pipeline {
  VkPipeline pipeline;
  VkPipelineLayout layout;

  Fpx3d_Vk_E_PipelineType type;

  union {
    struct {
      Fpx3d_Vk_ShapeBuffer *shapes;
      size_t shapeCount;
    } graphics;
  };
};

struct _fpx3d_vk_spirv {
  uint8_t *buffer;
  size_t filesize;

  // const char *version;

  Fpx3d_Vk_E_ShaderStage stage;

  // enum {
  //   UNKNOWN = 0,
  //   ESSL = 1,
  //   GLSL = 2,
  //   OPENCL_C = 3,
  //   OPENCL_CPP = 4,
  //   HLSL = 5,
  //   CPP_FOR_OPENCL = 6,
  //   SYCL = 7,
  //   HERO_C = 8,
  //   NZSL = 9,
  //   WGSL = 10,
  //   SLANG = 11,
  //   ZIG = 12,
  //   RUST = 13
  // } source_language;
};

Fpx3d_Vk_SpirvFile fpx3d_vk_read_spirv_file(const char *filename,
                                            Fpx3d_Vk_E_ShaderStage stage);
Fpx3d_E_Result fpx3d_vk_destroy_spirv_file(Fpx3d_Vk_SpirvFile *);

struct _fpx3d_vk_shader_modules {
  // VkShaderModule input;
  VkShaderModule vertex;
  VkShaderModule tesselationControl;
  VkShaderModule tesselationEvaluation;
  VkShaderModule geometry;
  // VkShaderModule rasterization;
  VkShaderModule fragment;
  // VkShaderModule blending;
};

Fpx3d_E_Result fpx3d_vk_load_shadermodules(Fpx3d_Vk_SpirvFile *spirv_files,
                                           size_t spirv_count,
                                           Fpx3d_Vk_LogicalGpu *,
                                           Fpx3d_Vk_ShaderModuleSet *output);

Fpx3d_E_Result fpx3d_vk_destroy_shadermodules(Fpx3d_Vk_ShaderModuleSet *,
                                              Fpx3d_Vk_LogicalGpu *);

struct _fpx3d_vk_command_pool {
  VkCommandPool pool;

  VkCommandBuffer *buffers;
  size_t bufferCount;

  Fpx3d_Vk_E_CommandPoolType type;
};

struct fpx3d_vulkan_queues {
  VkQueue *queues;
  size_t count;

  size_t offsetInFamily;
  int queueFamilyIndex;
};

struct _fpx3d_vk_lgpu {
  VkDevice handle;
  VkPhysicalDeviceFeatures features;

  Fpx3d_Vk_Swapchain currentSwapchain;

  Fpx3d_Vk_Swapchain *oldSwapchainsList;
  Fpx3d_Vk_Swapchain *newestOldSwapchain;

  Fpx3d_Vk_CommandPool *commandPools;
  size_t commandPoolCapacity;

  Fpx3d_Vk_Pipeline *pipelines;
  size_t pipelineCapacity;

  VkRenderPass *renderPasses;
  size_t renderPassCapacity;

  struct fpx3d_vulkan_queues graphicsQueues;
  struct fpx3d_vulkan_queues presentQueues;
  struct fpx3d_vulkan_queues transferQueues;

  size_t queueFamilyCount;

  // don't alter the in-flight metadata in your own usage of this library,
  // unless you understand how to use it to your advantage. Check the
  // implementation in `vk.c` for details on how many in-flight fences and
  // command-buffers are already allocated
  //
  // also:
  // https://vulkan-tutorial.com/Drawing_a_triangle/Drawing/Frames_in_flight
  Fpx3d_Vk_CommandPool inFlightCommandPool;
  VkFence *inFlightFences;

  uint16_t frameCounter;
};

struct _fpx3d_vk_context {
  Fpx3d_Wnd_Context *windowContext;

  // this pointer will be passed into callbacks
  void *customPointer;

  VkPhysicalDevice physicalGpu;

  Fpx3d_Vk_LogicalGpu *logicalGpus;
  size_t logicalGpuCapacity;

  const char **lgpuExtensions;
  size_t lgpuExtensionCount;

  const char **validationLayers;
  size_t validationLayersCount;

  VkInstance vkInstance;
  VkSurfaceKHR vkSurface;

  VkApplicationInfo appInfo;
};

Fpx3d_E_Result fpx3d_vk_set_custom_pointer(Fpx3d_Vk_Context *, void *);
void *fpx3d_vk_get_custom_pointer(Fpx3d_Vk_Context *);

Fpx3d_Wnd_Context *fpx3d_vk_get_windowcontext(Fpx3d_Vk_Context *);

bool fpx3d_vk_validation_layers_supported(const char **layers,
                                          size_t layer_count);
bool fpx3d_vk_device_extensions_supported(VkPhysicalDevice,
                                          const char **extensions,
                                          size_t extension_count);

Fpx3d_Vk_SwapchainProperties
fpx3d_vk_get_swapchain_support(Fpx3d_Vk_Context *ctx, VkPhysicalDevice dev,
                               Fpx3d_Vk_SwapchainRequirements reqs);

Fpx3d_E_Result fpx3d_vk_init_context(Fpx3d_Vk_Context *, Fpx3d_Wnd_Context *);
Fpx3d_E_Result fpx3d_vk_create_window(Fpx3d_Vk_Context *);
Fpx3d_E_Result fpx3d_vk_destroy_window(Fpx3d_Vk_Context *,
                                       void (*destruction_callback)(void *));

Fpx3d_E_Result fpx3d_vk_select_gpu(Fpx3d_Vk_Context *,
                                   int (*scoring_function)(Fpx3d_Vk_Context *,
                                                           VkPhysicalDevice));

// TODO: make some functions to aid in finding what GPU is best.
// e.g., functions to check if certain features exist on a VkPhysicalDevice
/*
 * Helper functions for inside the `scoring_function` for picking a GPU
 */
bool fpx3d_vk_are_swapchains_supported(VkPhysicalDevice);
/*
 * End of helper functions.
 */

Fpx3d_E_Result fpx3d_vk_allocate_logicalgpus(Fpx3d_Vk_Context *, size_t amount);
Fpx3d_E_Result fpx3d_vk_create_logicalgpu_at(Fpx3d_Vk_Context *, size_t index,
                                             VkPhysicalDeviceFeatures,
                                             size_t graphics_queues,
                                             size_t present_queues,
                                             size_t transfer_queues);
Fpx3d_Vk_LogicalGpu *fpx3d_vk_get_logicalgpu_at(Fpx3d_Vk_Context *,
                                                size_t index);
Fpx3d_E_Result fpx3d_vk_destroy_logicalgpu_at(Fpx3d_Vk_Context *, size_t index);

VkQueue *fpx3d_vk_get_queue_at(Fpx3d_Vk_LogicalGpu *, size_t index,
                               Fpx3d_Vk_E_QueueType);
// unneccesary? queues are automagically destroyed at lgpu-cleanup time
// Fpx3d_E_Result fpx3d_vk_destroy_queue_at(Fpx3d_Vk_LogicalGpu *,
//                                          Fpx3d_Vk_E_QueueType type,
//                                          size_t index);

Fpx3d_E_Result fpx3d_vk_allocate_renderpasses(Fpx3d_Vk_LogicalGpu *,
                                              size_t count);
Fpx3d_E_Result fpx3d_vk_create_renderpass_at(Fpx3d_Vk_LogicalGpu *,
                                             size_t index);
VkRenderPass *fpx3d_vk_get_renderpass_at(Fpx3d_Vk_LogicalGpu *, size_t index);
Fpx3d_E_Result fpx3d_vk_destroy_renderpass_at(Fpx3d_Vk_LogicalGpu *,
                                              size_t index);

Fpx3d_E_Result fpx3d_vk_create_swapchain(Fpx3d_Vk_Context *,
                                         Fpx3d_Vk_LogicalGpu *,
                                         Fpx3d_Vk_SwapchainProperties);
Fpx3d_Vk_Swapchain *fpx3d_vk_get_current_swapchain(Fpx3d_Vk_LogicalGpu *);
Fpx3d_E_Result fpx3d_vk_destroy_current_swapchain(Fpx3d_Vk_LogicalGpu *);
Fpx3d_E_Result fpx3d_vk_refresh_current_swapchain(Fpx3d_Vk_Context *,
                                                  Fpx3d_Vk_LogicalGpu *);

// TODO: also create() and destroy()? only if necessary tho
Fpx3d_Vk_SwapchainFrame *fpx3d_vk_get_swapchain_frame_at(Fpx3d_Vk_Swapchain *,
                                                         size_t index);
Fpx3d_E_Result fpx3d_vk_present_swapchain_frame_at(Fpx3d_Vk_Swapchain *,
                                                   size_t index,
                                                   VkQueue *present_queue);

// TODO: also create() and destroy()? only if necessary tho
Fpx3d_E_Result fpx3d_vk_create_framebuffers(Fpx3d_Vk_Swapchain *,
                                            Fpx3d_Vk_LogicalGpu *,
                                            VkRenderPass *render_pass);

VkPipelineLayout fpx3d_vk_create_pipeline_layout(Fpx3d_Vk_LogicalGpu *);
Fpx3d_E_Result fpx3d_vk_destroy_pipeline_layout(Fpx3d_Vk_LogicalGpu *,
                                                VkPipelineLayout);

Fpx3d_E_Result fpx3d_vk_allocate_pipelines(Fpx3d_Vk_LogicalGpu *,
                                           size_t amount);

Fpx3d_E_Result fpx3d_vk_create_graphics_pipeline_at(
    Fpx3d_Vk_LogicalGpu *, size_t index, VkPipelineLayout p_layout,
    VkRenderPass *render_pass, Fpx3d_Vk_ShaderModuleSet *shaders,
    Fpx3d_Vk_VertexBinding *vertex_bindings, size_t vertex_bind_count);
Fpx3d_Vk_Pipeline *fpx3d_vk_get_pipeline_at(Fpx3d_Vk_LogicalGpu *,
                                            size_t index);
Fpx3d_E_Result fpx3d_vk_destroy_pipeline_at(Fpx3d_Vk_LogicalGpu *,
                                            size_t index);

Fpx3d_E_Result fpx3d_vk_allocate_commandpools(Fpx3d_Vk_LogicalGpu *,
                                              size_t amount);
Fpx3d_E_Result fpx3d_create_commandpool_at(Fpx3d_Vk_LogicalGpu *, size_t index,
                                           Fpx3d_Vk_E_CommandPoolType type);
Fpx3d_Vk_CommandPool *fpx3d_vk_get_commandpool_at(Fpx3d_Vk_LogicalGpu *,
                                                  size_t index);
Fpx3d_E_Result fpx3d_vk_destroy_commandpool_at(Fpx3d_Vk_LogicalGpu *,
                                               size_t index);

Fpx3d_E_Result fpx3d_vk_allocate_commandbuffers_at_pool(Fpx3d_Vk_LogicalGpu *,
                                                        size_t cmd_pool_index,
                                                        size_t amount);
Fpx3d_E_Result fpx3d_vk_create_commandbuffer_at(Fpx3d_Vk_CommandPool *,
                                                size_t index,
                                                Fpx3d_Vk_LogicalGpu *);
VkCommandBuffer *fpx3d_vk_get_commandbuffer_at(Fpx3d_Vk_CommandPool *,
                                               size_t index);
// TODO: research render pass relationship to command buffer. is it always
// 1:1? so can i store them in the same struct?
Fpx3d_E_Result fpx3d_vk_record_commandbuffer_at(VkCommandBuffer *,
                                                Fpx3d_Vk_Pipeline *pipeline,
                                                Fpx3d_Vk_Swapchain *swapchain,
                                                size_t frame_index,
                                                VkRenderPass *render_pass);
Fpx3d_E_Result fpx3d_vk_submit_commandbuffer_at(VkCommandBuffer *,
                                                Fpx3d_Vk_LogicalGpu *,
                                                size_t frame_index,
                                                VkQueue *graphics_queue);

// draw_frame handles the aquiring of a swapchain-frame, recording and
// submitting of a command buffer, and presenting the frame
Fpx3d_E_Result fpx3d_vk_draw_frame(Fpx3d_Vk_Context *ctx, Fpx3d_Vk_LogicalGpu *,
                                   Fpx3d_Vk_Pipeline *pipelines,
                                   size_t pipeline_count,
                                   VkQueue *graphics_queue,
                                   VkQueue *present_queue);

#endif
