#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "stb_image.h"
#include "stb_image_write.h"

#include "scene.h"
#include "loadutil.h"
#include "textures_util.h"
#include <iostream>
#include <memory>
#include <optional>
#include <algorithm>
#include <utility>
#include <unordered_map>


namespace LiteScene {

#ifdef NDEBUG
    static constexpr bool DEBUG_ENABLED = true;
#else
    static constexpr bool DEBUG_ENABLED = false;
#endif

    struct PrimitiveData
    {
        std::vector<uint32_t> indices;
    };

    static std::unordered_map<uint32_t, PrimitiveData> group_by_material(const std::vector<unsigned> &indices,
                                                                         const std::vector<unsigned> &materials)
    {
        std::unordered_map<uint32_t, PrimitiveData> groups;
        for(uint32_t i = 0; i < materials.size(); ++i) {
            auto &data = groups[materials[i]];
            for(int j = 0; j < 3; ++j) {
                data.indices.push_back(indices[3 * i + j]);
            }
            
        }
        return groups;
    }

    /**
     * Returns ids of accessors to vectors
     */
    static std::tuple<int, int, int> write_vertices_to_buffer(gltf::Model &model, gltf::Buffer &buffer, int buf_id, //buffer is in model.buffers
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
    static int write_indices_to_buffer(gltf::Model &model, gltf::Buffer &buffer, int buf_id, const std::vector<unsigned> &indices)
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

    static int write_uv_to_buffer(gltf::Model &model, gltf::Buffer &buffer, int buf_id, //buffer is in model.buffers
                                                              const std::vector<float> &texCoords)
    {
        const size_t offset = buffer.data.size();

        gltf::BufferView view;
        gltf::Accessor acc;

        view.buffer = buf_id;
        view.byteOffset = offset;
        view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        view.byteLength = texCoords.size() * sizeof(float);

        buffer.data.resize(offset + view.byteLength);

        acc.byteOffset = 0;
        acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        acc.count = texCoords.size() / 2;
        acc.type = TINYGLTF_TYPE_VEC2;
        //acc.maxValues = {1.0, 1.0};
       // acc.minValues = {0.0, 0.0};

        std::copy(texCoords.begin(), texCoords.end(), reinterpret_cast<float *>(buffer.data.data() + offset));

        int view_id = model.bufferViews.size();
        model.bufferViews.push_back(std::move(view));
        acc.bufferView = view_id;

        const int id = int(model.accessors.size());
        model.accessors.push_back(std::move(acc));
        return id;
    }



    static bool gltfcvt_mesh(gltf::Model &model, const MeshGeometry &mesh_geom, bool only_geometry)
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
                        
        std::vector<float> texCoords;
        texCoords.reserve(simpleMesh.vTexCoord2f.size() * 2);
        for(const auto &vec : simpleMesh.vTexCoord2f) {
            texCoords.push_back(vec.x);
            texCoords.push_back(1.0f - vec.y);
        }
        int texCoordAcc = write_uv_to_buffer(model, buffer, buf_id, texCoords);
    

        auto grouped = group_by_material(simpleMesh.indices, simpleMesh.matIndices);
        for(const auto &[mat_id, group] : grouped) {
            gltf::Primitive &prim = mesh.primitives.emplace_back();

            if(!only_geometry) {
                prim.material = mat_id;
            }
            prim.mode = TINYGLTF_MODE_TRIANGLES;

            prim.attributes["POSITION"] = posAcc;
            prim.attributes["NORMAL"] = normAcc;
            prim.attributes["TANGENT"] = tangAcc;
            prim.attributes["TEXCOORD_0"] = texCoordAcc;

            prim.indices = write_indices_to_buffer(model, buffer, buf_id, group.indices);

        }

        model.meshes.push_back(std::move(mesh));
        return true;
    }


