#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include "scene.h"
#include "loadutil.h"
#include <iostream>
#include <memory>
#include <optional>
#include <algorithm>
#include <utility>
#include <unordered_map>





namespace LiteScene
{

    static void append_float3_as_float4(const gltf::Model &model, const gltf::Accessor& accessor, std::vector<LiteMath::float4> &target)
    {
        const gltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        const gltf::Buffer& buffer = model.buffers[bufferView.buffer];
        const float* p = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
        for(size_t i = 0; i < accessor.count; ++i) {
            target.push_back(LiteMath::float4(
                                p[3 * i + 0],
                                p[3 * i + 1],
                                p[3 * i + 2],
                                0));
        }

    }

    template<typename T, typename P>
    void _append_int(const gltf::Model &model, const gltf::Accessor& accessor, std::vector<P> &target, unsigned offset)
    {
        const gltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        const gltf::Buffer& buffer = model.buffers[bufferView.buffer];

        const T* p = reinterpret_cast<const T*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
        if(offset != 0) {
            const size_t old_size = target.size();
            target.resize(old_size + accessor.count);
            std::transform(p, p + accessor.count, target.begin() + old_size, [offset](T val) { return val + offset; });
        }
        else {
            target.insert(target.end(), p, p + accessor.count);
        }
    }

    static void append_indices(const gltf::Model &model, const gltf::Accessor& accessor, std::vector<unsigned> &target, unsigned offset = 0u)
    {
        switch(accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_INT:
            _append_int<int32_t, unsigned>(model, accessor, target, offset);
            return;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            _append_int<uint32_t, unsigned>(model, accessor, target, offset);
            return;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
            _append_int<int16_t, unsigned>(model, accessor, target, offset);
            return;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            _append_int<uint16_t, unsigned>(model, accessor, target, offset);
            return;
        case TINYGLTF_COMPONENT_TYPE_BYTE:
            _append_int<int8_t, unsigned>(model, accessor, target, offset);
            return;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            _append_int<uint8_t, unsigned>(model, accessor, target, offset);
            return;
        default:
            return;
        }

    }

    //returns offset to add to primitive indices
    static uint32_t load_vertices_by_accessor(const gltf::Model &model, int acc_id, std::vector<LiteMath::float4> &target, std::unordered_map<uint32_t, uint32_t> &offsetMap)
    {
        const auto [it, not_exist] = offsetMap.try_emplace(acc_id, target.size());
        uint32_t offset = it->second;
        if(not_exist) {
            const gltf::Accessor &accessor = model.accessors[acc_id];
            append_float3_as_float4(model, accessor, target);
        }
        return offset;
    }   

    LiteMath::float4 gen_tang(const LiteMath::float4 &normal)
    {
        LiteMath::float4 a = LiteMath::cross(normal, {0, 1, 0, 1});
        if(a.x == 0.0f && a.y == 0.0f && a.z == 0.0f) {
            return LiteMath::cross(normal, {1, 0, 0, 1});
        }
        return a;
    }

