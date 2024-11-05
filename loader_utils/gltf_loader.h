#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include <LiteMath.h>
#include "../cmesh4.h"
#include "../common.h"

#if defined(__ANDROID__)
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#include <android/asset_manager.h>
#endif

#include "tiny_gltf.h"
#include "Image2d.h"


namespace gltf_loader
{
  using LiteMath::Box4f;
  using LiteMath::BBox3f;
  using LiteMath::DEG_TO_RAD;
  using LiteMath::float3;
  using LiteMath::float4;
  using LiteMath::perspectiveMatrix;

  void getNumVerticesAndIndicesFromGLTFMesh(const tinygltf::Model &a_model, const tinygltf::Mesh &a_mesh,
                                            uint32_t& numVertices, uint32_t& numIndices);

  cmesh4::SimpleMesh  simpleMeshFromGLTFMesh(const tinygltf::Model &a_model, const tinygltf::Mesh &a_mesh);

  LiteMath::float4x4 transformMatrixFromGLTFNode(const tinygltf::Node &node);

  MaterialDataInternal materialDataFromGLTF(const tinygltf::Material &gltfMat, const std::vector<tinygltf::Texture> &gltfTextures);

  //LiteImage::Sampler samplerFromGLTF(const tinygltf::Sampler &gltfSampler);

  #if defined(__ANDROID__)
  //std::shared_ptr<LiteImage::ICombinedImageSampler> LoadTextureAndMakeCombined( AAssetManager* assetManager, const tinygltf::Image& a_image, const LiteImage::Sampler& a_sampler, const std::string& a_sceneDir);
  #else
  //std::shared_ptr<LiteImage::ICombinedImageSampler> LoadTextureAndMakeCombined(const tinygltf::Image& a_image, const LiteImage::Sampler& a_sampler, const std::string& a_sceneDir);
  #endif

  bool ends_with (const std::string &fullString, const std::string &ending);

  std::pair<LiteMath::float4x4, LiteMath::float4x4> makeDefaultCamera(float aspect);
  std::pair<LiteMath::float4x4, LiteMath::float4x4> makeCameraFromSceneBBox(uint32_t render_width, uint32_t render_height, Box4f box);
}

#endif //GLTF_LOADER_H