    static bool gltfcvt_meshes(gltf::Model &model, const std::map<uint32_t, Geometry *> &geometries, 
                               const SceneMetadata &meta, bool only_geometry)
    {
        model.meshes.reserve(geometries.size());
        if(geometries.rbegin()->first != geometries.size() - 1) {
            std::cerr << "[scene_export ERROR] Inconsistent mesh ids" << std::endl;
            return false;
        }

        for(const auto &[id, geom] : geometries) {
            if(geom->type_id != Geometry::MESH_TYPE_ID) {
                return false;
            }
            MeshGeometry *mesh_geom = static_cast<MeshGeometry *>(geom);
            mesh_geom->load_data(meta);

            if(!gltfcvt_mesh(model, *mesh_geom, only_geometry)) {
                return false;
            }
        }
        return true;
    }

   
    static gltf::Value gltfIntValue(int val) {
        return gltf::Value(val);
    }

    static bool gltfcvt_hydragltf_mat(gltf::Model &model, const GltfMaterial &mat, std::vector<H2GTextureConv> &texture_map)
    {
        gltf::Material &material = model.materials.emplace_back();
        auto &mr = material.pbrMetallicRoughness;

        if(std::holds_alternative<TextureInstance>(mat.color)) {
            TextureInstance inst = std::get<TextureInstance>(mat.color);

            texture_map.emplace_back(std::vector{1, 2, 3}, std::vector{std::move(inst)}, true);

            mr.baseColorTexture.index = texture_map.size() - 1;
        }
        else {
            LiteMath::float3 color = std::get<LiteMath::float3>(mat.color);
            std::copy(color.M, color.M + 3, mr.baseColorFactor.data());
        }

        if(std::holds_alternative<TextureInstance>(mat.glossiness_metalness_coat)) {
            const TextureInstance &gmcInst = std::get<TextureInstance>(mat.glossiness_metalness_coat);

            texture_map.emplace_back(std::vector{H2GTextureConv::REMAP_ZEROS, -1, 2}, std::vector{gmcInst});
            mr.metallicRoughnessTexture.index = texture_map.size() - 1;

            texture_map.emplace_back(std::vector{3}, std::vector{gmcInst}); //FIXME
            gltf::Value::Object clearcoat;
            clearcoat["clearcoatFactor"] = gltf::Value(1.0); //?
            clearcoat["clearcoatTexture"] = gltf::Value(gltf::Value::Object{{"index", gltfIntValue(texture_map.size() - 1)}});
            material.extensions.emplace(GLTF_EXT_CLEARCOAT, std::move(clearcoat));
        }
        else {

            GltfMaterial::GMC gmc = std::get<GltfMaterial::GMC>(mat.glossiness_metalness_coat);
            H2GTextureConv conv{{H2GTextureConv::REMAP_ZEROS, H2GTextureConv::REMAP_ONES, H2GTextureConv::REMAP_ONES}, {}};
            bool useTexture = false;

            if(std::holds_alternative<float>(gmc.glossiness)) {
                float glossiness = std::get<float>(gmc.glossiness);
                mr.roughnessFactor = 1.0f - glossiness;
            }
            else {
                TextureInstance gInst = std::get<TextureInstance>(gmc.glossiness);
                conv.textures.push_back(std::move(gInst));
                conv.remap[1] = -int(conv.textures.size());
                useTexture = true;
            }

            if(std::holds_alternative<float>(gmc.metalness)) {
                float metalness = std::get<float>(gmc.metalness);
                mr.metallicFactor = metalness;
            }
            else {
                TextureInstance mInst = std::get<TextureInstance>(gmc.metalness);
                conv.textures.push_back(std::move(mInst));
                conv.remap[2] = conv.textures.size();
                useTexture = true;
            }

            if(useTexture) {
                texture_map.push_back(std::move(conv));
                mr.metallicRoughnessTexture.index = texture_map.size() - 1;
            }

            gltf::Value::Object clearcoat;
            if(std::holds_alternative<float>(gmc.coat)) {
                float coat = std::get<float>(gmc.coat);
                clearcoat["clearcoatFactor"] = gltf::Value(coat);
            }
            else {
                TextureInstance cInst = std::get<TextureInstance>(gmc.coat);
                H2GTextureConv conv_coat;
                conv_coat.remap = {1};
                conv_coat.textures = {std::move(cInst)};
                
                texture_map.push_back(std::move(conv_coat));

                clearcoat["clearcoatFactor"] = gltf::Value(1.0); //?
                clearcoat["clearcoatTexture"] = gltf::Value(gltf::Value::Object{{"index", gltfIntValue(texture_map.size() - 1)}});
            }
            material.extensions.emplace(GLTF_EXT_CLEARCOAT, std::move(clearcoat));
            //clearcoatRoughness - ???
        }
        
        if(mat.fresnel_ior != 1.5f) {
            gltf::Value::Object material_ior{{"ior", gltf::Value(mat.fresnel_ior)}};
            material.extensions.emplace(GLTF_EXT_FRESNEL_IOR, std::move(material_ior));
        }
        
        return true;
    }

