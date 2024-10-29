#ifndef GLTF_TO_HYDRA_H
#define GLTF_TO_HYDRA_H

#include "tiny_gltf.h"
#include "../common.h"

void getNumVerticesAndIndicesFromGLTFMesh(const tinygltf::Model &a_model, const tinygltf::Mesh &a_mesh, uint32_t& numVertices, uint32_t& numIndices);
cmesh4::SimpleMesh simpleMeshFromGLTFMesh(const tinygltf::Model &a_model, const tinygltf::Mesh &a_mesh);
LiteMath::float4x4 transformMatrixFromGLTFNode(const tinygltf::Node &node);
MaterialDataInternal materialDataFromGLTF(const tinygltf::Material &gltfMat);

#endif 