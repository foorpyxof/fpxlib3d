/*
 * Copyright (c) Erynn Scholtes
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <vulkan/vulkan_core.h>

#include "debug.h"
#include "fpx3d.h"
#include "fpxlibc/include/c-utils/format.h"
#include "fpxlibc/include/networking/netutils.h"
#include "fpxlibc/include/serialize/json.h"
#include "macros.h"
#include "model/gltf.h"
#include "model/model.h"
#include "model/typedefs.h"

#define glTF_HEADER_SIZE 12

#define IS_WHITESPACE(character)                                               \
  (character == 0x20 || character == 0x0A || character == 0x0D ||              \
   character == 0x09)

#define TRIM_WHITESPACE(_dataptr, _limitptr)                                   \
  {                                                                            \
    for (; (_dataptr < _limitptr) && IS_WHITESPACE(*_dataptr); ++_dataptr)     \
      ;                                                                        \
  }

extern Fpx3d_E_Result __fpx3d_realloc_array(void **arr, size_t obj_size,
                                            size_t amount,
                                            size_t *old_capacity);

static Fpx3d_E_Result
_json_to_asset_desc(const uint8_t *data, const uint8_t *limit,
                    Fpx3d_Model_GltfAssetDescription *output);

// expects bytes 0,1,2,3 to be chunk_length
// expects bytes 4,5,6,7 to be "JSON"
// returns FPX3D_MODEL_ERROR otherwise
static Fpx3d_E_Result _parse_json_chunk(const uint8_t **dataptr,
                                        const uint8_t *limit,
                                        Fpx_Json_Entity *output);

static Fpx3d_E_Result _alloc_top_level(Fpx3d_Model_GltfAssetDescription *output,
                                       const Fpx_Json_Entity *input);

static void _free_top_level(Fpx3d_Model_GltfAssetDescription *asset_desc);

static Fpx_Json_Value *_get_value_by_key(Fpx_Json_Object *obj, const char *key,
                                         Fpx_Json_E_ValueType type);

static Fpx3d_E_Result _parse_scenes(Fpx_Json_Array *scenes,
                                    Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result _parse_cameras(Fpx_Json_Array *cameras,
                                     Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result _parse_nodes(Fpx_Json_Array *nodes,
                                   Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result _parse_primitive_attributes(
    struct fpx3d_model_gltf_primitive_attribute *attributes_output,
    Fpx_Json_Value *json_obj, Fpx3d_Model_GltfAssetDescription *parent_asset);

static Fpx3d_E_Result
_parse_mesh_primitives(Fpx_Json_Array *primitives,
                       struct fpx3d_model_gltf_mesh_primitive *output,
                       Fpx3d_Model_GltfAssetDescription *parent_asset);

static Fpx3d_E_Result _parse_meshes(Fpx_Json_Array *meshes,
                                    Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result _parse_buffers(Fpx_Json_Array *buffers,
                                     Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result
_parse_buffer_views(Fpx_Json_Array *views,
                    Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result
_parse_accessors(Fpx_Json_Array *accessors,
                 Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result _parse_images(Fpx_Json_Array *images,
                                    Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result _parse_samplers(Fpx_Json_Array *samplers,
                                      Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result _parse_textures(Fpx_Json_Array *textures,
                                      Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result
_parse_tex_info(Fpx_Json_Object *texture_info,
                struct fpx3d_model_gltf_texture_info *output,
                Fpx3d_Model_GltfAssetDescription *asset);

static Fpx3d_E_Result
_parse_materials(Fpx_Json_Array *materials,
                 Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result _parse_skins(Fpx_Json_Array *skins,
                                   Fpx3d_Model_GltfAssetDescription *output);

static Fpx3d_E_Result
_parse_animations(Fpx_Json_Array *animations,
                  Fpx3d_Model_GltfAssetDescription *output);

static void _destroy_asset_desc(Fpx3d_Model_GltfAssetDescription *asset_desc);

static void _destroy_chunk(struct fpx3d_model_glb_chunk *chunkptr);

Fpx3d_E_Result fpx3d_model_read_gltf(const uint8_t *data, size_t datalength,
                                     Fpx3d_Model_GltfAsset *output) {
  NULL_CHECK(data, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  const uint8_t *limit = data + datalength;
  TRIM_WHITESPACE(data, limit);

  FPX3D_DEBUG("Parsing glTF/GLB asset");

  Fpx3d_Model_GltfAsset new_asset = {0};

  if (*data == '{') {
    new_asset.containerType = FPX3D_GLTF_CONTAINER_GLTF;
  } else {
    if (limit - data < glTF_HEADER_SIZE) // header length
      return FPX3D_ARGS_ERROR;

    uint32_t *header = (uint32_t *)data;

    if (0 != memcmp(header, "glTF\x02\x00\x00\x00", 8))
      return FPX3D_MODEL_INVALID_FILE_ERROR;

    {
      uint32_t gltf_len = header[2];

      // make sure the file length is read as little endian
      fpx_endian_swap_if_network(&gltf_len, sizeof(gltf_len));

      if (datalength != gltf_len)
        return FPX3D_MODEL_INVALID_FILE_ERROR;
    }

    data += glTF_HEADER_SIZE;

    new_asset.containerType = FPX3D_GLTF_CONTAINER_GLB;
  }

  if (new_asset.containerType == FPX3D_GLTF_CONTAINER_GLTF) {
    Fpx3d_E_Result json_result =
        _json_to_asset_desc(data, limit, &new_asset.gltf);

    if (FPX3D_SUCCESS > json_result)
      return json_result;

  } else if (new_asset.containerType == FPX3D_GLTF_CONTAINER_GLB) {

    // now we parse chunks
    for (size_t chunk_idx = 0;
         data < limit && chunk_idx < ARRAY_SIZE(output->glb.chunks);
         ++chunk_idx) {

#define UNWIND_ASSET(retval)                                                   \
  {                                                                            \
    for (size_t destr_idx = 0; destr_idx <= chunk_idx; ++destr_idx) {          \
      _destroy_chunk(&new_asset.glb.chunks[destr_idx]);                        \
    }                                                                          \
    return retval;                                                             \
  }

      uint32_t *header = (uint32_t *)data;

      uint32_t chunk_len = header[0];

      // read length as little endian
      fpx_endian_swap_if_network(&chunk_len, sizeof(chunk_len));

      data += 8;

      if (0 == memcmp(data - 4, "JSON", 4)) {
        // reading a JSON chunk
        FPX3D_DEBUG(" - Found JSON glb chunk");
        new_asset.glb.chunks[chunk_idx].type = FPX3D_GLB_CHUNK_JSON;

        Fpx3d_E_Result json_result = _json_to_asset_desc(
            data, data + chunk_len, &new_asset.glb.chunks[chunk_idx].json);

        if (FPX3D_SUCCESS > json_result) {
          UNWIND_ASSET(json_result);
        }

      } else if (0 == memcmp(data - 4, "BIN", 4)) {
        // reading a BINARY chunk
        FPX3D_DEBUG(" - Found BINARY glb chunk");
        new_asset.glb.chunks[chunk_idx].type = FPX3D_GLB_CHUNK_BINARY;

        Fpx3d_E_Result chunk_alloc_res = __fpx3d_realloc_array(
            &new_asset.glb.chunks[chunk_idx].binary.data, 1, chunk_len,
            &new_asset.glb.chunks[chunk_idx].binary.dataLength);

        if (FPX3D_SUCCESS > chunk_alloc_res)
          UNWIND_ASSET(chunk_alloc_res);

        memcpy(new_asset.glb.chunks[chunk_idx].binary.data, data, chunk_len);
      }

      data += chunk_len;

#undef UNWIND_ASSET
    }
  }

  *output = new_asset;

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_json_to_asset_desc(const uint8_t *data, const uint8_t *limit,
                    Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(data, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  Fpx_Json_Entity json = {0};

  {
    Fpx3d_E_Result json_res = _parse_json_chunk(&data, limit, &json);
    if (FPX3D_SUCCESS > json_res || false == json.isValid ||
        FPX_JSON_VALUE_OBJECT != json.root.valueType) {
      fpx_json_destroy(&json);
      return json_res;
    }
  }

  Fpx3d_E_Result alloc_res = _alloc_top_level(output, &json);
  if (FPX3D_SUCCESS > alloc_res) {
    fpx_json_destroy(&json);
    return alloc_res;
  }

#define PARSE_COMPONENT(component, parsing_function)                           \
  {                                                                            \
    Fpx_Json_Value *component = _get_value_by_key(                             \
        &json.root.object, #component, FPX_JSON_VALUE_ARRAY);                  \
                                                                               \
    if (NULL != component) {                                                   \
      Fpx3d_E_Result parse_res = parsing_function(&component->array, output);  \
                                                                               \
      if (FPX3D_SUCCESS > parse_res) {                                         \
        fpx_json_destroy(&json);                                               \
        return parse_res;                                                      \
      }                                                                        \
    }                                                                          \
  }

  PARSE_COMPONENT(scenes, _parse_scenes);
  PARSE_COMPONENT(cameras, _parse_cameras);
  PARSE_COMPONENT(nodes, _parse_nodes);
  PARSE_COMPONENT(meshes, _parse_meshes);
  PARSE_COMPONENT(buffers, _parse_buffers);
  PARSE_COMPONENT(bufferViews, _parse_buffer_views);
  PARSE_COMPONENT(accessors, _parse_accessors);
  PARSE_COMPONENT(images, _parse_images);
  PARSE_COMPONENT(samplers, _parse_samplers);
  PARSE_COMPONENT(textures, _parse_textures);
  PARSE_COMPONENT(materials, _parse_materials);
  PARSE_COMPONENT(skins, _parse_skins);
  PARSE_COMPONENT(animations, _parse_animations);

#undef PARSE_COMPONENT

  fpx_json_destroy(&json);
  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _parse_json_chunk(const uint8_t **dataptr,
                                        const uint8_t *limit,
                                        Fpx_Json_Entity *output) {
  NULL_CHECK(dataptr, FPX3D_ARGS_ERROR);
  NULL_CHECK(*dataptr, FPX3D_ARGS_ERROR);
  NULL_CHECK(limit, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  const uint8_t *data = *dataptr;

  if (limit - data < 8)
    return FPX3D_MODEL_ERROR;

  Fpx_Json_Entity new_json = fpx_json_read((const char *)data, limit - data);

  // TODO:
  // 1 - parse Fpx_Json_Entity structure into GltfAssetDescription
  // 2 - Use that GltfAssetDescription in the bigger GltfAsset object

  if (!new_json.isValid) {
    return FPX3D_MODEL_ERROR;
  }

  *output = new_json;
  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _alloc_top_level(Fpx3d_Model_GltfAssetDescription *output,
                                       const Fpx_Json_Entity *input) {
  NULL_CHECK(input, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  for (size_t i = 0; i < input->root.object.memberCount; ++i) {
    Fpx_Json_Member *m = &input->root.object.members[i];

    if (m->value->valueType != FPX_JSON_VALUE_ARRAY)
      continue;

    switch (m->key.data[0]) {
    case 's':
      if (0 == strcmp("scenes", m->key.data)) {
        // scenes top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->scenes,
                                           sizeof(Fpx3d_Model_GltfScene),
                                           m->value->array.count,
                                           &output->sceneCount),
                     alloc_res, return alloc_res;);

      } else if (0 == strcmp("samplers", m->key.data)) {
        // samplers top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->samplers,
                                           sizeof(Fpx3d_Model_GltfSampler),
                                           m->value->array.count,
                                           &output->samplerCount),
                     alloc_res, return alloc_res;);

      } else if (0 == strcmp("skins", m->key.data))
        // skins top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array(
                         (void **)&output->skins, sizeof(Fpx3d_Model_GltfSkin),
                         m->value->array.count, &output->skinCount),
                     alloc_res, return alloc_res;);

      break;

    case 'c':
      if (0 == strcmp("cameras", m->key.data)) {
        // cameras top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->cameras,
                                           sizeof(Fpx3d_Model_GltfCamera),
                                           m->value->array.count,
                                           &output->cameraCount),
                     alloc_res, return alloc_res;);
      }

      break;

    case 'n':
      if (0 == strcmp("nodes", m->key.data)) {
        // nodes top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array(
                         (void **)&output->nodes, sizeof(Fpx3d_Model_GltfNode),
                         m->value->array.count, &output->nodeCount),
                     alloc_res, return alloc_res;);
      }

      break;

    case 'm':
      if (0 == strcmp("meshes", m->key.data)) {
        // meshes top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array(
                         (void **)&output->meshes, sizeof(Fpx3d_Model_GltfMesh),
                         m->value->array.count, &output->meshCount),
                     alloc_res, return alloc_res;);

      } else if (0 == strcmp("materials", m->key.data))
        // materials top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->materials,
                                           sizeof(Fpx3d_Model_GltfMaterial),
                                           m->value->array.count,
                                           &output->materialCount),
                     alloc_res, return alloc_res;);

      break;

    case 'b':
      if (0 == strcmp("buffers", m->key.data)) {
        // buffers top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->buffers,
                                           sizeof(Fpx3d_Model_GltfBuffer),
                                           m->value->array.count,
                                           &output->bufferCount),
                     alloc_res, return alloc_res;);

      } else if (0 == strcmp("bufferViews", m->key.data))
        // bufferViews top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->bufferViews,
                                           sizeof(Fpx3d_Model_GltfBufferView),
                                           m->value->array.count,
                                           &output->bufferViewCount),
                     alloc_res, return alloc_res;);

      break;

    case 'a':
      if (0 == strcmp("accessors", m->key.data)) {
        // accessors top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->accessors,
                                           sizeof(Fpx3d_Model_GltfAccessor),
                                           m->value->array.count,
                                           &output->accessorCount),
                     alloc_res, return alloc_res;);

      } else if (0 == strcmp("animations", m->key.data))
        // animations top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->animations,
                                           sizeof(Fpx3d_Model_GltfAnimation),
                                           m->value->array.count,
                                           &output->animationCount),
                     alloc_res, return alloc_res;);

      break;

    case 'i':
      if (0 == strcmp("images", m->key.data)) {
        // images top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->images,
                                           sizeof(Fpx3d_Model_GltfImage),
                                           m->value->array.count,
                                           &output->imageCount),
                     alloc_res, return alloc_res;);
      }

      break;

    case 't':
      if (0 == strcmp("textures", m->key.data)) {
        // textures top-level array
        FPX3D_ONFAIL(__fpx3d_realloc_array((void **)&output->textures,
                                           sizeof(Fpx3d_Model_GltfTexture),
                                           m->value->array.count,
                                           &output->textureCount),
                     alloc_res, return alloc_res;);
      }

      break;

    default:
      break;
    }
  }

  return FPX3D_SUCCESS;
}

static void _free_top_level(Fpx3d_Model_GltfAssetDescription *asset_desc) {
  NULL_CHECK(asset_desc, );

  FREE_SAFE(asset_desc->scenes);
  FREE_SAFE(asset_desc->cameras);
  FREE_SAFE(asset_desc->nodes);
  FREE_SAFE(asset_desc->meshes);
  FREE_SAFE(asset_desc->buffers);
  FREE_SAFE(asset_desc->bufferViews);
  FREE_SAFE(asset_desc->accessors);
  FREE_SAFE(asset_desc->images);
  FREE_SAFE(asset_desc->samplers);
  FREE_SAFE(asset_desc->textures);
  FREE_SAFE(asset_desc->materials);
  FREE_SAFE(asset_desc->skins);
  FREE_SAFE(asset_desc->animations);

  memset(asset_desc, 0, sizeof(*asset_desc));

  return;
}

static Fpx_Json_Value *_get_value_by_key(Fpx_Json_Object *obj, const char *key,
                                         Fpx_Json_E_ValueType type) {
  NULL_CHECK(obj, NULL);
  NULL_CHECK(key, NULL);

  for (size_t i = 0; i < obj->memberCount; ++i) {
    if (0 == strncmp(key, obj->members[i].key.data, strlen(key) + 1) &&
        type == obj->members[i].value->valueType)
      return obj->members[i].value;
  }

  return NULL;
}

static Fpx3d_E_Result _parse_scenes(Fpx_Json_Array *scenes,
                                    Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(scenes, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->scenes, FPX3D_SUCCESS);

  Fpx3d_Model_GltfScene *output_s = output->scenes;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_s[_iter].name);                                         \
      FREE_SAFE(output_s[_iter].nodes);                                        \
      memset(&output_s[_iter], 0, sizeof(output_s[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < scenes->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != scenes->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Value *json_scene_name = _get_value_by_key(
        &scenes->values[i].object, "name", FPX_JSON_VALUE_STRING);

    Fpx_Json_Value *json_node_array = _get_value_by_key(
        &scenes->values[i].object, "nodes", FPX_JSON_VALUE_ARRAY);

    if (NULL == json_scene_name || NULL == json_node_array)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    if (NULL != json_scene_name) {
      size_t temp_cap = 0;

      Fpx3d_E_Result name_alloc =
          __fpx3d_realloc_array((void **)&output_s[i].name, 1,
                                json_scene_name->string.size + 1, &temp_cap);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_s[i].name, json_scene_name->string.data,
             json_scene_name->string.size + 1);
    }

    if (NULL != json_node_array) {
      if (NULL == output->nodes)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      size_t temp_cap = 0;

      Fpx3d_E_Result node_alloc = __fpx3d_realloc_array(
          (void **)&output_s[i].nodes, sizeof(Fpx3d_Model_GltfNode *),
          json_node_array->array.count, &temp_cap);

      if (FPX3D_SUCCESS > node_alloc)
        PARSE_FAIL(node_alloc);

      for (size_t n_idx_idx = 0; n_idx_idx < json_node_array->array.count;
           ++n_idx_idx) {
        if (FPX_JSON_VALUE_NUMBER !=
            json_node_array->array.values[n_idx_idx].valueType) {
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);
        }

        size_t node_offset =
            (size_t)json_node_array->array.values[n_idx_idx].number;

        if (output->nodeCount <= node_offset)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_s[i].nodes[n_idx_idx] = output->nodes + node_offset;
      }
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _parse_cameras(Fpx_Json_Array *cameras,
                                     Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(cameras, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->cameras, FPX3D_SUCCESS);

  Fpx3d_Model_GltfCamera *output_c = output->cameras;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_c[_iter].name);                                         \
      memset(&output_c[_iter], 0, sizeof(output_c[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < cameras->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != cameras->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Value *json_camera_name = _get_value_by_key(
        &cameras->values[i].object, "name", FPX_JSON_VALUE_STRING);

    Fpx_Json_Value *json_camera_type = _get_value_by_key(
        &cameras->values[i].object, "type", FPX_JSON_VALUE_STRING);

    if (NULL == json_camera_type)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    if (NULL != json_camera_name) {
      size_t temp_cap = 0;

      Fpx3d_E_Result name_alloc =
          __fpx3d_realloc_array((void **)&output_c[i].name, 1,
                                json_camera_name->string.size + 1, &temp_cap);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_c[i].name, json_camera_name->string.data,
             json_camera_name->string.size + 1);
    }

    char *cam_type = NULL;

    if (0 == strcmp("perspective", json_camera_type->string.data)) {
      output_c[i].type = FPX3D_MODEL_GLTF_PROJECTION_TYPE_PERSPECTIVE;

      cam_type = "perspective";
    } else if (0 == strcmp("orthographic", json_camera_type->string.data)) {
      output_c[i].type = FPX3D_MODEL_GLTF_PROJECTION_TYPE_ORTHOGRAPHIC;

      cam_type = "orthographic";
    }

    Fpx_Json_Value *json_cam_props = _get_value_by_key(
        &cameras->values[i].object, cam_type, FPX_JSON_VALUE_OBJECT);

    if (NULL == json_cam_props)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    if (FPX3D_MODEL_GLTF_PROJECTION_TYPE_PERSPECTIVE == output_c[i].type) {
      Fpx_Json_Value *ratio = _get_value_by_key(
          &cameras->values[i].object, "aspectRatio", FPX_JSON_VALUE_NUMBER);
      Fpx_Json_Value *fov = _get_value_by_key(&cameras->values[i].object,
                                              "yfov", FPX_JSON_VALUE_NUMBER);
      Fpx_Json_Value *near_plane = _get_value_by_key(
          &cameras->values[i].object, "znear", FPX_JSON_VALUE_NUMBER);
      Fpx_Json_Value *far_plane = _get_value_by_key(
          &cameras->values[i].object, "zfar", FPX_JSON_VALUE_NUMBER);

      if (NULL == fov || NULL == near_plane)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      if (NULL != ratio) {
        output_c[i].perspective.aspectRatio = ratio->number;
      }

      if (NULL != far_plane) {
        output_c[i].farPlane = far_plane->number;
      }

      output_c[i].nearPlane = near_plane->number;
      output_c[i].perspective.fov = fov->number;
    } else if (FPX3D_MODEL_GLTF_PROJECTION_TYPE_ORTHOGRAPHIC ==
               output_c[i].type) {
      Fpx_Json_Value *xmag = _get_value_by_key(&cameras->values[i].object,
                                               "xmag", FPX_JSON_VALUE_NUMBER);
      Fpx_Json_Value *ymag = _get_value_by_key(&cameras->values[i].object,
                                               "ymag", FPX_JSON_VALUE_NUMBER);
      Fpx_Json_Value *near_plane = _get_value_by_key(
          &cameras->values[i].object, "znear", FPX_JSON_VALUE_NUMBER);
      Fpx_Json_Value *far_plane = _get_value_by_key(
          &cameras->values[i].object, "zfar", FPX_JSON_VALUE_NUMBER);

      if (NULL == xmag || NULL == ymag || NULL == near_plane ||
          NULL == far_plane)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_c[i].nearPlane = near_plane->number;
      output_c[i].farPlane = far_plane->number;
      output_c[i].orthographic.xmag = xmag->number;
      output_c[i].orthographic.ymag = ymag->number;
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _parse_nodes(Fpx_Json_Array *nodes,
                                   Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(nodes, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->nodes, FPX3D_SUCCESS);

  Fpx3d_Model_GltfNode *output_n = output->nodes;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_n[_iter].name);                                         \
      FREE_SAFE(output_n[_iter].meshMorphTargetWeights);                       \
      FREE_SAFE(output_n[_iter].children);                                     \
      memset(&output_n[_iter], 0, sizeof(output_n[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < nodes->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != nodes->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Value *node_cam = _get_value_by_key(
        &nodes->values[i].object, "camera", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *node_children = _get_value_by_key(
        &nodes->values[i].object, "children", FPX_JSON_VALUE_ARRAY);
    Fpx_Json_Value *node_skin = _get_value_by_key(
        &nodes->values[i].object, "skin", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *node_matrix = _get_value_by_key(
        &nodes->values[i].object, "matrix", FPX_JSON_VALUE_ARRAY);
    Fpx_Json_Value *node_mesh = _get_value_by_key(
        &nodes->values[i].object, "mesh", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *node_rot = _get_value_by_key(
        &nodes->values[i].object, "rotation", FPX_JSON_VALUE_ARRAY);
    Fpx_Json_Value *node_scale = _get_value_by_key(
        &nodes->values[i].object, "scale", FPX_JSON_VALUE_ARRAY);
    Fpx_Json_Value *node_translation = _get_value_by_key(
        &nodes->values[i].object, "translation", FPX_JSON_VALUE_ARRAY);
    Fpx_Json_Value *node_weights = _get_value_by_key(
        &nodes->values[i].object, "weights", FPX_JSON_VALUE_ARRAY);
    Fpx_Json_Value *node_name = _get_value_by_key(
        &nodes->values[i].object, "name", FPX_JSON_VALUE_STRING);

    if (NULL != node_cam) {
      // handle camera index
      if (NULL == output->cameras)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      size_t cam_offset = (size_t)node_cam->number;

      if (output->cameraCount <= cam_offset)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_n[i].camera = output->cameras + cam_offset;
    }
    if (NULL != node_children) {
      // handle children array
      {
        size_t temp = 0;
        Fpx3d_E_Result alloc_res = __fpx3d_realloc_array(
            (void **)&output_n[i].children, sizeof(Fpx3d_Model_GltfNode *),
            node_children->array.count, &temp);

        if (FPX3D_SUCCESS > alloc_res)
          PARSE_FAIL(alloc_res);
      }

      Fpx_Json_Value *child_idxs = node_children->array.values;

      for (size_t iter = 0; iter < node_children->array.count; ++iter) {
        if (FPX_JSON_VALUE_NUMBER != child_idxs[iter].valueType ||
            (size_t)child_idxs[iter].number > output->nodeCount)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_n[i].children[iter] =
            output->nodes + (size_t)child_idxs[iter].number;
      }
    }
    if (NULL != node_skin) {
      // handle node skin
      if (NULL == output->skins)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      size_t skin_offset = (size_t)node_skin->number;

      if (output->skinCount <= skin_offset)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_n[i].skin = output->skins + skin_offset;
    }
    if (NULL != node_matrix) {
      // handle transformation matrix
      for (size_t iter = 0; iter < node_matrix->array.count; ++iter) {
        if (FPX_JSON_VALUE_NUMBER != node_matrix->array.values[iter].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_n[i].matrix[0][iter] =
            (float)node_matrix->array.values[iter].number;
      }
    }
    if (NULL != node_mesh) {
      // handle mesh
      if (NULL == output->meshes)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      size_t mesh_offset = (size_t)node_mesh->number;

      if (output->meshCount <= mesh_offset)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_n[i].mesh = output->meshes + mesh_offset;
    }
    if (NULL != node_rot) {
      // handle rotation
      for (size_t iter = 0; iter < node_rot->array.count; ++iter) {
        if (FPX_JSON_VALUE_NUMBER != node_rot->array.values[iter].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_n[i].rotationQuat[iter] =
            (float)node_rot->array.values[iter].number;
      }
    }
    if (NULL != node_scale) {
      // handle scale
      for (size_t iter = 0; iter < node_scale->array.count; ++iter) {
        if (FPX_JSON_VALUE_NUMBER != node_scale->array.values[iter].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_n[i].scale[iter] = (float)node_scale->array.values[iter].number;
      }
    }
    if (NULL != node_translation) {
      // handle translation
      for (size_t iter = 0; iter < node_translation->array.count; ++iter) {
        if (FPX_JSON_VALUE_NUMBER !=
            node_translation->array.values[iter].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_n[i].translation[iter] =
            (float)node_translation->array.values[iter].number;
      }
    }
    // if weights exist, mesh must exist as well
    if (NULL != node_weights && NULL != node_mesh) {
      // handle mesh weights
      {
        Fpx3d_E_Result alloc_res = __fpx3d_realloc_array(
            (void **)&output_n[i].meshMorphTargetWeights, sizeof(float),
            node_weights->array.count, &output_n[i].weightCount);

        if (FPX3D_SUCCESS > alloc_res)
          PARSE_FAIL(alloc_res);
      }

      for (size_t iter = 0; iter < node_weights->array.count; ++iter) {
        if (FPX_JSON_VALUE_NUMBER != node_weights->array.values[i].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_n[i].meshMorphTargetWeights[iter] =
            (float)node_weights->array.values[iter].number;
      }
    }
    if (NULL != node_name) {
      // handle node name
      size_t temp_cap = 0;

      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_n[i].name, 1, node_name->string.size + 1, &temp_cap);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_n[i].name, node_name->string.data,
             node_name->string.size + 1);
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _parse_primitive_attributes(
    struct fpx3d_model_gltf_primitive_attribute *attributes_output,
    Fpx_Json_Value *json_obj, Fpx3d_Model_GltfAssetDescription *parent_asset) {
  NULL_CHECK(attributes_output, FPX3D_ARGS_ERROR);
  NULL_CHECK(json_obj, FPX3D_ARGS_ERROR);
  NULL_CHECK(parent_asset, FPX3D_ARGS_ERROR);

  for (size_t attr = 0; attr < json_obj->object.memberCount; ++attr) {
    Fpx_Json_Member *attr_kv = &json_obj->object.members[attr];

    if (FPX_JSON_VALUE_NUMBER != attr_kv->value->valueType ||
        attr_kv->value->number >= parent_asset->accessorCount)
      return (FPX3D_MODEL_INVALID_FILE_ERROR);

    attributes_output[attr].accessor =
        parent_asset->accessors + (size_t)attr_kv->value->number;

    if (0 == strcmp("POSITION", attr_kv->key.data))
      attributes_output[attr].attribute = FPX3D_GLTF_MESH_ATTRIBUTE_POSITION;
    else if (0 == strcmp("NORMAL", attr_kv->key.data))
      attributes_output[attr].attribute = FPX3D_GLTF_MESH_ATTRIBUTE_NORMAL;
    else if (0 == strcmp("TANGENT", attr_kv->key.data))
      attributes_output[attr].attribute = FPX3D_GLTF_MESH_ATTRIBUTE_TANGENT;
    else if (0 == strncmp("TEXCOORD", attr_kv->key.data, 8)) {
      attributes_output[attr].attribute = FPX3D_GLTF_MESH_ATTRIBUTE_TEXCOORD;
      attributes_output[attr].n =
          fpx_strint(attr_kv->key.data + strlen("TEXCOORD") + 1);
    } else if (0 == strncmp("COLOR", attr_kv->key.data, 8)) {
      attributes_output[attr].attribute = FPX3D_GLTF_MESH_ATTRIBUTE_COLOR;
      attributes_output[attr].n =
          fpx_strint(attr_kv->key.data + strlen("COLOR") + 1);
    } else if (0 == strncmp("JOINTS", attr_kv->key.data, 8)) {
      attributes_output[attr].attribute = FPX3D_GLTF_MESH_ATTRIBUTE_JOINTS;
      attributes_output[attr].n =
          fpx_strint(attr_kv->key.data + strlen("JOINTS") + 1);
    } else if (0 == strncmp("WEIGHTS", attr_kv->key.data, 8)) {
      attributes_output[attr].attribute = FPX3D_GLTF_MESH_ATTRIBUTE_WEIGHTS;
      attributes_output[attr].n =
          fpx_strint(attr_kv->key.data + strlen("WEIGHTS") + 1);
    }
  }

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_parse_mesh_primitives(Fpx_Json_Array *primitives,
                       struct fpx3d_model_gltf_mesh_primitive *outputs,
                       Fpx3d_Model_GltfAssetDescription *parent_asset) {
  NULL_CHECK(primitives, FPX3D_ARGS_ERROR);
  NULL_CHECK(outputs, FPX3D_ARGS_ERROR);
  NULL_CHECK(parent_asset, FPX3D_ARGS_ERROR);

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(outputs[_iter].attributes);                                    \
      for (size_t _another_iter = 0;                                           \
           _another_iter < outputs[_iter].morphTargetCount; ++_another_iter) { \
        FREE_SAFE(outputs[_iter].morphTargets[_another_iter].attributes);      \
      }                                                                        \
      FREE_SAFE(outputs[_iter].morphTargets);                                  \
      memset(&outputs[_iter], 0, sizeof(outputs[_iter]));                      \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < primitives->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != primitives->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    {
      // .attributes
      Fpx_Json_Value *attr_obj = _get_value_by_key(
          &primitives->values[i].object, "attributes", FPX_JSON_VALUE_OBJECT);
      if (NULL == attr_obj)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      {
        Fpx3d_E_Result alloc_res = __fpx3d_realloc_array(
            (void **)&outputs[i].attributes,
            sizeof(struct fpx3d_model_gltf_primitive_attribute),
            attr_obj->object.memberCount, &outputs[i].attributeCount);

        if (FPX3D_SUCCESS < alloc_res)
          PARSE_FAIL(alloc_res);
      }

      Fpx3d_E_Result prim_res = _parse_primitive_attributes(
          outputs[i].attributes, attr_obj, parent_asset);

      if (FPX3D_SUCCESS > prim_res)
        PARSE_FAIL(prim_res);
    }

    {
      // .indices
      Fpx_Json_Value *ind_value = _get_value_by_key(
          &primitives->values[i].object, "indices", FPX_JSON_VALUE_NUMBER);

      if (NULL != ind_value) {
        if (NULL == parent_asset->accessors)
          PARSE_FAIL(FPX3D_NULLPTR_ERROR);

        if ((size_t)ind_value->number >= parent_asset->accessorCount)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        outputs[i].indices =
            parent_asset->accessors + (size_t)ind_value->number;
      }
    }

    {
      // .material
      Fpx_Json_Value *mat_value = _get_value_by_key(
          &primitives->values[i].object, "material", FPX_JSON_VALUE_NUMBER);

      if (NULL != mat_value) {
        if (NULL == parent_asset->materials)
          PARSE_FAIL(FPX3D_NULLPTR_ERROR);

        if ((size_t)mat_value->number >= parent_asset->materialCount)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        outputs[i].material =
            parent_asset->materials + (size_t)mat_value->number;
      }
    }

    {
      // .renderMode
      Fpx_Json_Value *mode_value = _get_value_by_key(
          &primitives->values[i].object, "mode", FPX_JSON_VALUE_NUMBER);

      if (NULL != mode_value) {
        outputs[i].renderMode = (size_t)mode_value->number;
      }
    }

    {
      // .morphTargets
      Fpx_Json_Value *targets_value = _get_value_by_key(
          &primitives->values[i].object, "targets", FPX_JSON_VALUE_ARRAY);

      if (NULL != targets_value) {
        Fpx3d_E_Result target_alloc_res = __fpx3d_realloc_array(
            (void **)&outputs[i].morphTargets,
            sizeof(outputs[i].morphTargets[0]), targets_value->array.count,
            &outputs[i].morphTargetCount);

        if (FPX3D_SUCCESS > target_alloc_res)
          PARSE_FAIL(target_alloc_res);

        for (size_t iter = 0; iter < targets_value->array.count; ++iter) {
          Fpx_Json_Value *attr_obj = &targets_value->array.values[iter];

          if (FPX_JSON_VALUE_OBJECT != attr_obj->valueType)
            PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

          Fpx3d_E_Result attr_alloc_res = __fpx3d_realloc_array(
              (void **)&outputs[i].morphTargets[iter].attributes,
              sizeof(outputs[i].morphTargets[iter].attributes[0]),
              attr_obj->object.memberCount,
              &outputs[i].morphTargets[iter].attributeCount);

          if (FPX3D_SUCCESS > attr_alloc_res)
            PARSE_FAIL(attr_alloc_res);

          Fpx3d_E_Result prim_res = _parse_primitive_attributes(
              outputs[i].morphTargets[iter].attributes, attr_obj, parent_asset);

          if (FPX3D_SUCCESS > prim_res)
            PARSE_FAIL(prim_res);
        }
      }
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _parse_meshes(Fpx_Json_Array *meshes,
                                    Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(meshes, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->meshes, FPX3D_SUCCESS);

  Fpx3d_Model_GltfMesh *output_m = output->meshes;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_m[_iter].name);                                         \
      FREE_SAFE(output_m[_iter].morphTargetWeights);                           \
      FREE_SAFE(output_m[_iter].primitives);                                   \
      memset(&output_m[_iter], 0, sizeof(output_m[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < meshes->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != meshes->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Value *mesh_prims = _get_value_by_key(
        &meshes->values[i].object, "primitives", FPX_JSON_VALUE_ARRAY);

    if (NULL == mesh_prims) {
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);
    }

    {
      size_t temp = 0;
      Fpx3d_E_Result prim_alloc = __fpx3d_realloc_array(
          (void **)&output_m[i].primitives, sizeof(output_m[i].primitives[0]),
          mesh_prims->array.count, &temp);

      if (FPX3D_SUCCESS > prim_alloc)
        PARSE_FAIL(prim_alloc);

      // primitives
      Fpx3d_E_Result prim_res = _parse_mesh_primitives(
          &mesh_prims->array, output_m[i].primitives, output);

      if (FPX3D_SUCCESS > prim_res)
        PARSE_FAIL(prim_res);
    }

    Fpx_Json_Value *mesh_name = _get_value_by_key(
        &meshes->values[i].object, "name", FPX_JSON_VALUE_STRING);
    Fpx_Json_Value *mesh_weights = _get_value_by_key(
        &meshes->values[i].object, "weights", FPX_JSON_VALUE_ARRAY);

    if (NULL != mesh_name) {
      size_t temp_cap = 0;

      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_m[i].name, 1, mesh_name->string.size + 1, &temp_cap);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_m[i].name, mesh_name->string.data,
             mesh_name->string.size + 1);
    }

    if (NULL != mesh_weights) {
      {
        Fpx3d_E_Result alloc_res = __fpx3d_realloc_array(
            (void **)&output_m[i].morphTargetWeights, sizeof(float),
            mesh_weights->array.count, &output_m[i].weightCount);

        if (FPX3D_SUCCESS > alloc_res)
          PARSE_FAIL(alloc_res);
      }

      for (size_t iter = 0; iter < mesh_weights->array.count; ++iter) {
        if (FPX_JSON_VALUE_NUMBER != mesh_weights->array.values[i].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_m[i].morphTargetWeights[iter] =
            (float)mesh_weights->array.values[iter].number;
      }
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _parse_buffers(Fpx_Json_Array *buffers,
                                     Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(buffers, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->buffers, FPX3D_SUCCESS);

  Fpx3d_Model_GltfBuffer *output_b = output->buffers;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_b[_iter].name);                                         \
      FREE_SAFE(output_b[_iter].uri);                                          \
      memset(&output_b[_iter], 0, sizeof(output_b[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < buffers->count; ++i) {
    Fpx_Json_Value *buf = &buffers->values[i];

    if (FPX_JSON_VALUE_OBJECT != buf->valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Value *data_len =
        _get_value_by_key(&buf->object, "byteLength", FPX_JSON_VALUE_NUMBER);

    if (NULL == data_len)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    output_b[i].dataLength = (size_t)data_len->number;

    Fpx_Json_Value *uri =
        _get_value_by_key(&buf->object, "uri", FPX_JSON_VALUE_STRING);
    Fpx_Json_Value *name =
        _get_value_by_key(&buf->object, "name", FPX_JSON_VALUE_STRING);

    if (NULL != uri) {
      size_t temp = 0;
      Fpx3d_E_Result uri_alloc = __fpx3d_realloc_array(
          (void **)&output_b[i].uri, 1, uri->string.size + 1, &temp);

      if (FPX3D_SUCCESS > uri_alloc)
        PARSE_FAIL(uri_alloc);

      memcpy(output_b[i].uri, uri->string.data, temp);
    }

    if (NULL != name) {
      size_t temp = 0;
      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_b[i].name, 1, name->string.size + 1, &temp);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_b[i].name, name->string.data, temp);
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_parse_buffer_views(Fpx_Json_Array *views,
                    Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(views, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->bufferViews, FPX3D_SUCCESS);
  NULL_CHECK(output->buffers, FPX3D_NULLPTR_ERROR);

  Fpx3d_Model_GltfBufferView *output_v = output->bufferViews;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_v[_iter].name);                                         \
      memset(&output_v[_iter], 0, sizeof(output_v[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < views->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != views->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Value *buf_idx = _get_value_by_key(
        &views->values[i].object, "buffer", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *view_len = _get_value_by_key(
        &views->values[i].object, "byteLength", FPX_JSON_VALUE_NUMBER);

    if (NULL == buf_idx || NULL == view_len)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    if ((size_t)buf_idx->number >= output->bufferCount)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    output_v[i].buffer = output->buffers + (size_t)buf_idx->number;
    output_v[i].byteLength = (size_t)view_len->number;

    Fpx_Json_Value *offset = _get_value_by_key(
        &views->values[i].object, "byteOffset", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *stride = _get_value_by_key(
        &views->values[i].object, "byteStride", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *target = _get_value_by_key(&views->values[i].object,
                                               "target", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *name = _get_value_by_key(&views->values[i].object, "name",
                                             FPX_JSON_VALUE_STRING);

    if (NULL != offset) {
      output_v[i].byteOffset = (size_t)offset->number;
    }
    if (NULL != stride) {
      output_v[i].byteStride = (size_t)stride->number;
    }
    if (NULL != target) {
      output_v[i].target = (size_t)target->number;
    }
    if (NULL != name) {
      size_t temp = 0;
      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_v[i].name, 1, name->string.size + 1, &temp);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_v[i].name, name->string.data, temp);
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_parse_accessors(Fpx_Json_Array *accessors,
                 Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(accessors, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->accessors, FPX3D_SUCCESS);

  Fpx3d_Model_GltfAccessor *output_a = output->accessors;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_a[_iter].name);                                         \
      memset(&output_a[_iter], 0, sizeof(output_a[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < accessors->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != accessors->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Object *acc = &accessors->values[i].object;

    Fpx_Json_Value *comp_type =
        _get_value_by_key(acc, "componentType", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *ele_type =
        _get_value_by_key(acc, "type", FPX_JSON_VALUE_STRING);
    Fpx_Json_Value *ele_count =
        _get_value_by_key(acc, "count", FPX_JSON_VALUE_NUMBER);

    if (NULL == comp_type || NULL == ele_type || NULL == ele_count)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    output_a[i].componentType = (size_t)comp_type->number;
    output_a[i].elementCount = (size_t)ele_count->number;

    if (0 == strcmp("SCALAR", ele_type->string.data))
      output_a[i].elementType = FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_SCALAR;
    else if (0 == strcmp("VEC2", ele_type->string.data))
      output_a[i].elementType = FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_VEC2;
    else if (0 == strcmp("VEC3", ele_type->string.data))
      output_a[i].elementType = FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_VEC3;
    else if (0 == strcmp("VEC4", ele_type->string.data))
      output_a[i].elementType = FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_VEC4;
    else if (0 == strcmp("MAT2", ele_type->string.data))
      output_a[i].elementType = FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_MAT2;
    else if (0 == strcmp("MAT3", ele_type->string.data))
      output_a[i].elementType = FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_MAT3;
    else if (0 == strcmp("MAT4", ele_type->string.data))
      output_a[i].elementType = FPX3D_GLTF_ACCESSOR_ELEMENT_TYPE_MAT4;

    Fpx_Json_Value *buf_view =
        _get_value_by_key(acc, "bufferView", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *offset =
        _get_value_by_key(acc, "byteOffset", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *normalized =
        _get_value_by_key(acc, "normalized", FPX_JSON_VALUE_BOOL);
    Fpx_Json_Value *max_vals =
        _get_value_by_key(acc, "max", FPX_JSON_VALUE_ARRAY);
    Fpx_Json_Value *min_vals =
        _get_value_by_key(acc, "min", FPX_JSON_VALUE_ARRAY);
    Fpx_Json_Value *sparse =
        _get_value_by_key(acc, "sparse", FPX_JSON_VALUE_OBJECT);
    Fpx_Json_Value *name =
        _get_value_by_key(acc, "name", FPX_JSON_VALUE_STRING);

    if (NULL != buf_view) {
      if (NULL == output->bufferViews)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      if ((size_t)buf_view->number >= output->bufferViewCount)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_a[i].view = output->bufferViews + (size_t)buf_view->number;
    }
    if (NULL != offset) {
      output_a[i].byteOffset = (size_t)offset->number;
    }
    if (NULL != normalized) {
      output_a[i].componentsNormalized = normalized->boolean;
    }
    if (NULL != max_vals) {
      if (max_vals->array.count > 15)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      for (size_t iter = 0; iter < max_vals->array.count; ++iter) {
        if (FPX_JSON_VALUE_NUMBER != max_vals->array.values[iter].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_a[i].maxValues.matrix4[0][iter] =
            max_vals->array.values[iter].number;
      }
    }
    if (NULL != min_vals) {
      if (min_vals->array.count > 15)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      for (size_t iter = 0; iter < min_vals->array.count; ++iter) {
        if (FPX_JSON_VALUE_NUMBER != min_vals->array.values[iter].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_a[i].minValues.matrix4[0][iter] =
            min_vals->array.values[iter].number;
      }
    }
    if (NULL != sparse) {
      Fpx_Json_Object *sparse_obj = &sparse->object;

      {
        // .count
        Fpx_Json_Value *sparse_count =
            _get_value_by_key(sparse_obj, "count", FPX_JSON_VALUE_NUMBER);
        if (NULL == sparse_count)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_a[i].sparse.count = (size_t)sparse_count->number;
      }

      {
        // .indices
        Fpx_Json_Value *sparse_indices =
            _get_value_by_key(sparse_obj, "indices", FPX_JSON_VALUE_OBJECT);

        if (NULL == sparse_indices)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        Fpx_Json_Value *s_ind_view = _get_value_by_key(
            &sparse_indices->object, "bufferView", FPX_JSON_VALUE_NUMBER);
        Fpx_Json_Value *s_ind_type = _get_value_by_key(
            &sparse_indices->object, "componentType", FPX_JSON_VALUE_NUMBER);

        if (NULL == s_ind_view || NULL == s_ind_type)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        if ((size_t)s_ind_view->number >= output->bufferViewCount)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        Fpx3d_Model_GltfBufferView *bv =
            output->bufferViews + (size_t)s_ind_view->number;

        uint8_t alignment = 1;
        switch ((uint32_t)s_ind_type->number) {
        case FPX3D_GLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
          alignment = sizeof(uint8_t);
          break;
        case FPX3D_GLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
          alignment = sizeof(unsigned short);
          break;
        case FPX3D_GLTF_COMPONENT_TYPE_UNSIGNED_INT:
          alignment = sizeof(unsigned int);
          break;

        default:
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);
        }

        if (bv->byteStride != 0 ||
            bv->target != FPX3D_GLTF_BUFFER_VIEW_TARGET_INVALID ||
            bv->byteLength % alignment != 0)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_a[i].sparse.indices.componentType = (size_t)s_ind_type->number;
        output_a[i].sparse.indices.view = bv;

        Fpx_Json_Value *offset = _get_value_by_key(
            &sparse_indices->object, "byteOffset", FPX_JSON_VALUE_NUMBER);
        if (NULL != offset) {
          if ((size_t)offset->number % alignment != 0)
            PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

          output_a[i].sparse.indices.byteOffset = (size_t)offset->number;
        }
      }

      {
        // .values
        Fpx_Json_Value *sparse_values =
            _get_value_by_key(sparse_obj, "values", FPX_JSON_VALUE_OBJECT);

        if (NULL == sparse_values)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        Fpx_Json_Value *s_val_view = _get_value_by_key(
            &sparse_values->object, "bufferView", FPX_JSON_VALUE_NUMBER);

        if (NULL == s_val_view)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        if ((size_t)s_val_view->number >= output->bufferViewCount)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        Fpx3d_Model_GltfBufferView *bv =
            output->bufferViews + (size_t)s_val_view->number;

        if (bv->byteStride != 0 ||
            bv->target != FPX3D_GLTF_BUFFER_VIEW_TARGET_INVALID)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_a[i].sparse.indices.view = bv;

        Fpx_Json_Value *offset = _get_value_by_key(
            &sparse_values->object, "byteOffset", FPX_JSON_VALUE_NUMBER);
        if (NULL != offset) {
          output_a[i].sparse.indices.byteOffset = (size_t)offset->number;
        }
      }
    }
    if (NULL != name) {
      size_t temp = 0;
      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_a[i].name, 1, name->string.size + 1, &temp);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_a[i].name, name->string.data, temp);
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _parse_images(Fpx_Json_Array *images,
                                    Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(images, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->images, FPX3D_SUCCESS);

  Fpx3d_Model_GltfImage *output_i = output->images;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_i[_iter].name);                                         \
      FREE_SAFE(output_i[_iter].uri);                                          \
      FREE_SAFE(output_i[_iter].mimeType);                                     \
      memset(&output_i[_iter], 0, sizeof(output_i[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < images->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != images->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Object *image = &images->values[i].object;

    Fpx_Json_Value *uri =
        _get_value_by_key(image, "uri", FPX_JSON_VALUE_STRING);
    Fpx_Json_Value *mime =
        _get_value_by_key(image, "mimeType", FPX_JSON_VALUE_STRING);
    Fpx_Json_Value *view =
        _get_value_by_key(image, "bufferView", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *name =
        _get_value_by_key(image, "name", FPX_JSON_VALUE_STRING);

    if (NULL != uri) {
      size_t temp = 0;
      Fpx3d_E_Result uri_alloc = __fpx3d_realloc_array(
          (void **)&output_i[i].uri, 1, uri->string.size + 1, &temp);

      if (FPX3D_SUCCESS > uri_alloc)
        PARSE_FAIL(uri_alloc);

      memcpy(output_i[i].uri, uri->string.data, temp);
    }
    if (NULL != mime) {
      size_t temp = 0;
      Fpx3d_E_Result mime_alloc = __fpx3d_realloc_array(
          (void **)&output_i[i].mimeType, 1, mime->string.size + 1, &temp);

      if (FPX3D_SUCCESS > mime_alloc)
        PARSE_FAIL(mime_alloc);

      memcpy(output_i[i].mimeType, mime->string.data, temp);
    }
    if (NULL != view) {
      if (NULL == output->bufferViews)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      if (NULL != uri || NULL == mime)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      if ((size_t)view->number >= output->bufferViewCount)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_i[i].bufferView = output->bufferViews + (size_t)view->number;
    }
    if (NULL != name) {
      size_t temp = 0;
      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_i[i].name, 1, name->string.size + 1, &temp);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_i[i].name, name->string.data, temp);
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_parse_samplers(Fpx_Json_Array *samplers,
                Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(samplers, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->samplers, FPX3D_SUCCESS);

  Fpx3d_Model_GltfSampler *output_s = output->samplers;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_s[_iter].name);                                         \
      memset(&output_s[_iter], 0, sizeof(output_s[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < samplers->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != samplers->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Value *mag = _get_value_by_key(&samplers->values[i].object,
                                            "magFilter", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *min = _get_value_by_key(&samplers->values[i].object,
                                            "magFilter", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *wrapU = _get_value_by_key(&samplers->values[i].object,
                                              "wrapS", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *wrapV = _get_value_by_key(&samplers->values[i].object,
                                              "wrapT", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *name = _get_value_by_key(&samplers->values[i].object,
                                             "name", FPX_JSON_VALUE_STRING);

    if (NULL != mag) {
      output_s[i].magFilter = (uint32_t)mag->number;
    }
    if (NULL != min) {
      output_s[i].minFilter = (uint32_t)min->number;
    }
    if (NULL != wrapU) {
      output_s[i].wrapU = (uint32_t)wrapU->number;
    } else {
      output_s[i].wrapU = FPX3D_GLTF_SAMPLER_WRAP_REPEAT;
    }
    if (NULL != wrapV) {
      output_s[i].wrapV = (uint32_t)wrapV->number;
    } else {
      output_s[i].wrapV = FPX3D_GLTF_SAMPLER_WRAP_REPEAT;
    }
    if (NULL != name) {
      size_t temp = 0;
      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_s[i].name, 1, name->string.size + 1, &temp);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_s[i].name, name->string.data, temp);
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_parse_textures(Fpx_Json_Array *textures,
                Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(textures, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->textures, FPX3D_SUCCESS);

  Fpx3d_Model_GltfTexture *output_t = output->textures;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_t[_iter].name);                                         \
      memset(&output_t[_iter], 0, sizeof(output_t[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < textures->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != textures->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Value *sampler = _get_value_by_key(
        &textures->values[i].object, "sampler", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *source = _get_value_by_key(&textures->values[i].object,
                                               "source", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *name = _get_value_by_key(&textures->values[i].object,
                                             "name", FPX_JSON_VALUE_STRING);

    if (NULL != sampler) {
      if (NULL == output->samplers)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      if ((size_t)sampler->number >= output->samplerCount)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_t[i].sampler = output->samplers + (size_t)sampler->number;
    }
    if (NULL != source) {
      if (NULL == output->images)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      if ((size_t)source->number >= output->imageCount)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_t[i].sourceImage = output->images + (size_t)source->number;
    }
    if (NULL != name) {
      size_t temp = 0;
      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_t[i].name, 1, name->string.size + 1, &temp);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_t[i].name, name->string.data, temp);
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_parse_tex_info(Fpx_Json_Object *texture_info,
                struct fpx3d_model_gltf_texture_info *output,
                Fpx3d_Model_GltfAssetDescription *asset) {
  NULL_CHECK(texture_info, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);
  NULL_CHECK(asset, FPX3D_ARGS_ERROR);

  NULL_CHECK(asset->textures, FPX3D_MODEL_INVALID_FILE_ERROR);

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    memset(output, 0, sizeof(*output));                                        \
    return retval;                                                             \
  }

  Fpx_Json_Value *txt_index =
      _get_value_by_key(texture_info, "index", FPX_JSON_VALUE_NUMBER);

  if (NULL != txt_index) {
    if ((size_t)txt_index->number >= asset->textureCount ||
        NULL == asset->textures)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    output->texture = asset->textures + (size_t)txt_index->number;
  } else {
    PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);
  }

  Fpx_Json_Value *tex_coord =
      _get_value_by_key(texture_info, "texCoord", FPX_JSON_VALUE_NUMBER);

  if (NULL != tex_coord) {
    output->texCoordIndex = (size_t)tex_coord->number;
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_parse_materials(Fpx_Json_Array *materials,
                 Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(materials, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->materials, FPX3D_SUCCESS);

  Fpx3d_Model_GltfMaterial *output_m = output->materials;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_m[_iter].name);                                         \
      memset(&output_m[_iter], 0, sizeof(output_m[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < materials->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != materials->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Object *mat_obj = &materials->values[i].object;

    Fpx_Json_Value *name =
        _get_value_by_key(mat_obj, "name", FPX_JSON_VALUE_STRING);
    Fpx_Json_Value *pbr = _get_value_by_key(mat_obj, "pbrMetallicRoughness",
                                            FPX_JSON_VALUE_OBJECT);
    Fpx_Json_Value *normal =
        _get_value_by_key(mat_obj, "normalTexture", FPX_JSON_VALUE_OBJECT);
    Fpx_Json_Value *occlusion =
        _get_value_by_key(mat_obj, "occlusionTexture", FPX_JSON_VALUE_OBJECT);
    Fpx_Json_Value *emissive =
        _get_value_by_key(mat_obj, "emissiveTexture", FPX_JSON_VALUE_OBJECT);
    Fpx_Json_Value *emissive_factor =
        _get_value_by_key(mat_obj, "emissiveFactor", FPX_JSON_VALUE_ARRAY);
    Fpx_Json_Value *alpha_mode =
        _get_value_by_key(mat_obj, "alphaMode", FPX_JSON_VALUE_STRING);
    Fpx_Json_Value *alpha_cutoff =
        _get_value_by_key(mat_obj, "alphaCutoff", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *double_sided =
        _get_value_by_key(mat_obj, "doubleSided", FPX_JSON_VALUE_BOOL);

    // .name
    if (NULL != name) {
      size_t temp = 0;
      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_m[i].name, 1, name->string.size + 1, &temp);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_m[i].name, name->string.data, temp);
    }

    // .pbrMetallicRoughness
    if (NULL != pbr) {
      Fpx_Json_Object *pbr_obj = &pbr->object;
      Fpx_Json_Value *base_factors =
          _get_value_by_key(pbr_obj, "baseColorFactor", FPX_JSON_VALUE_ARRAY);
      Fpx_Json_Value *base_color_tex =
          _get_value_by_key(pbr_obj, "baseColorTexture", FPX_JSON_VALUE_OBJECT);
      Fpx_Json_Value *metal =
          _get_value_by_key(pbr_obj, "metallicFactor", FPX_JSON_VALUE_NUMBER);
      Fpx_Json_Value *rough =
          _get_value_by_key(pbr_obj, "roughnessFactor", FPX_JSON_VALUE_NUMBER);
      Fpx_Json_Value *metal_rough_tex = _get_value_by_key(
          pbr_obj, "metallicRoughnessTexture", FPX_JSON_VALUE_OBJECT);

      if (NULL != base_factors && base_factors->array.count >= 4) {
        for (size_t factor = 0; factor < 4; ++factor) {
          if (base_factors->array.values[factor].valueType !=
              FPX_JSON_VALUE_NUMBER)
            PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

          output_m[i].pbrMetallicRoughness.baseColorFactor[factor] =
              (float)base_factors->array.values[factor].number;
        }
      }

      if (NULL != metal) {
        output_m[i].pbrMetallicRoughness.metallicFactor = (float)metal->number;
      } else
        output_m[i].pbrMetallicRoughness.metallicFactor = 1.0f;

      if (NULL != rough) {
        output_m[i].pbrMetallicRoughness.roughnessFactor = (float)rough->number;
      } else
        output_m[i].pbrMetallicRoughness.roughnessFactor = 1.0f;

      if (NULL != base_color_tex) {
        if (NULL == output->textures)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        Fpx3d_E_Result tex_parse_res = _parse_tex_info(
            &base_color_tex->object,
            &output_m[i].pbrMetallicRoughness.baseColorTexture, output);
        if (FPX3D_SUCCESS > tex_parse_res)
          PARSE_FAIL(tex_parse_res);
      }

      if (NULL != metal_rough_tex) {
        if (NULL == output->textures)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        Fpx3d_E_Result tex_parse_res = _parse_tex_info(
            &metal_rough_tex->object,
            &output_m[i].pbrMetallicRoughness.metallicRoughnessTexture, output);
        if (FPX3D_SUCCESS > tex_parse_res)
          PARSE_FAIL(tex_parse_res);
      }
    }

    // .normalTexture
    if (NULL != normal) {
      if (NULL == output->textures)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      Fpx3d_E_Result normal_tex_result = _parse_tex_info(
          &normal->object, &output_m[i].normalTexture.textureInfo, output);

      if (FPX3D_SUCCESS > normal_tex_result)
        PARSE_FAIL(normal_tex_result);

      Fpx_Json_Value *scale =
          _get_value_by_key(&normal->object, "scale", FPX_JSON_VALUE_NUMBER);

      if (NULL != scale) {
        output_m[i].normalTexture.scale = (float)scale->number;
      } else
        output_m[i].normalTexture.scale = 1.0f;
    }

    // .occlusionTexture
    if (NULL != occlusion) {
      if (NULL == output->textures)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      Fpx3d_E_Result occlusion_tex_result =
          _parse_tex_info(&occlusion->object,
                          &output_m[i].occlusionTexture.textureInfo, output);

      if (FPX3D_SUCCESS > occlusion_tex_result)
        PARSE_FAIL(occlusion_tex_result);

      Fpx_Json_Value *scale =
          _get_value_by_key(&occlusion->object, "scale", FPX_JSON_VALUE_NUMBER);

      if (NULL != scale) {
        output_m[i].occlusionTexture.strength = (float)scale->number;
      } else
        output_m[i].occlusionTexture.strength = 1.0f;
    }

    // .emissiveTexture
    if (NULL != emissive) {
      if (NULL == output->textures)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      Fpx3d_E_Result tex_parse_res = _parse_tex_info(
          &emissive->object, &output_m[i].emissiveTexture, output);
      if (FPX3D_SUCCESS > tex_parse_res)
        PARSE_FAIL(tex_parse_res);
    }

    // .emissiveFactor
    if (NULL != emissive_factor) {
      if (emissive_factor->array.count < 3)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      for (size_t factor = 0; factor < 3; ++factor) {
        if (FPX_JSON_VALUE_NUMBER !=
            emissive_factor->array.values[factor].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_m[i].emissiveFactor[factor] =
            (float)emissive_factor->array.values[factor].number;
      }
    }

    // .alphaMode
    if (NULL != alpha_mode) {
      if (0 == strcmp("OPAQUE", alpha_mode->string.data))
        output_m[i].alphaMode = FPX3D_GLTF_ALPHA_MODE_OPAQUE;
      else if (0 == strcmp("MASK", alpha_mode->string.data))
        output_m[i].alphaMode = FPX3D_GLTF_ALPHA_MODE_MASK;
      else if (0 == strcmp("BLEND", alpha_mode->string.data))
        output_m[i].alphaMode = FPX3D_GLTF_ALPHA_MODE_BLEND;
    } else
      output_m[i].alphaMode = FPX3D_GLTF_ALPHA_MODE_OPAQUE;

    // .alphaCutoff
    if (NULL != alpha_cutoff) {
      output_m[i].alphaCutoff = (float)alpha_cutoff->number;
    } else
      output_m[i].alphaCutoff = 0.5f;

    // .doubleSided
    if (NULL != double_sided) {
      output_m[i].doubleSided = double_sided->boolean;
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result _parse_skins(Fpx_Json_Array *skins,
                                   Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(skins, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->skins, FPX3D_SUCCESS);
  NULL_CHECK(output->nodes, FPX3D_SUCCESS);

  Fpx3d_Model_GltfSkin *output_s = output->skins;

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_s[_iter].name);                                         \
      FREE_SAFE(output_s[_iter].joints);                                       \
      memset(&output_s[_iter], 0, sizeof(output_s[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  for (size_t i = 0; i < skins->count; ++i) {
    if (FPX_JSON_VALUE_OBJECT != skins->values[i].valueType)
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Object *skin = &skins->values[i].object;

    Fpx_Json_Value *joints =
        _get_value_by_key(skin, "joints", FPX_JSON_VALUE_ARRAY);

    if (NULL != joints && 0 < joints->array.count) {
      Fpx3d_E_Result joint_alloc = __fpx3d_realloc_array(
          (void **)&output_s[i].joints, sizeof(output_s[i].joints[0]),
          joints->array.count, &output_s[i].jointCount);

      if (FPX3D_SUCCESS > joint_alloc)
        PARSE_FAIL(joint_alloc);

      for (size_t joint = 0; joint < output_s[i].jointCount; ++joint) {
        if (FPX_JSON_VALUE_NUMBER != joints->array.values[joint].valueType)
          PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

        output_s[i].joints[joint] =
            output->nodes + (size_t)joints->array.values[joint].number;
      }
    } else
      PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

    Fpx_Json_Value *name =
        _get_value_by_key(skin, "name", FPX_JSON_VALUE_STRING);
    Fpx_Json_Value *inv_bind =
        _get_value_by_key(skin, "inverseBindMatrices", FPX_JSON_VALUE_NUMBER);
    Fpx_Json_Value *skeleton =
        _get_value_by_key(skin, "skeleton", FPX_JSON_VALUE_NUMBER);

    if (NULL != name) {
      size_t temp = 0;
      Fpx3d_E_Result name_alloc = __fpx3d_realloc_array(
          (void **)&output_s[i].name, 1, name->string.size + 1, &temp);

      if (FPX3D_SUCCESS > name_alloc)
        PARSE_FAIL(name_alloc);

      memcpy(output_s[i].name, name->string.data, temp);
    }

    if (NULL != inv_bind) {
      if (NULL == output->accessors)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      if ((size_t)inv_bind->number >= output->accessorCount)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_s[i].inverseBindMatrices =
          output->accessors + (size_t)inv_bind->number;
    }

    if (NULL != skeleton) {
      if ((size_t)skeleton->number >= output->nodeCount)
        PARSE_FAIL(FPX3D_MODEL_INVALID_FILE_ERROR);

      output_s[i].skeletonRoot = output->nodes + (size_t)skeleton->number;
    }
  }

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static Fpx3d_E_Result
_parse_animations(Fpx_Json_Array *animations,
                  Fpx3d_Model_GltfAssetDescription *output) {
  NULL_CHECK(animations, FPX3D_ARGS_ERROR);
  NULL_CHECK(output, FPX3D_ARGS_ERROR);

  NULL_CHECK(output->animations, FPX3D_SUCCESS);
  NULL_CHECK(output->accessors, FPX3D_NULLPTR_ERROR);
  NULL_CHECK(output->nodes, FPX3D_NULLPTR_ERROR);

  Fpx3d_Model_GltfAnimation *output_a = output->animations;
  UNUSED(output_a);

#define PARSE_FAIL(retval)                                                     \
  {                                                                            \
    for (size_t _iter = 0; _iter <= i; ++_iter) {                              \
      FREE_SAFE(output_a[_iter].name);                                         \
      FREE_SAFE(output_a[_iter].channels);                                     \
      FREE_SAFE(output_a[_iter].samplers);                                     \
      memset(&output_a[_iter], 0, sizeof(output_a[_iter]));                    \
    }                                                                          \
    return retval;                                                             \
  }

  FPX3D_TODO("Implement glTF animation parsing ( _parse_animations() )");

#undef PARSE_FAIL

  return FPX3D_SUCCESS;
}

static void _destroy_asset_desc(Fpx3d_Model_GltfAssetDescription *asset_desc) {
  NULL_CHECK(asset_desc, );

  // destroy scenes
  for (size_t i = 0; i < asset_desc->sceneCount; ++i) {
    FREE_SAFE(asset_desc->scenes[i].name);
    FREE_SAFE(asset_desc->scenes[i].nodes);
    memset(&asset_desc->scenes[i], 0, sizeof(asset_desc->scenes[i]));
  }

  // destroy cameras
  for (size_t i = 0; i < asset_desc->cameraCount; ++i) {
    FREE_SAFE(asset_desc->cameras[i].name);
    memset(&asset_desc->cameras[i], 0, sizeof(asset_desc->cameras[i]));
  }

  // destroy nodes
  for (size_t i = 0; i < asset_desc->nodeCount; ++i) {
    FREE_SAFE(asset_desc->nodes[i].name);
    FREE_SAFE(asset_desc->nodes[i].children);
    FREE_SAFE(asset_desc->nodes[i].meshMorphTargetWeights);
    memset(&asset_desc->nodes[i], 0, sizeof(asset_desc->nodes[i]));
  }

  // destroy meshes
  for (size_t i = 0; i < asset_desc->meshCount; ++i) {
    for (size_t j = 0; j < asset_desc->meshes[i].primitiveCount; ++j) {
      for (size_t k = 0;
           k < asset_desc->meshes[i].primitives[j].morphTargetCount; ++k) {
        FREE_SAFE(
            asset_desc->meshes[i].primitives[j].morphTargets[k].attributes);
        memset(&asset_desc->meshes[i].primitives[j].morphTargets[k], 0,
               sizeof(asset_desc->meshes[i].primitives[j].morphTargets[k]));
      }

      FREE_SAFE(asset_desc->meshes[i].primitives[j].attributes);
      FREE_SAFE(asset_desc->meshes[i].primitives[j].morphTargets);
      memset(&asset_desc->meshes[i].primitives[j], 0,
             sizeof(asset_desc->meshes[i].primitives[j]));
    }

    FREE_SAFE(asset_desc->meshes[i].name);
    FREE_SAFE(asset_desc->meshes[i].morphTargetWeights);
    FREE_SAFE(asset_desc->meshes[i].primitives);
    memset(&asset_desc->meshes[i], 0, sizeof(asset_desc->meshes[i]));
  }

  // destroy buffers
  for (size_t i = 0; i < asset_desc->bufferCount; ++i) {
    FREE_SAFE(asset_desc->buffers[i].name);
    FREE_SAFE(asset_desc->buffers[i].data);
    FREE_SAFE(asset_desc->buffers[i].uri);
    memset(&asset_desc->buffers[i], 0, sizeof(asset_desc->buffers[i]));
  }

  // destroy bufferViews
  for (size_t i = 0; i < asset_desc->bufferViewCount; ++i) {
    FREE_SAFE(asset_desc->bufferViews[i].name);
    memset(&asset_desc->bufferViews[i], 0, sizeof(asset_desc->bufferViews[i]));
  }

  // destroy accessors
  for (size_t i = 0; i < asset_desc->accessorCount; ++i) {
    FREE_SAFE(asset_desc->accessors[i].name);
    memset(&asset_desc->accessors[i], 0, sizeof(asset_desc->accessors[i]));
  }

  // destroy images
  for (size_t i = 0; i < asset_desc->imageCount; ++i) {
    FREE_SAFE(asset_desc->images[i].name);
    FREE_SAFE(asset_desc->images[i].mimeType);
    FREE_SAFE(asset_desc->images[i].uri);
    memset(&asset_desc->images[i], 0, sizeof(asset_desc->images[i]));
  }

  // destroy samplers
  for (size_t i = 0; i < asset_desc->samplerCount; ++i) {
    FREE_SAFE(asset_desc->samplers[i].name);
    memset(&asset_desc->samplers[i], 0, sizeof(asset_desc->samplers[i]));
  }

  // destroy textures
  for (size_t i = 0; i < asset_desc->textureCount; ++i) {
    FREE_SAFE(asset_desc->textures[i].name);
    memset(&asset_desc->textures[i], 0, sizeof(asset_desc->textures[i]));
  }

  // destroy materials
  for (size_t i = 0; i < asset_desc->materialCount; ++i) {
    FREE_SAFE(asset_desc->materials[i].name);
    memset(&asset_desc->materials[i], 0, sizeof(asset_desc->materials[i]));
  }

  // destroy skins
  for (size_t i = 0; i < asset_desc->skinCount; ++i) {
    FREE_SAFE(asset_desc->skins[i].name);
    FREE_SAFE(asset_desc->skins[i].joints);
    memset(&asset_desc->skins[i], 0, sizeof(asset_desc->skins[i]));
  }

  // destroy animations
  for (size_t i = 0; i < asset_desc->animationCount; ++i) {
    FREE_SAFE(asset_desc->animations[i].name);
    FREE_SAFE(asset_desc->animations[i].channels);
    FREE_SAFE(asset_desc->animations[i].samplers);
    memset(&asset_desc->animations[i], 0, sizeof(asset_desc->animations[i]));
  }

  _free_top_level(asset_desc);
  return;
}

static void _destroy_chunk(struct fpx3d_model_glb_chunk *chunkptr) {
  NULL_CHECK(chunkptr, );

  switch (chunkptr->type) {
  case FPX3D_GLB_CHUNK_JSON:
    _destroy_asset_desc(&chunkptr->json);
    memset(chunkptr, 0, sizeof(*chunkptr));
    break;

  case FPX3D_GLB_CHUNK_BINARY:
    break;

  default:
    break;
  }

  return;
}
