/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#ifndef FPX3D_MODEL_GLTF_H
#define FPX3D_MODEL_GLTF_H

#include <stdint.h>

#include "../fpx3d.h"
#include "typedefs.h"

#include "../../modules/cglm/include/cglm/types.h"

// ALL ANGLES ARE IN RADIANS

struct fpx3d_model_gltf_mesh_primitive;

struct _fpx3d_model_gltf_scene {
  char *name;
  Fpx3d_Model_GltfNode **nodes;
};

struct _fpx3d_model_gltf_camera_perspective {
  float fov, aspectRatio;
};

struct _fpx3d_model_gltf_camera_orthographic {
  float xmag, ymag;
};

struct _fpx3d_model_gltf_camera {
  char *name;

  enum {
    FPX3D_MODEL_GLTF_PROJECTION_TYPE_INVALID = 0,
    FPX3D_MODEL_GLTF_PROJECTION_TYPE_PERSPECTIVE = 1,
    FPX3D_MODEL_GLTF_PROJECTION_TYPE_ORTHOGRAPHIC = 2,
  } type;

  union {
    struct _fpx3d_model_gltf_camera_perspective perspective;
    struct _fpx3d_model_gltf_camera_orthographic orthographic;
  };

  float nearPlane, farPlane;
};

struct _fpx3d_model_gltf_node {
  char *name;

  Fpx3d_Model_GltfCamera *camera;
  Fpx3d_Model_GltfSkin *skin;

  Fpx3d_Model_GltfMesh *mesh;

  float *meshMorphTargetWeights;
  size_t weightCount;

  vec3 scale;
  vec4 rotationQuat;
  vec3 translation;

  mat4 matrix;

  Fpx3d_Model_GltfNode **children;
  size_t childCount;
};

struct fpx3d_model_gltf_primitive_attribute {
  enum {
    FPX3D_GLTF_MESH_ATTRIBUTE_INVALID = 0,
    FPX3D_GLTF_MESH_ATTRIBUTE_POSITION = 1,
    FPX3D_GLTF_MESH_ATTRIBUTE_NORMAL = 2,
    FPX3D_GLTF_MESH_ATTRIBUTE_TANGENT = 3,
    FPX3D_GLTF_MESH_ATTRIBUTE_TEXCOORD = 4,
    FPX3D_GLTF_MESH_ATTRIBUTE_COLOR = 5,
    FPX3D_GLTF_MESH_ATTRIBUTE_JOINTS = 6,
    FPX3D_GLTF_MESH_ATTRIBUTE_WEIGHTS = 7,
  } attribute;
  uint8_t n;

  Fpx3d_Model_GltfAccessor *accessor;
};

struct fpx3d_model_gltf_mesh_primitive {
  struct fpx3d_model_gltf_primitive_attribute *attributes;
  size_t attributeCount;

  Fpx3d_Model_GltfAccessor *indices;
  Fpx3d_Model_GltfMaterial *material;

  enum {
    FPX3D_GLTF_RENDER_MODE_POINTS = 0,
    FPX3D_GLTF_RENDER_MODE_LINES = 1,
    FPX3D_GLTF_RENDER_MODE_LINE_LOOP = 2,
    FPX3D_GLTF_RENDER_MODE_LINE_STRIP = 3,
    FPX3D_GLTF_RENDER_MODE_TRIANGLES = 4,
    FPX3D_GLTF_RENDER_MODE_TRIANGLE_STRIP = 5,
    FPX3D_GLTF_RENDER_MODE_TRIANGLE_FAN = 6,
  } renderMode;

  struct {
    struct fpx3d_model_gltf_primitive_attribute *attributes;
    size_t attributeCount;
  } *morphTargets;
  size_t morphTargetCount;
};

struct _fpx3d_model_gltf_mesh {
  char *name;

  struct fpx3d_model_gltf_mesh_primitive *primitives;
  size_t primitiveCount;

  float *morphTargetWeights;
  size_t weightCount;
};

struct _fpx3d_model_gltf_buffer {
  char *name;

  char *uri; // ptr is NULL if buffer is GLB-embedded

  void *data;
  size_t dataLength;
};

struct _fpx3d_model_gltf_buffer_view {
  char *name;

