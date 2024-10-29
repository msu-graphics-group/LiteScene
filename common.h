#ifndef LOADER_UTILS_COMMON_H
#define LOADER_UTILS_COMMON_H

#include "LiteMath.h"
#include "cmesh4.h"
#include "pugixml.hpp"

using LiteMath::uint2;
using LiteMath::float2;
using LiteMath::float3;
using LiteMath::float4;
using LiteMath::float4x4;
using LiteMath::make_float2;
using LiteMath::make_float4;

typedef unsigned int uint;
typedef uint2        uvec2;
typedef float4       vec4;
typedef float3       vec3;
typedef float2       vec2;
typedef float4x4     mat4;

struct UniformParams
{
  mat4  lightMatrix;
  vec4  lightPos;
  vec4  baseColor;
  float time;
  bool animateLightColor;
};

struct MaterialDataInternal
{
  vec4 baseColor;

  float metallic;
  float roughness;
  int baseColorTexId;
  int metallicRoughnessTexId;

  vec3 emissionColor;
  int emissionTexId;

  int normalTexId;
  int occlusionTexId;
  float alphaCutoff;
  int alphaMode;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Instance
{
  uint32_t           instId = uint32_t(-1);
  uint32_t           geomId = uint32_t(-1); ///< geom id
  uint32_t           rmapId = uint32_t(-1); ///< remap list id, todo: add function to get real remap list by id
  uint32_t           lightInstId = uint32_t(-1);
  LiteMath::float4x4 matrix;                ///< transform matrix
  LiteMath::float4x4 matrix_motion;         ///< transform matrix at the end of motion
  bool               hasMotion = false;    ///< is this instance moving? (i.e has meaningful motion matrix)
  pugi::xml_node     node;
};

struct LightInstance
{
  uint32_t           instId  = uint32_t(-1);
  uint32_t           lightId = uint32_t(-1);
  pugi::xml_node     instNode;
  pugi::xml_node     lightNode;
  LiteMath::float4x4 matrix;
  pugi::xml_node     node;
};

struct Camera
{
  float pos[3];
  float lookAt[3];
  float up[3];
  float fov;
  float nearPlane;
  float farPlane;
  float exposureMult;
  pugi::xml_node node;
  LiteMath::float4x4 matrix;  // view matrix
  bool has_matrix;
};

struct Settings
{
  uint32_t width;
  uint32_t height;
  uint32_t spp;
  uint32_t depth;
  uint32_t depthDiffuse;
  pugi::xml_node node;
};

#endif