    static bool gltfcvt_materials(gltf::Model &model, const std::map<uint32_t, Material *> &materials, std::vector<H2GTextureConv> &texture_map, bool strict_mode)
    {
        model.materials.reserve(materials.size());
        if(materials.size() && materials.rbegin()->first != materials.size() - 1) {
            std::cerr << "[scene_export ERROR] Inconsistent material ids" << std::endl;
            return false;
        }

        for(const auto &[id, mat] : materials) {
            if(mat->type() != MaterialType::GLTF) {
                if(strict_mode) {
                    std::cerr << "[scene_export ERROR] Only HydraGLTF materials can be converted to glTF, id = " << id << " cannot be processed" << std::endl;
                    return false;
                }
                else {
                    std::cerr << "[scene_export WARNING] Only HydraGLTF materials can be converted to glTF, using dummy material for id = " << id << std::endl;
                }
            }
            else if(!gltfcvt_hydragltf_mat(model, *static_cast<const GltfMaterial *>(mat), texture_map)) {
                return false;
            }
        }
        return true;
    }

    static inline void matrix_to_array(const LiteMath::float4x4 &matrix, std::vector<double> &array)
    {
        array.resize(16);
        for(int i = 0; i < 4; ++i) {
            auto vec = matrix.get_col(i);
            std::copy(vec.M, vec.M + 4, array.data() + 4 * i);
        }
    }

    static inline void cam_params_to_array(const Camera &cam, std::vector<double> &array)
    {
        if(cam.has_matrix) {
            matrix_to_array(cam.matrix, array);
        }
        else {
            auto forward = cam.lookAt - cam.pos;
            auto right = LiteMath::cross(forward, cam.up);
            array = {
                right.x,      right.y,      right.z,      0,
                cam.up.x,     cam.up.y,     cam.up.z,     0,
               -forward.x,  -forward.y,   -forward.z,    0,
                cam.pos.x,    cam.pos.y,    cam.pos.z,    1
            };
            /*
            array = {
                right.x, cam.up.x, cam.lookAt.z, cam.pos.x,
                right.y, cam.up.y, cam.lookAt.y, cam.pos.y,
                right.z, cam.up.x, cam.lookAt.z, cam.pos.z,
                0,       0,        0,            1
            };*/
        }
    }

    static inline bool gltfcvt_cameras(gltf::Model &model, const std::map<uint32_t, Camera> &cams, std::vector<int> &cam_nodes)
    {
        for(const auto &[cam_id, cam] : cams) {
            gltf::Camera gltf_cam;
            gltf_cam.name = cam.name;
            gltf_cam.type = "perspective";

            gltf_cam.perspective.yfov = cam.fov;
            gltf_cam.perspective.znear = cam.nearPlane;
            gltf_cam.perspective.zfar = cam.farPlane;


            int id = model.cameras.size();
            model.cameras.push_back(std::move(gltf_cam));


            gltf::Node cam_node;
            cam_node.name = "cam_node[" + cam.name + "]";
            cam_node.camera = id;

            cam_params_to_array(cam, cam_node.matrix);

            model.nodes.push_back(std::move(cam_node));
            cam_nodes.push_back(model.nodes.size() - 1);
        }
        return true;
    }