  Fpx3d_Model_GltfBuffer *buffer;

  size_t byteOffset;
  size_t byteLength;

  size_t byteStride;

  enum {
    FPX3D_GLTF_BUFFER_VIEW_TARGET_INVALID = 0,
    FPX3D_GLTF_BUFFER_VIEW_TARGET_ARRAY_BUFFER = 34962,
    FPX3D_GLTF_BUFFER_VIEW_TARGET_ELEMENT_ARRAY_BUFFER = 34963,
  } target;
};

struct _fpx3d_model_gltf_accessor {
  char *name;

  Fpx3d_Model_GltfBufferView *view;
  size_t byteOffset;

  Fpx3d_Model_E_GltfComponentType componentType;
  bool componentsNormalized;

  size_t elementCount;

  enum {
    FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_INVALID = 0,
    FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_SCALAR = 1,
    FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_VEC2 = 2,
    FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_VEC3 = 3,
    FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_VEC4 = 4,
    FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_MAT2 = 5,
    FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_MAT3 = 6,
    FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_MAT4 = 7,
  } elementType;

  union {
    float scalar;
    vec2 vector2;
    vec3 vector3;
    vec4 vector4;
    mat2 matrix2;
    mat3 matrix3;
    mat4 matrix4;
  } maxValues, minValues;

  struct {
    size_t count;
    struct {
      Fpx3d_Model_GltfBufferView *view;
      size_t byteOffset;
      Fpx3d_Model_E_GltfComponentType componentType;
    } indices, values;
  } sparse;
};

struct _fpx3d_model_gltf_image {
  char *name;

  char *uri;

  Fpx3d_Model_GltfBufferView *bufferView;
  char *mimeType;
};

struct _fpx3d_model_gltf_sampler {
  char *name;

  enum {
    FPX3D_GLTF_SAMPLER_FILTER_INVALID = 0,
    FPX3D_GLTF_SAMPLER_FILTER_NEAREST = 9728,
    FPX3D_GLTF_SAMPLER_FILTER_LINEAR = 9729,
    FPX3D_GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST = 9984,
    FPX3D_GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEARETS = 9985,
    FPX3D_GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR = 9986,
    FPX3D_GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR = 9987,
  } minFilter,
      magFilter;

  enum {
    FPX3D_GLTF_SAMPLER_WRAP_INVALID = 0,
    FPX3D_GLTF_SAMPLER_WRAP_CLAMP_TO_EDGE = 33071,
    FPX3D_GLTF_SAMPLER_WRAP_MIRRORED_REPEAT = 33684,
    FPX3D_GLTF_SAMPLER_WRAP_REPEAT = 10497,
  } wrapU,
      wrapV;
};

struct _fpx3d_model_gltf_texture {
  char *name;

  Fpx3d_Model_GltfSampler *sampler;
  Fpx3d_Model_GltfImage *sourceImage;
};

struct fpx3d_model_gltf_texture_info {
  Fpx3d_Model_GltfTexture *texture;
  uint8_t texCoordIndex;
};

struct _fpx3d_model_gltf_material {
  char *name;

  struct {
    float baseColorFactor[4];
    struct fpx3d_model_gltf_texture_info baseColorTexture;

    float metallicFactor;
    float roughnessFactor;

    struct fpx3d_model_gltf_texture_info metallicRoughnessTexture;
  } pbrMetallicRoughness;

  struct {
    struct fpx3d_model_gltf_texture_info textureInfo;
    float scale;
  } normalTexture;

  struct {
    struct fpx3d_model_gltf_texture_info textureInfo;
    float strength;
  } occlusionTexture;

  struct fpx3d_model_gltf_texture_info emissiveTexture;
  float emissiveFactor[3];

  enum {
    FPX3D_GLTF_ALPHA_MODE_INVALID = 0,
    FPX3D_GLTF_ALPHA_MODE_OPAQUE = 1,
    FPX3D_GLTF_ALPHA_MODE_MASK = 2,
    FPX3D_GLTF_ALPHA_MODE_BLEND = 3,
  } alphaMode;
  float alphaCutoff;

  bool doubleSided;
};

struct _fpx3d_model_gltf_skin {
  char *name;

  Fpx3d_Model_GltfAccessor *inverseBindMatrices;