    static bool load_gltf_mesh(const gltf::Model &model, const gltf::Mesh &mesh, cmesh4::SimpleMesh &simpleMesh, std::vector<uint32_t> &materials)
    {
        std::unordered_map<uint32_t, uint32_t> posOffsetMap;
        std::unordered_map<uint32_t, uint32_t> normOffsetMap;
        std::unordered_map<uint32_t, uint32_t> tangOffsetMap;

        for(const auto &prim : mesh.primitives) {
            if(prim.mode != TINYGLTF_MODE_TRIANGLES) {
                std::cerr << "[scene_convert ERROR] Only triangle primitives are supported" << std::endl;
                return false;
            }  


            const int posAccId =  prim.attributes.at("POSITION");
            const int normAccId = prim.attributes.at("NORMAL");
            const auto tangAccIdIt = prim.attributes.find("TANGENT");
            const int tangAccId = tangAccIdIt == prim.attributes.end() ? -1 : tangAccIdIt->second;

            const uint32_t posOffset  = load_vertices_by_accessor(model, posAccId,  simpleMesh.vPos4f,  posOffsetMap);
            const uint32_t normOffset = load_vertices_by_accessor(model, normAccId, simpleMesh.vNorm4f, normOffsetMap);

            const uint32_t tangOffset = tangAccId != -1 ? load_vertices_by_accessor(model, tangAccId, simpleMesh.vTang4f, tangOffsetMap) : 0xffffffff;

            if(posOffset != normOffset || (tangOffset != 0xffffffff && posOffset != tangOffset)) {
                std::cerr << "[scene_convert ERROR] Inconsistent mesh primitive vectors layout" << std::endl;
                return false;
            }

            size_t ind_count;

            if(prim.indices != -1) {
                const gltf::Accessor& indAcessor = model.accessors[prim.indices];
                append_indices(model, indAcessor, simpleMesh.indices, posOffset);
                ind_count = indAcessor.count;
            }
            else {
                std::cerr << "[scene_convert ERROR] Non-indexed geometry is not supported yet" << std::endl;
                return false;
            }

            if(tangAccId == -1) {
                simpleMesh.vTang4f.resize(simpleMesh.vPos4f.size());
                for(int i = 0; i < simpleMesh.vNorm4f.size(); ++i) {
                    simpleMesh.vTang4f[i] = gen_tang(simpleMesh.vNorm4f[i]);
                }
            }
            simpleMesh.vTexCoord2f.resize(simpleMesh.vPos4f.size());


            size_t mat_offset = materials.size();
            materials.resize(mat_offset + ind_count / 3);
            std::fill_n(materials.begin() + mat_offset, ind_count / 3, prim.material == -1 ? 0 : prim.material); // XXX: cmesh4 is in quads
        }

        return true;
    }

    static bool load_gltf_meshes(const gltf::Model model, std::map<uint32_t, Geometry *> &geometries, bool only_geometry)
    {
        std::vector<std::unique_ptr<MeshGeometry>> meshes;
        meshes.reserve(model.meshes.size());

        uint32_t id = 0;
        for(const auto &mesh : model.meshes) {
            std::unique_ptr<MeshGeometry> mg{new MeshGeometry()};

            //std::cout << "Adding mesh" << id << std::endl;

            mg->id = id;
            mg->name = mesh.name.size() ? mesh.name : "gltf-mesh#" + std::to_string(id);
            mg->relative_file_path = "data/gltf_mesh_" + std::to_string(id) + ".vsgf";
            mg->is_loaded = true;

            std::vector<uint32_t> prim_materials;
            prim_materials.reserve(mesh.primitives.size());
            load_gltf_mesh(model, mesh, mg->mesh, prim_materials);

            //TODO add dummy material on request
            mg->mesh.matIndices = std::move(prim_materials);


            geometries[id] = mg.release(); 
            id += 1;
        }

        return true;
    }


    static inline float4x4 quat2mat(double qw, double qx, double qy, double qz)
    {   
        float4x4 rotation;

        double qx2 = qx * qx;
        double qy2 = qy * qy;
        double qz2 = qz * qz;

        rotation(0, 0) = 1.0f - 2.0f * float(qy2 + qz2);
        rotation(0, 1) = 2.0f * float(qx * qy - qz * qw);
        rotation(0, 2) = 2.0f * float(qx * qz + qy * qw);
        rotation(1, 0) = 2.0f * float(qx * qy + qz * qw);
        rotation(1, 1) = 1.0f - 2.0f * float(qx2 + qz2);
        rotation(1, 2) = 2.0f * float(qy * qz - qx * qw);
        rotation(2, 0) = 2.0f * float(qx * qz - qy * qw);
        rotation(2, 1) = 2.0f * float(qy * qz + qx * qw);
        rotation(2, 2) = 1.0f - 2.0f * float(qx2 + qy2);
        return rotation;
    }

    static void load_gltf_node_matrix(const gltf::Node &node, LiteMath::float4x4 &mat)
    {
        if(!node.matrix.empty()) {
            std::vector<float> data{node.matrix.begin(), node.matrix.end()};
            array_to_float4x4(data, mat);
            return;
        }

        LiteMath::float4x4 rotation;
        LiteMath::float4x4 scale;
        LiteMath::float4x4 translation;

        if(!node.rotation.empty()) {
            double qw = node.rotation[0];
            double qx = node.rotation[1];
            double qy = node.rotation[2];
            double qz = node.rotation[3];

            rotation = quat2mat(qw, qx, qy, qz);
        }

        if(!node.scale.empty()) {
            for(int i = 0; i < 3; ++i) scale(i, i) = node.scale[i];
        }

        if(!node.translation.empty()) {
            for(int i = 0; i < 3; ++i) translation(i, 3) = -node.translation[i];
        }

        mat = translation * rotation * scale;
    }

