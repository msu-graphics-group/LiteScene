#ifndef INCLUDE_LITESCENE_TEXTURE_H_
#define INCLUDE_LITESCENE_TEXTURE_H_

#include <LiteScene/sceneobj.h>


namespace ls {

    class Texture : public SceneObject
    {
    public:
        using SceneObject::SceneObject;
        //std::shared_ptr<LiteImage::ICombinedImageSampler> sampler;
    };

}

#endif