  Fpx3d_Model_GltfNode *skeletonRoot;
  Fpx3d_Model_GltfNode **joints;
  size_t jointCount;
};

struct fpx3d_model_gltf_anim_sampler {
  Fpx3d_Model_GltfAccessor *keyframes;
  Fpx3d_Model_GltfAccessor *outputValues;
  enum {
    FPX3D_GLTF_ANIM_INTERPOLATION_INVALID = 0,
    FPX3D_GLTF_ANIM_INTERPOLATION_LINEAR = 1,
    FPX3D_GLTF_ANIM_INTERPOLATION_STEP = 2,
    FPX3D_GLTF_ANIM_INTERPOLATION_CUBICSPLINE = 3,
  } interpolation;
};

struct fpx3d_model_gltf_anim_channel {
  struct fpx3d_model_gltf_anim_sampler *sampler;

  struct {
    Fpx3d_Model_GltfNode *node;
    enum {
      FPX3D_GLTF_ANIM_PATH_INVALID = 0,
      FPX3D_GLTF_ANIM_PATH_TRANSLATION = 1,
      FPX3D_GLTF_ANIM_PATH_ROTATION = 2,
      FPX3D_GLTF_ANIM_PATH_SCALE = 3,
      FPX3D_GLTF_ANIM_PATH_WEIGHTS = 4,
    } path;
  } target;
};

struct _fpx3d_model_gltf_anim {
  char *name;

  struct fpx3d_model_gltf_anim_channel *channels;
  size_t channelCount;

  struct fpx3d_model_gltf_anim_sampler *samplers;
  size_t samplerCount;
};

struct _fpx3d_model_gltf_asset_description {
  struct {
    uint8_t major, minor;
  } version;

  Fpx3d_Model_GltfScene *scenes;
  Fpx3d_Model_GltfCamera *cameras;
  Fpx3d_Model_GltfNode *nodes;
  Fpx3d_Model_GltfMesh *meshes;
  Fpx3d_Model_GltfBuffer *buffers;
  Fpx3d_Model_GltfBufferView *bufferViews;
  Fpx3d_Model_GltfAccessor *accessors;
  Fpx3d_Model_GltfImage *images;
  Fpx3d_Model_GltfSampler *samplers;
  Fpx3d_Model_GltfTexture *textures;
  Fpx3d_Model_GltfMaterial *materials;
  Fpx3d_Model_GltfSkin *skins;
  Fpx3d_Model_GltfAnimation *animations;

  size_t sceneCount;
  size_t cameraCount;
  size_t nodeCount;
  size_t meshCount;
  size_t bufferCount;
  size_t bufferViewCount;
  size_t accessorCount;
  size_t imageCount;
  size_t samplerCount;
  size_t textureCount;
  size_t materialCount;
  size_t skinCount;
  size_t animationCount;

  // ptr to one of the cameras in the `cameras` array
  Fpx3d_Model_GltfCamera *mainCamera;
};

struct fpx3d_model_glb_chunk {
  Fpx3d_Model_E_GlbChunkType type;

  union {
    Fpx3d_Model_GltfAssetDescription json;
    Fpx3d_Model_GltfBuffer binary;
  };
};

struct _fpx3d_model_gltf_asset_glb {
  uint32_t containerVersion;
  size_t chunkCount;
  struct fpx3d_model_glb_chunk chunks[2];
};

struct _fpx3d_model_gltf_asset {
  Fpx3d_Model_E_GltfContainerType containerType;

  union {
    Fpx3d_Model_GltfAssetDescription gltf;
    struct _fpx3d_model_gltf_asset_glb glb;
  };
};

// can parse either glTF or GLB-container
Fpx3d_E_Result fpx3d_model_read_gltf(const uint8_t *data, size_t datalength,
                                     Fpx3d_Model_GltfAsset *output);

Fpx3d_E_Result
fpx3d_model_parse_gltf_json(const uint8_t *data, size_t dataLength,
                            struct fpx3d_model_glb_chunk *output);

Fpx3d_E_Result
fpx3d_model_parse_gltf_binary(const uint8_t *, size_t dataLength,
                              struct fpx3d_model_glb_chunk *output);

#endif // FPX3D_MODEL_GLTF_H
