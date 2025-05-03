#include "scene.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "loadutil.h"
#include <iostream>
#include <memory>
#include <optional>
#include <algorithm>
#include <utility>
#include <unordered_map>

#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include "3rd_party/tiny_gltf.h"

namespace gltf = tinygltf;



namespace LiteScene
{
    static constexpr int GLTF_INVALID_ID = -1;

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
    void _append_int(const gltf::Model &model, const gltf::Accessor& accessor, std::vector<P> &target)
    {
        const gltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        const gltf::Buffer& buffer = model.buffers[bufferView.buffer];

        const T* p = reinterpret_cast<const T*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
        target.insert(target.end(), p, p + accessor.count);
    }

    static void append_indices(const gltf::Model &model, const gltf::Accessor& accessor, std::vector<unsigned> &target)
    {
        switch(accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_INT:
            _append_int<int32_t, unsigned>(model, accessor, target);
            return;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            _append_int<uint32_t, unsigned>(model, accessor, target);
            return;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
            _append_int<int16_t, unsigned>(model, accessor, target);
            return;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            _append_int<uint16_t, unsigned>(model, accessor, target);
            return;
        case TINYGLTF_COMPONENT_TYPE_BYTE:
            _append_int<int8_t, unsigned>(model, accessor, target);
            return;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            _append_int<uint8_t, unsigned>(model, accessor, target);
            return;
        default:
            return;
        }

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

            std::vector<uint32_t> prim_materials;

            cmesh4::SimpleMesh &simpleMesh = mg->mesh; 
            mg->is_loaded = true;

            for(const auto &prim : mesh.primitives) {
                if(prim.mode != TINYGLTF_MODE_TRIANGLES) {
                    std::cerr << "[ERROR] Only triangle primitives are supported" << std::endl;
                    return false;
                }

                const gltf::Accessor& posAccessor = model.accessors[prim.attributes.at("POSITION")];
                const gltf::Accessor& normAccessor = model.accessors[prim.attributes.at("NORMAL")];
                const gltf::Accessor& tangAccessor = model.accessors[prim.attributes.at("TANGENT")];
                const gltf::Accessor& indAcessor  = model.accessors[prim.indices];
                append_float3_as_float4(model, posAccessor, simpleMesh.vPos4f);
                append_float3_as_float4(model, normAccessor, simpleMesh.vNorm4f);
                append_float3_as_float4(model, tangAccessor, simpleMesh.vTang4f);
                append_indices(model, indAcessor, simpleMesh.indices);

                simpleMesh.vTexCoord2f.resize(simpleMesh.vPos4f.size());
                simpleMesh.vTang4f.resize(simpleMesh.vPos4f.size());
                simpleMesh.matIndices.push_back(only_geometry ? 0 : prim.material);
            }

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
            std::cout << "[Tiny-glTF WARN]: " << warn << std::endl;
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



    inline std::unordered_map<uint32_t, std::vector<uint32_t>> group_by_material(const std::vector<unsigned> &indices, const std::vector<unsigned> &materials)
    {
        std::unordered_map<uint32_t, std::vector<uint32_t>> groups;

        for(uint32_t i = 0; i < materials.size(); ++i) {
            auto &list = groups[materials[i]];
            list.push_back(indices[3 * i]);
            list.push_back(indices[3 * i + 1]);
            list.push_back(indices[3 * i + 2]);
        }
        return groups;
    }


    /**
     * Returns ids of accessors to vectors
     */
    inline std::tuple<int, int, int> write_vertices_to_buffer(gltf::Model &model, gltf::Buffer &buffer, int buf_id, //buffer is in model.buffers
                                                              const std::vector<LiteMath::float4> &pos4f,
                                                              const std::vector<LiteMath::float4> &norm4f,
                                                              const std::vector<LiteMath::float4> &tang4f)
    {
        size_t offset = buffer.data.size();

        gltf::BufferView view;
        gltf::Accessor posAcc;
        gltf::Accessor normAcc;
        gltf::Accessor tangAcc;

        view.buffer = buf_id;
        view.byteOffset = offset;
        view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        view.byteLength = (pos4f.size() + norm4f.size() + tang4f.size()) * 3 * sizeof(float);


        buffer.data.resize(offset + view.byteLength);

        posAcc.byteOffset = 0;
        posAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        posAcc.count = pos4f.size();
        posAcc.type = TINYGLTF_TYPE_VEC3;
        posAcc.maxValues = {1.0, 1.0, 1.0};
        posAcc.minValues = {0.0, 0.0, 0.0};
        for(const auto &vec : pos4f) {
            std::copy(vec.M, vec.M + 3, reinterpret_cast<float *>(buffer.data.data() + offset));
            offset += 3 * sizeof(float);
        }

        normAcc.byteOffset = posAcc.byteOffset + pos4f.size() * 3 * sizeof(float);
        normAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        normAcc.count = norm4f.size();
        normAcc.type = TINYGLTF_TYPE_VEC3;
        normAcc.maxValues = {1.0, 1.0, 1.0};
        normAcc.minValues = {0.0, 0.0, 0.0};
        for(const auto &vec : norm4f) {
            std::copy(vec.M, vec.M + 3, reinterpret_cast<float *>(buffer.data.data() + offset));
            offset += 3 * sizeof(float);
        }

        tangAcc.byteOffset = normAcc.byteOffset + norm4f.size() * 3 * sizeof(float);
        tangAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        tangAcc.count = tang4f.size();
        tangAcc.type = TINYGLTF_TYPE_VEC3;
        tangAcc.maxValues = {1.0, 1.0, 1.0};
        tangAcc.minValues = {0.0, 0.0, 0.0};
        for(const auto &vec : tang4f) {
            std::copy(vec.M, vec.M + 3, reinterpret_cast<float *>(buffer.data.data() + offset));
            offset += 3 * sizeof(float);
        }

        int view_id = model.bufferViews.size();
        model.bufferViews.push_back(std::move(view));
        posAcc.bufferView = view_id;
        normAcc.bufferView = view_id;
        tangAcc.bufferView = view_id;


        const int id = int(model.accessors.size());
        model.accessors.push_back(std::move(posAcc));
        model.accessors.push_back(std::move(normAcc));
        model.accessors.push_back(std::move(tangAcc));
        return {id, id + 1, id + 2};
    }

    /**
     * Returns if of accessor to indices 
     */
    int write_indices_to_buffer(gltf::Model &model, gltf::Buffer &buffer, int buf_id, const std::vector<unsigned> &indices)
    {
        size_t offset = buffer.data.size();

        gltf::BufferView view;
        gltf::Accessor acc;

        view.buffer = buf_id;
        view.byteOffset = offset;
        view.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
        view.byteLength = indices.size() * sizeof(uint32_t);

        buffer.data.resize(offset + view.byteLength);

        acc.byteOffset = 0;
        acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        acc.count = indices.size();
        acc.type = TINYGLTF_TYPE_SCALAR;

        auto [imin, imax] = std::minmax_element(indices.begin(), indices.end());
        acc.maxValues = {double(*imax)};
        acc.minValues = {double(*imin)};

        std::copy(indices.begin(), indices.end(), reinterpret_cast<uint32_t *>(buffer.data.data() + offset));


        const int view_id = model.bufferViews.size();
        model.bufferViews.push_back(std::move(view));
        acc.bufferView = view_id;
        const int acc_id = int(model.accessors.size());
        model.accessors.push_back(std::move(acc));
        return acc_id;
    }




    bool save_gltf_mesh(gltf::Model &model, const MeshGeometry &mesh_geom, bool only_geometry)
    {
        const int buf_id = int(model.buffers.size());
        gltf::Buffer &buffer = model.buffers.emplace_back();
        gltf::Mesh mesh;
        mesh.name = mesh_geom.name;

        const cmesh4::SimpleMesh &simpleMesh = mesh_geom.mesh; 

        const auto [posAcc, normAcc, tangAcc] = write_vertices_to_buffer(model, buffer, buf_id, 
                                                                         simpleMesh.vPos4f,
                                                                         simpleMesh.vNorm4f,
                                                                         simpleMesh.vTang4f);

        auto grouped = group_by_material(simpleMesh.indices, simpleMesh.matIndices);
        for(const auto &[mat_id, indices] : grouped) {
            gltf::Primitive &prim = mesh.primitives.emplace_back();

            prim.material = mat_id;
            prim.mode = TINYGLTF_MODE_TRIANGLES;

            prim.attributes["POSITION"] = posAcc;
            prim.attributes["NORMAL"] = normAcc;
            prim.attributes["TANGENT"] = tangAcc;

            prim.indices = write_indices_to_buffer(model, buffer, buf_id, indices);
        }

        model.meshes.push_back(std::move(mesh));
        return true;
    }

    bool save_gltf_meshes(gltf::Model &model, const std::map<uint32_t, Geometry*> &geometries, const SceneMetadata &meta, bool only_geometry)
    {
        model.meshes.reserve(geometries.size());
        if(geometries.rbegin()->first != geometries.size() - 1) {
            std::cerr << "[scene_convert ERROR] : Illegal mesh ids" << std::endl;
            return false;
        }

        for(const auto &[id, geom] : geometries) {
            if(geom->type_id != Geometry::MESH_TYPE_ID) return false;
            MeshGeometry *mesh_geom = static_cast<MeshGeometry *>(geom);
            mesh_geom->load_data(meta);

            if(!save_gltf_mesh(model, *mesh_geom, only_geometry)) return false;
        }
        return true;
    }

    bool save_gltf_scene(const std::string &filename, const HydraScene &scene, bool only_geometry)
    {
        gltf::Model model;
        if(!save_gltf_meshes(model, scene.geometries, scene.metadata, only_geometry)) return false;

        return true;
    }


}