/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX_VK_H
#define FPX_VK_H

#include <sys/types.h>

// so the compiler doesn't whine abt VOLK
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif // VK_NO_PROTOTYPES

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "../modules/cglm/include/cglm/types.h"

#include "fpx3d.h"
#include "window/window.h"

#include "vk/typedefs.h"

#include "vk/buffer.h"
#include "vk/command.h"
#include "vk/context.h"
#include "vk/descriptors.h"
#include "vk/image.h"
#include "vk/logical_gpu.h"
#include "vk/pipeline.h"
#include "vk/queues.h"
#include "vk/renderpass.h"
#include "vk/shaders.h"
#include "vk/shape.h"
#include "vk/swapchain.h"
#include "vk/vertex.h"

#include "vk/utility.h"

#endif // FPX_VK_H