    static inline bool gltfcvt_scene_inst(gltf::Model &model, const InstancedScene &instScene, const std::vector<int> &camera_nodes, bool strict)
    {
        gltf::Scene scene;
        scene.name = instScene.name;

        for(const auto &[inst_id, inst] : instScene.instances) {
            gltf::Node node;
            node.mesh = inst.mesh_id;

            if(inst.rmap_id != INVALID_ID) {
                std::cerr << (strict ? "[scene_export ERROR]" : "[scene_export WARNING]");
                std::cerr << " Remap lists are not supported for instanced scene '" << scene.name << "'" << std::endl;
                if(strict) return false;
            }

            if(inst.light_id != INVALID_ID) {
                std::cerr << "[scene_export WARNING] Ignoring parameter light_id for instanced scene '" << scene.name << "'" << std::endl;
            } 
            if(inst.linst_id != INVALID_ID) {
                std::cerr << "[scene_export WARNING] Ignoring parameter linst_id for instanced scene '" << scene.name << "'" << std::endl;
            } 

            matrix_to_array(inst.matrix, node.matrix);

            scene.nodes.push_back(model.nodes.size());
            model.nodes.push_back(std::move(node));
        }

        std::copy(camera_nodes.begin(), camera_nodes.end(), std::back_inserter(scene.nodes));

        model.scenes.push_back(std::move(scene));
        return true;
    }

    static bool gltfcvt_scenes_insts(gltf::Model &model, const std::map<uint32_t, InstancedScene> &scenes, const std::vector<int> &camera_nodes, bool strict)
    {
        for(const auto &[id, instance] : scenes) {
            if(!gltfcvt_scene_inst(model, instance, camera_nodes, strict)) return false;
        }
        return true;
    }

    struct HTextureConv
    {
        std::vector<int> remap; 
        std::vector<uint32_t> textures;
        float gamma;

        HTextureConv(const H2GTextureConv &conv)
        {
            remap = conv.remap;
            for(const auto &t : conv.textures) textures.push_back(t.id);
            gamma = conv.textures[0].input_gamma;
        }

        bool operator==(const HTextureConv &other) const 
        {
            return (remap == other.remap) && (textures == other.textures) && (gamma == other.gamma); 
        }
    };

    struct HTextureConvHash
    {
        size_t operator()(const HTextureConv &obj) const 
        {
            size_t res = 0ull;

            for(int v : obj.remap) {
                res = 31 * res + std::hash<int>{}(v);
            }
            for(uint32_t v : obj.textures) {
                res = 31 * res + std::hash<uint32_t>{}(v);
            }
            res = 31 * res + std::hash<float>{}(obj.gamma);
            return res;
        }
    };
    using TextureConvCacheMap = std::unordered_map<HTextureConv, int, HTextureConvHash>;
    using TextureSamplerCacheMap = std::unordered_map<TextureInstance::SamplerData, int, TexSamplerHash>;

    static inline int gltfcvt_tex_wrap(LiteImage::Sampler::AddressMode mode)
    {
        switch(mode) {
        case LiteImage::Sampler::AddressMode::MIRROR_ONCE:
            std::cerr << "[scene_export WARNING] MIRROR_ONCE wrapping is not supported in glTF, using MIRROR" << std::endl;
            [[fallthrough]];
        case LiteImage::Sampler::AddressMode::MIRROR:
            return TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT;
        case LiteImage::Sampler::AddressMode::CLAMP:
            return TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE;
        case LiteImage::Sampler::AddressMode::BORDER:
            std::cerr << "[scene_export WARNING] BORDER wrapping is not supported in glTF, using WRAP" << std::endl;
            [[fallthrough]];
        case LiteImage::Sampler::AddressMode::WRAP:
            return TINYGLTF_TEXTURE_WRAP_REPEAT;
        }
        return TINYGLTF_TEXTURE_WRAP_REPEAT;
    }

    static inline int gltfcvt_tex_filter(LiteImage::Sampler::Filter filter)
    {
        switch(filter) {
        case LiteImage::Sampler::Filter::NEAREST:
            return TINYGLTF_TEXTURE_FILTER_NEAREST;
        case LiteImage::Sampler::Filter::CUBIC:
            std::cerr << "[scene_export WARNING] CUBIC filtering is not supported in glTF, using LINEAR" << std::endl;
            [[fallthrough]];
        case LiteImage::Sampler::Filter::LINEAR:
            return TINYGLTF_TEXTURE_FILTER_LINEAR;
        }
        return TINYGLTF_TEXTURE_FILTER_NEAREST;
    }

