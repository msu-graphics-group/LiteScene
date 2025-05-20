#ifndef TEXTURES_UTIL_H_
#define TEXTURES_UTIL_H_
#include "scene.h"

#include <memory>

namespace LiteScene
{
    std::shared_ptr<LiteImage::ICombinedImageSampler> make_combined_sampler(const Texture::Info &info, const TextureInstance &inst);


/*
    struct IWrappedTexture
    {
        virtual ~IWrappedTexture() = default; 
        virtual LiteMath::float4 get(int x, int y) const = 0;
    };
*/

    inline float toSRGB(float s, float gamma = 2.4) // https://entropymine.com/imageworsener/srgbformula/
    {
      if(s <= 0.0031308f)
        return s*12.92f;
      else 
        return 1.055 * std::pow(s, 1 / gamma) - 0.055;
    }
    inline LiteMath::float4 toSRGB(LiteMath::float4 s, float gamma = 2.4) { return LiteMath::float4(toSRGB(s.x), toSRGB(s.y), toSRGB(s.z), toSRGB(s.w)); }

}


#endif