    static std::optional<InstancedScene> load_gltf_scene_node(const gltf::Model &model, const gltf::Scene &scene, std::map<uint32_t, Camera> &cameras, uint32_t id)
    {
        InstancedScene out;
        out.id = id;
        out.name = "gltf-scene#" + std::to_string(id);

        uint32_t inst_id = 0;
        uint32_t linst_id = 0;
        for(int node_id : scene.nodes) {
            const gltf::Node &node = model.nodes[node_id];
            LiteMath::float4x4 matrix;
            load_gltf_node_matrix(node, matrix);
            if(node.mesh != GLTF_INVALID_ID) {

                Instance inst;
                inst.id = inst_id++;
                inst.mesh_id = uint32_t(node.mesh);
                inst.scn_id = id;
                inst.matrix = matrix;
                out.instances[inst.id] = inst;
            }
            else if(node.light != GLTF_INVALID_ID) {
                LightInstance linst;
                linst.id = linst_id++;
                linst.light_id = uint32_t(node.light);
                linst.matrix = matrix;
                out.light_instances[linst.id] = linst;
            }
            if(node.camera != GLTF_INVALID_ID) {
                Camera &cam = cameras[node.camera];
                cam.matrix = matrix;
                cam.has_matrix = true;
            }
        }
        return {std::move(out)};
    }

    static bool load_gltf_scenes(const gltf::Model &model, std::map<uint32_t, InstancedScene> &scenes, std::map<uint32_t, Camera> &cameras)
    {
        uint32_t i = 0;
        for(const auto &scene : model.scenes) {
            auto opt = load_gltf_scene_node(model, scene, cameras, i);
            if(!opt) {
                scenes.clear();
                return false;
            }
            scenes[i++] = std::move(*opt);
        }
        return true;
    }

    static bool load_gltf_cameras(const gltf::Model &model, std::map<uint32_t, Camera> &cameras)
    {
        uint32_t id = 0;
        if(!model.cameras.empty()) {
            for(const auto &gltfCam : model.cameras) {
                const auto &cam = gltfCam.perspective;
                Camera camera;
                camera.id = id;
                camera.name = "gltf-camera#" + std::to_string(id);
                id += 1;

                camera.fov = cam.yfov;
                camera.nearPlane = cam.znear;
                camera.farPlane = cam.zfar;
                cameras[camera.id] = std::move(camera);
            }
            return true;
        }
        return true;
    }
/*
    bool load_gltf_mat(const gltf::Model &model, std::map<uint32_t, Material *> &materials)
    {
        uint32_t id = 0;
        for(const auto &gltfMat : model.materials) {
            std::string matName = gltfMat.name.size() ? gltfMat.name : ("gltf-mat#" + std::to_string(id));
            GltfMaterial mat{id, matName};
            const auto &mr = gltfMat.pbrMetallicRoughness;
            if(mr.baseColorTexture.index != -1) {

            }

        }
    }
*/
    bool load_gltf_scene(const std::string &filename, HydraScene &scene, bool only_geometry)
    {
        gltf::Model model;
        gltf::TinyGLTF loader;
        std::string err, warn;

        if(!loader.LoadASCIIFromFile(&model, &err, &warn, filename)) {
            std::cerr << "Failed to parse glTF" << std::endl;
            return false;
        }

        if(!warn.empty()) {
            std::cerr << "[Tiny-glTF WARN]: " << warn << std::endl;
        }
        if(!err.empty()) {
            std::cerr << "[Tiny-glTF ERROR]: " << err << std::endl;
            return false;
        }

        if(!load_gltf_meshes(model, scene.geometries, only_geometry)) return false;
        if(!load_gltf_cameras(model, scene.cameras)) return false;
        if(!load_gltf_scenes(model, scene.scenes, scene.cameras)) return false;

        return true;
    }


}