    static inline int get_or_convert_sampler(gltf::Model &model, const TextureInstance::SamplerData &sampler, TextureSamplerCacheMap &cache)
    {
        auto it = cache.find(sampler);
        if(it != cache.end()) return it->second;

        gltf::Sampler res;
        res.wrapS = gltfcvt_tex_wrap(sampler.addr_mode_u);
        res.wrapT = gltfcvt_tex_wrap(sampler.addr_mode_v);
        res.minFilter = res.magFilter = gltfcvt_tex_filter(sampler.filter);

        model.samplers.push_back(std::move(res));
        return model.samplers.size() - 1;
    }



    static inline unsigned char do_remap(unsigned char rgb[4], int remap) {
        switch(remap) {
        case H2GTextureConv::REMAP_ONES:
            return 255;
        case H2GTextureConv::REMAP_ZEROS:
            return 0;
        default:
            unsigned char val = rgb[std::abs(remap) - 1];
            return remap < 0 ? 255 - val : val;
        }
    }

    static void fill_value1(int w, int h, unsigned char val, unsigned char *out, int ch_out)
    {
        for(int i = 0; i < h; ++i) {
            for(int j = 0; j < w; ++j) {
                out[0] = val;
                out += ch_out;
            }
        }
    }

    static bool load_texture1(const Texture &tex, int w, int h, TextureInstance inst, bool invert, unsigned char *out, int ch_out, bool srgb)
    {   
        auto csampler = tex.get_combined_sampler(inst);

        static const LiteMath::float2 shift = {0.5 / w, 0.5 / h};
        for(int i = 0; i < h; ++i) {
            for(int j = 0; j < w; ++j) {
                LiteMath::float2 pos = {float(j) / float(w), float(h - i - 1) / float(h)};
                LiteMath::float4 pixel = toSRGB(csampler->sample(pos + shift), srgb ? 2.4f : 1.0f);

                unsigned char val = int(LiteMath::clamp(pixel.x, 0.0f, 1.0f) * 255);
                out[0] = invert ? 255 - val : val;
                out += ch_out;
            }
        }
        return true;
    }

    //sets each (0 + k * ch_out),... pixels in out according to remap
    static bool load_texture(const Texture &tex, int w, int h, TextureInstance inst, const std::vector<int> &remap, unsigned char *out, int ch_out, bool srgb)
    {
        //inst.input_gamma = 1.0f;
        auto csampler = tex.get_combined_sampler(inst);
        static const LiteMath::float2 shift = {0.5f / w, 0.5f / h};
        for(int i = 0; i < h; ++i) {
            for(int j = 0; j < w; ++j) {
                LiteMath::float2 pos = {float(j) / float(w), float(h - i - 1) / float(h)};
                LiteMath::float4 pixel = toSRGB(csampler->sample(pos + shift), srgb ? 2.4f : 1.0f);
                pixel = LiteMath::clamp(pixel, 0.0f, 1.0f);
                unsigned char rgb[] = {uint8_t(pixel.x * 255), uint8_t(pixel.y * 255), uint8_t(pixel.z * 255), uint8_t(pixel.w * 255)};

                for(int i = 0; i < remap.size() && i < ch_out; ++i) {
                    out[i] = do_remap(rgb, remap[i]);
                }
                out += ch_out;
            }
        }
        return true;
    }

    int fill_remap_or_texture1(int w, int h, int remap, unsigned char *out, int ch_out, int &tex_id)
    {
        switch(remap) {
        case H2GTextureConv::REMAP_ONES:
            fill_value1(w, h, 255, out, ch_out);
            return 0;
        case H2GTextureConv::REMAP_ZEROS:
            fill_value1(w, h, 0, out, ch_out);
            return 0;
        default:
            tex_id = remap;
            return 1;
        }
    }

