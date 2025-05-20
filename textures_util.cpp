#include "textures_util.h"
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "loadutil.h"

#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstdio>

//#ifndef LITESCENE_NOTINYEXR
#define TINYEXR_IMPLEMENTATION
//#endif
#include "3rd_party/tinyexr/tinyexr.h"

// Undefine macro from windows headers
#ifdef LoadImage
#undef LoadImage
#endif



namespace fs = std::filesystem;
using LiteImage::Image2D;
using namespace LiteMath;


namespace LiteScene
{

    std::vector<float> LoadImage1fFromEXR(const char* infilename, int* pW, int* pH)
    {
        float* out; // width * height * RGBA
        int width       = 0;
        int height      = 0;
        const char* err = nullptr;

        int ret = LoadEXR(&out, &width, &height, infilename, &err);
        if (ret != TINYEXR_SUCCESS) {
            if (err) {
                fprintf(stderr, "[LoadImage1fFromEXR] : %s\n", err);
                std::cerr << "[LoadImage1fFromEXR] : " << err;
                std::cerr << " from path : " << infilename << std::endl;
                
                delete err;
            }
            return std::vector<float>();
        }

        const int imgSize = width * height;
        std::vector<float> result(imgSize);
        *pW = uint32_t(width);
        *pH = uint32_t(height);
        
        #pragma omp parallel for
        for(int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                size_t idx = (x + (height - y - 1) * width) * 4;
                size_t out_idx = x + y * width;
                if (std::isinf(out[idx]))
                    result[out_idx] = 65504.0f;                       // max half float according to ieee
                else
                    result[out_idx] = clamp(out[idx], 0.0f, 65504.0f); // max half float according to ieee
                
            }
        }

