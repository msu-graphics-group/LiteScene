#ifndef LITESCENE_MATERIAL_H_
#define LITESCENE_MATERIAL_H_

#include "3rd_party/pugixml.hpp"
#include <LiteMath.h>
#include <Image2d.h>
#include <string>
#include <variant>
#include <optional>


namespace LiteScene {

    class Spectrum;

    struct TextureInstance
    {
        struct SamplerData {
            LiteImage::Sampler::AddressMode addr_mode_u;
            LiteImage::Sampler::AddressMode addr_mode_v;
            LiteImage::Sampler::AddressMode addr_mode_w;
            LiteImage::Sampler::Filter filter;

            bool operator==(const SamplerData &other) const
            {
                return addr_mode_u == other.addr_mode_u
                    && addr_mode_v == other.addr_mode_v
                    && addr_mode_w == other.addr_mode_w
                    && filter == other.filter;
            }
        };

        uint32_t id;

        SamplerData sampler;
        LiteMath::float4x4 matrix;
        float input_gamma = 2.2f;
        bool alpha_from_rgb = true;
    };

    struct TexSamplerHash
    {
        std::size_t operator()(const std::pair<TextureInstance::SamplerData, bool> &s) const 
        {
            std::size_t res = std::hash<uint64_t>{}(static_cast<uint64_t>(s.first.addr_mode_u)) * 31ull
                            + std::hash<uint64_t>{}(static_cast<uint64_t>(s.first.addr_mode_v)) * 31ull
                            + std::hash<uint64_t>{}(static_cast<uint64_t>(s.first.addr_mode_w)) * 31ull
                            + std::hash<uint64_t>{}(static_cast<uint64_t>(s.first.filter)) * 31ull
                            + std::hash<bool>{}(s.second);

            return res;
        }
    };

    template<typename T>
    struct SceneRef {
        uint32_t id;
        operator uint32_t() const { return id; }
    };

    enum class MaterialType 
    {
        LIGHTSOURCE,
        GLTF,
        DIFFUSE,
        CONDUCTOR,
        DIELECTRIC,
        PLASTIC,
        BLEND,
        THIN_FILM
    };

    struct ColorHolder {
        std::optional<LiteMath::float4> color;
        uint32_t spec_id;
    };

    class Material
    {
    public:
        Material(uint32_t id, const std::string &name) : id(id), name(name) {}

        virtual ~Material() = default;
        virtual MaterialType type() const = 0;

        uint32_t id;
        std::string name;
        pugi::xml_node raw_xml;
    };

    class LightSourceMaterial : public Material
    {
    public:
        uint32_t light_id;
        std::variant<ColorHolder, TextureInstance> color;
        float power = 1.0f;

        using Material::Material;

        MaterialType type() const override
        {
            return MaterialType::LIGHTSOURCE;
        }
    };

    class GltfMaterial : public Material
    {
    public:

        struct GMC
        {
            std::variant<float, TextureInstance> glossiness;
            std::variant<float, TextureInstance> metalness;
            std::variant<float, TextureInstance> coat;
        };

        std::variant<LiteMath::float3, TextureInstance> color;
        std::variant<GMC, TextureInstance> glossiness_metalness_coat;
        float fresnel_ior;

        using Material::Material;

        MaterialType type() const override
        {
            return MaterialType::GLTF;
        }
    };

    class DiffuseMaterial : public Material
    {
    public:
        enum class BSDF { LAMBERT };

        std::variant<ColorHolder, TextureInstance> reflectance;
        BSDF bsdf_type;

        using Material::Material;

        MaterialType type() const override
        {
            return MaterialType::DIFFUSE;
        }
    };

    struct MiRoughness
    {
        float alpha_u;
        float alpha_v;
    };

    class ConductorMaterial : public Material
    {
    public:
        enum class BSDF { GGX };
        BSDF bsdf_type;

        std::variant<MiRoughness, TextureInstance> alpha;
        std::variant<float, SceneRef<Spectrum>> eta;
        std::variant<float, SceneRef<Spectrum>> k;
        std::optional<LiteMath::float3> reflectance;

        using Material::Material;

        MaterialType type() const override
        {
            return MaterialType::CONDUCTOR;
        }
    };

    class DielectricMaterial : public Material
    {
    public:
        std::variant<float, SceneRef<Spectrum>> int_ior;
        float ext_ior;

        using Material::Material;

        MaterialType type() const override
        {
            return MaterialType::DIELECTRIC;
        }
    };

    class PlasticMaterial : public Material
    {
    public:
        std::variant<ColorHolder, TextureInstance> reflectance;
        float int_ior;
        float ext_ior;
        float alpha;
        bool nonlinear;

        using Material::Material;

        MaterialType type() const override
        {
            return MaterialType::PLASTIC;
        }
    };


    class ThinFilmMaterial : public Material
    {
    public:
        struct Layer
        {
            std::variant<float, SceneRef<Spectrum>> eta;
            std::variant<float, SceneRef<Spectrum>> k;
            float thickness;
        };
        struct ThicknessMap 
        {
            float min;
            float max;
            TextureInstance texture;
        };
        enum class BSDF { GGX };


        BSDF bsdf_type;
        std::variant<MiRoughness, TextureInstance> alpha;
        std::variant<float, SceneRef<Spectrum>> eta;
        std::variant<float, SceneRef<Spectrum>> k;
        float ext_ior;
        std::optional<ThicknessMap> thickness_map;
        bool transparent;
        std::vector<Layer> layers;

        using Material::Material;

        MaterialType type() const override
        {
            return MaterialType::THIN_FILM;
        }
    };

    class BlendMaterial : public Material
    {
    public:
        float weight;
        Material *bsdf1;
        Material *bsdf2;

        using Material::Material;

        MaterialType type() const override
        {
            return MaterialType::BLEND;
        }
    };

}

#endif