    static int get_or_convert_image(gltf::Model &model, const fs::path &scene_root, const std::map<uint32_t, Texture> &textures, const H2GTextureConv &conv, TextureConvCacheMap &cache)
    {
        HTextureConv hconv{conv};
        auto it = cache.find(hconv);
        if(it != cache.end()) return it->second;

        const Texture &texture0 = textures.at(conv.textures[0].id);
        std::string name = "image_" + std::to_string(model.images.size());
        const Texture::Info &info = texture0.get_info();

        gltf::Image tex;
        tex.name = name;
        tex.width = info.width;
        tex.height = info.height;
        tex.bits = 8;
        tex.pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        tex.uri = scene_root / (name + ".png"); 

        if(conv.textures.size() == 1) {
            tex.component = conv.remap.size();
            tex.image.resize(tex.width * tex.height * tex.component);
            load_texture(texture0, info.width, info.height, conv.textures[0], conv.remap, tex.image.data(), tex.component, conv.srgb);
        }
        else {
            tex.component = conv.remap.size();
            tex.image.resize(tex.width * tex.height * tex.component);
            for(size_t i = 0; i < conv.remap.size(); ++i) {
                int tex_val;
                if(fill_remap_or_texture1(info.width, info.height, conv.remap[i], tex.image.data() + i, tex.component, tex_val)) {
                    const auto inst = conv.textures[std::abs(tex_val) - 1];
                    const Texture &texture = textures.at(inst.id); 
                    //std::cout << texture.name << " ";
                    load_texture1(texture, info.width, info.height, inst, tex_val < 0, tex.image.data() + i, tex.component, conv.srgb);
                }
                //std::cout << " -> " << name << std::endl;

            }
        }


        model.images.push_back(std::move(tex));
        cache[hconv] = model.images.size() - 1;
        return model.images.size() - 1;;
    }

    static bool gltfcvt_textures(gltf::Model &model, const fs::path &scene_root, const std::map<uint32_t, Texture> &textures, const std::vector<H2GTextureConv> &texture_map, bool strict)
    {
        TextureConvCacheMap texture_conv_cache;
        TextureSamplerCacheMap texture_samplers_cache;

        for(size_t i = 0; i < texture_map.size(); ++i) {
            const H2GTextureConv &conv = texture_map[i];

            if(conv.textures.size() > 1) {
                bool different_samplers = !(conv.textures[0].sampler == conv.textures[1].sampler);
                different_samplers = different_samplers || (conv.textures.size() == 3 && !(conv.textures[0].sampler == conv.textures[2].sampler));
                if(different_samplers) {
                    std::cerr << (strict ? "[scene_export ERROR]" : "[scene_export WARNING]");
                    std::cerr << " Different samplers for merged textures" << std::endl;
                    if(strict) return false;
                } 
            }


            const int image = get_or_convert_image(model, scene_root, textures, conv, texture_conv_cache);
            const int sampler = get_or_convert_sampler(model, conv.textures[0].sampler, texture_samplers_cache);

            gltf::Texture tex{.sampler = sampler, .source = image};
            model.textures.push_back(std::move(tex));
        }

        return true;
    }

    bool save_as_gltf_scene(const std::string &filename, const HydraScene &scene, bool strict, bool only_geometry)
    {
        gltf::Model model;
        std::vector<H2GTextureConv> texture_map;
        std::vector<int> cam_nodes;

        fs::path scene_root = fs::path(filename).parent_path();
        if(!gltfcvt_meshes(model, scene.geometries, scene.metadata, only_geometry)) return false;
        if(!only_geometry && !gltfcvt_materials(model, scene.materials, texture_map, strict)) return false;
        if(!gltfcvt_textures(model, scene_root, scene.textures, texture_map, strict)) return false;
        if(!gltfcvt_cameras(model, scene.cameras, cam_nodes)) return false;
        if(!gltfcvt_scenes_insts(model, scene.scenes, cam_nodes, strict)) return false;


        gltf::TinyGLTF loader;
        loader.WriteGltfSceneToFile(&model, filename,
                           false, // embedImages
                           true, // embedBuffers
                           true || DEBUG_ENABLED, // pretty print
                           false); // write binary

        return true;
    }

}