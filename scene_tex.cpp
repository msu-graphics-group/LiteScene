#include "scene.h"
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "loadutil.h"


#include <iostream>
#include <vector>
#include <algorithm>

#include "textures_util.h"


namespace fs = std::filesystem;
using LiteImage::Image2D;
using namespace LiteMath;

namespace LiteScene
{


    bool Texture::load_info(pugi::xml_node &node, const std::string &scene_root)
    {
        id = node.attribute(L"id").as_uint();
        name = ws2s(node.attribute(L"name").as_string());

        if(node.attribute(L"loc").empty()) {
            info.path = ws2s(std::wstring(node.attribute(L"path").as_string()));
        }
        else {
            std::string loc = ws2s(std::wstring(node.attribute(L"loc").as_string()));
            info.path = (fs::path(scene_root) / loc).string();
        }
        
        info.offset = node.attribute(L"offset").as_ullong();
        info.width = node.attribute(L"width").as_uint();
        info.height = node.attribute(L"height").as_uint();
        if(info.width != 0 && info.height != 0) {
            const size_t byteSize = node.attribute(L"bytesize").as_ullong();
            info.bpp = uint32_t(byteSize / size_t(info.width * info.height));
        }
        return true;
    }


    bool Texture::save_info(pugi::xml_node &node, const std::string &old_scene_root, const SceneMetadata &newmeta) const
    {
        set_attr(node, L"id", id);
        set_attr(node, L"name", s2ws(name));

        fs::path path = fs::path(info.path);
        fs::path abs_new_path = fs::path(newmeta.geometry_folder) / path.filename();
        fs::path rel_new_path = get_relative_if_possible(fs::path(newmeta.scene_xml_folder), abs_new_path);

        if(rel_new_path.is_absolute()) {
            set_attr(node, L"path", s2ws(path.string()));
        }
        else {
            fs::copy(fs::path(info.path), abs_new_path, fs::copy_options::update_existing
                                                      | fs::copy_options::recursive);

            set_attr(node, L"loc", s2ws(rel_new_path.string()));
        }
        set_attr(node, L"offset", info.offset);
        set_attr(node, L"height", info.height);
        set_attr(node, L"width", info.width);
        if(info.width != 0 && info.height != 0) {
            set_attr(node, L"bytesize", size_t(info.bpp) * info.width * info.height);
        }
        return true;
    }

    std::shared_ptr<LiteImage::ICombinedImageSampler> Texture::get_combined_sampler(const TextureInstance &inst) const
    {
        std::pair<TextureInstance::SamplerData, bool> sd = {inst.sampler, inst.input_gamma == 1.0f};
        auto it = tex_cache.find(sd);
        if(it != tex_cache.end()) {
            return it->second;
        }
        auto s = make_combined_sampler(info, inst);
        tex_cache.insert({sd, s});
        return s;
    }

}
