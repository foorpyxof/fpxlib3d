/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX3D_MODEL_TYPEDEFS_H
#define FPX3D_MODEL_TYPEDEFS_H

typedef struct _fpx3d_model_model Fpx3d_Model_Model;

// this is a default Vertex type.
// The functions in other components (e.g. Vulkan) will allow you to specify
// custom Vertex types for use in rendering and shading
typedef struct _fpx3d_model_vertex Fpx3d_Model_Vertex;

// -------------------- GLTF --------------------
typedef enum {
  FPX3D_GLTF_CONTAINER_INVALID = 0,
  FPX3D_GLTF_CONTAINER_GLTF = 1,
  FPX3D_GLTF_CONTAINER_GLB = 2,
} Fpx3d_Model_E_GltfContainerType;
typedef enum {
  FPX3D_GLB_CHUNK_INVALID = 0,
  FPX3D_GLB_CHUNK_JSON = 1,
  FPX3D_GLB_CHUNK_BINARY = 2,
} Fpx3d_Model_E_GlbChunkType;
typedef enum {
  FPX3D_GLTF_COMPONENT_TYPE_INVALID = 0,
  FPX3D_GLTF_COMPONENT_TYPE_BYTE = 5120,
  FPX3D_GLTF_COMPONENT_TYPE_UNSIGNED_BYTE = 5121,
  FPX3D_GLTF_COMPONENT_TYPE_SHORT = 5122,
  FPX3D_GLTF_COMPONENT_TYPE_UNSIGNED_SHORT = 5123,
  FPX3D_GLTF_COMPONENT_TYPE_UNSIGNED_INT = 5125,
  FPX3D_GLTF_COMPONENT_TYPE_FLOAT = 5126,
} Fpx3d_Model_E_GltfComponentType;

typedef struct _fpx3d_model_gltf_scene Fpx3d_Model_GltfScene;
typedef struct _fpx3d_model_gltf_camera Fpx3d_Model_GltfCamera;
typedef struct _fpx3d_model_gltf_node Fpx3d_Model_GltfNode;
typedef struct _fpx3d_model_gltf_mesh Fpx3d_Model_GltfMesh;
typedef struct _fpx3d_model_gltf_buffer Fpx3d_Model_GltfBuffer;
typedef struct _fpx3d_model_gltf_buffer_view Fpx3d_Model_GltfBufferView;
typedef struct _fpx3d_model_gltf_accessor Fpx3d_Model_GltfAccessor;
typedef struct _fpx3d_model_gltf_image Fpx3d_Model_GltfImage;
typedef struct _fpx3d_model_gltf_sampler Fpx3d_Model_GltfSampler;
typedef struct _fpx3d_model_gltf_texture Fpx3d_Model_GltfTexture;
typedef struct _fpx3d_model_gltf_material Fpx3d_Model_GltfMaterial;
typedef struct _fpx3d_model_gltf_skin Fpx3d_Model_GltfSkin;
typedef struct _fpx3d_model_gltf_anim Fpx3d_Model_GltfAnimation;

typedef struct _fpx3d_model_gltf_asset_description
    Fpx3d_Model_GltfAssetDescription;

typedef struct _fpx3d_model_gltf_asset Fpx3d_Model_GltfAsset;
// ----------------- END OF GLTF ----------------

#endif // FPX3D_MODEL_TYPEDEFS_H