        free(out);
        return result;
    }


    std::vector<float> LoadImage4fFromEXR(const char* infilename, int* pW, int* pH) 
    {
        float* out; // width * height * RGBA
        int width       = 0;
        int height      = 0;
        const char* err = nullptr; 

        int ret = LoadEXR(&out, &width, &height, infilename, &err);
        if (ret != TINYEXR_SUCCESS) {
            if (err) {
                fprintf(stderr, "[LoadImage4fFromEXR] : %s\n", err);
                std::cerr << "[LoadImage4fFromEXR] : " << err << std::endl;
                delete err;
            }
            return std::vector<float>();
        }

        std::vector<float> result(width * height * 4);
        *pW = uint32_t(width);
        *pH = uint32_t(height);

        #pragma omp parallel for
        for(int y = 0; y < height; y++)
        {
            const int offset1 = (height - y - 1) * width * 4;
            const int offset2 = y * width * 4;
            std::copy_n(out + offset2, width * sizeof(float) * 4, result.data() + offset1);
        }
        free(out);  
        
        return result;
    }


    float* LoadImage4fFromEXRUnsafe(const char* infilename, int* pW, int* pH)
    {
        float* out; // width * height * RGBA
        int width  = 0;
        int height = 0;
        const char* err = nullptr; 

        int ret = LoadEXR(&out, &width, &height, infilename, &err);
        if (ret != TINYEXR_SUCCESS) {
            if (err) {
                fprintf(stderr, "[LoadImage4fFromEXR] : %s\n", err);
                std::cerr << "[LoadImage4fFromEXR] : " << err << std::endl;
                delete err;
            }
            return nullptr;
        }

        *pW = uint32_t(width);
        *pH = uint32_t(height);
        return out;
    }


    std::shared_ptr<LiteImage::ICombinedImageSampler> make_combined_sampler(const Texture::Info &info, const TextureInstance &inst)
    {
        std::shared_ptr<LiteImage::ICombinedImageSampler> pResult;

        const bool disable_gamma = inst.input_gamma == 1.0f;
        LiteImage::Sampler sampler ;
        sampler.addressU = inst.sampler.addr_mode_u;
        sampler.addressV = inst.sampler.addr_mode_v;
        sampler.addressW = inst.sampler.addr_mode_w;
        sampler.filter = inst.sampler.filter;
        const fs::path path{info.path};
        const std::string ex = path.extension().string();

        if(ex == ".bmp" || ex == ".ppm" || ex == ".jpg" || ex == ".jpeg" || ex == ".png") {

            Image2D<uint32_t> image = LiteImage::LoadImage<uint32_t>(info.path.c_str());
            auto pTexture = std::make_shared<Image2D<uint32_t>>(std::move(image));
            pTexture->setSRGB(!disable_gamma);
            pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);

        }
        else if (ex == ".exr") {
            int wh[2]{ 0, 0 };
            if (info.bpp == 16) {
                const auto image_vect = LoadImage4fFromEXR(info.path.c_str(), &wh[0], &wh[1]);
                auto pTexture = std::make_shared<Image2D<float4>>(wh[0], wh[1], (const float4*)image_vect.data());
                pTexture->setSRGB(false);
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
            else {
                const auto image_vect = LoadImage1fFromEXR(info.path.c_str(), &wh[0], &wh[1]);
                auto pTexture = std::make_shared<Image2D<float>>(wh[0], wh[1], (const float*)image_vect.data());
                pTexture->setSRGB(false);
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
        }
        else if(ex.find(".image") != std::string::npos) { // hydra image formats: image4f, image4ub

            int wh[2] = {0,0};

            std::ifstream fin(info.path.c_str(), std::ios::binary);
            if(!fin.is_open())
              std::cout << "[LoadTextureAndMakeCombined]: can't open '" << info.path << "'" << std::endl;
            
            fin.read((char*)wh, sizeof(int)*2);
            if(wh[0] == 0 || wh[1] == 0) {
                float4 data[1] = {float4(1.0f, 1.0f, 1.0f, 1.0f)};
                auto pTexture  = std::make_shared<Image2D<float4>>(1, 1, data);
                pTexture->setSRGB(false);
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
            else if(info.bpp == 16) { // image4f
                std::vector<float> data(4 * wh[0] * wh[1]);
                fin.read((char*)data.data(), 4 * sizeof(float) * data.size());
                fin.close();

                auto pTexture = std::make_shared<Image2D<float4>>(wh[0], wh[1], (const float4*)data.data());
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
            else {                       // image4ub
                std::vector<uint32_t> data(wh[0]*wh[1]);
                fin.read((char*)data.data(), sizeof(uint32_t)*data.size());
                fin.close();

                auto pTexture = std::make_shared< Image2D<uint32_t> >(wh[0], wh[1], data.data());
                pTexture->setSRGB(!disable_gamma);
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
        }

        return pResult;
    }


/*
    static inline float sRGBToLinear(float s, float gamma) // https://entropymine.com/imageworsener/srgbformula/
    {
      if(s <= 0.0404482362771082f)
        return s*0.077399381f;
      else 
        return std::pow((s+0.055f)*0.947867299f, gamma);
    }

    static inline float4 sRGBToLinear4f(float4 s) { return float4(sRGBToLinear(s.x), sRGBToLinear(s.y), sRGBToLinear(s.z), sRGBToLinear(s.w)); }

    template<typename T>
    std::unique_ptr<IWrappedTexture> wrapped(std::shared_ptr<LiteImage::Image2D<T>> tex, float gamma = 2.4f)
    {
        struct WrappedTextureImpl : public IWrappedTexture
        {
            std::shared_ptr<LiteImage::Image2D<T>> tex;
            float gamma;

            LiteMath::float4 get(int x, int y) const override 
            {
                T value
            }
        };
    }


    std::unique_ptr<IWrappedTexture> make_wrapped_texture(const Texture::Info &info, const TextureInstance &inst)
    {
        std::unique_ptr<IWrappedTexture> result;

        const fs::path path{info.path};
        const std::string ex = path.extension().string();

        if(ex == ".bmp" || ex == ".ppm" || ex == ".jpg" || ex == ".jpeg" || ex == ".png") {

            Image2D<uint32_t> image = LiteImage::LoadImage<uint32_t>(info.path.c_str());
            auto pTexture = std::make_shared<Image2D<uint32_t>>(std::move(image));
            pTexture->setSRGB(!disable_gamma);
            pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);

        }
        else if (ex == ".exr") {
            int wh[2]{ 0, 0 };
            if (info.bpp == 16) {
                const auto image_vect = LoadImage4fFromEXR(info.path.c_str(), &wh[0], &wh[1]);
                auto pTexture = std::make_shared<Image2D<float4>>(wh[0], wh[1], (const float4*)image_vect.data());
                pTexture->setSRGB(false);
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
            else {
                const auto image_vect = LoadImage1fFromEXR(info.path.c_str(), &wh[0], &wh[1]);
                auto pTexture = std::make_shared<Image2D<float>>(wh[0], wh[1], (const float*)image_vect.data());
                pTexture->setSRGB(false);
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
        }
        else if(ex.find(".image") != std::string::npos) { // hydra image formats: image4f, image4ub

            int wh[2] = {0,0};

            std::ifstream fin(info.path.c_str(), std::ios::binary);
            if(!fin.is_open())
              std::cout << "[LoadTextureAndMakeCombined]: can't open '" << info.path << "'" << std::endl;
            
            fin.read((char*)wh, sizeof(int)*2);
            if(wh[0] == 0 || wh[1] == 0) {
                float4 data[1] = {float4(1.0f, 1.0f, 1.0f, 1.0f)};
                auto pTexture  = std::make_shared<Image2D<float4>>(1, 1, data);
                pTexture->setSRGB(false);
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
            else if(info.bpp == 16) { // image4f
                std::vector<float> data(4 * wh[0] * wh[1]);
                fin.read((char*)data.data(), 4 * sizeof(float) * data.size());
                fin.close();

                auto pTexture = std::make_shared<Image2D<float4>>(wh[0], wh[1], (const float4*)data.data());
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
            else {                       // image4ub
                std::vector<uint32_t> data(wh[0]*wh[1]);
                fin.read((char*)data.data(), sizeof(uint32_t)*data.size());
                fin.close();

                auto pTexture = std::make_shared< Image2D<uint32_t> >(wh[0], wh[1], data.data());
                pTexture->setSRGB(!disable_gamma);
                pResult = LiteImage::MakeCombinedTexture2D(pTexture, sampler);
            }
        }

        return pResult;
    }
*/
}