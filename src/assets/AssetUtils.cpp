#include "AssetUtils.h"

namespace hybrid::assets
{

    bool FillMaterialTexture(const aiMaterial &material,
                             aiTextureType type,
                             hybrid::core::scene::TextureColorSpace color_space,
                             AssimpTextureCache &texture_cache,
                             hybrid::core::scene::MaterialTexture &out_texture)
    {
        if (material.GetTextureCount(type) == 0)
        {
            return false;
        }

        aiString path;
        aiTextureMapping mapping = aiTextureMapping_UV;
        unsigned int uv_index = 0;
        float blend = 1.0f;
        aiTextureOp op = aiTextureOp_Multiply;
        std::array<aiTextureMapMode, 2> map_modes = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
        if (material.GetTexture(type, 0, &path, &mapping, &uv_index, &blend, &op, map_modes.data()) != AI_SUCCESS)
        {
            return false;
        }

        const std::string raw_path = path.C_Str();
        if (raw_path.empty())
        {
            return false;
        }

        if (!raw_path.empty() && raw_path[0] == '*')
        {
            return false;
        }

        out_texture.name = raw_path;
        out_texture.image = texture_cache.GetOrLoad(raw_path);
        out_texture.texcoord = static_cast<int>(uv_index);
        out_texture.color_space = color_space;
        out_texture.sampler.wrap_s = ToWrap(map_modes[0]);
        out_texture.sampler.wrap_t = ToWrap(map_modes[1]);

#ifdef AI_MATKEY_UVTRANSFORM
        if (aiUVTransform uv_transform; material.Get(AI_MATKEY_UVTRANSFORM(type, 0), uv_transform) == AI_SUCCESS)
        {
            if (uv_transform.mScaling.x != 1.0f || uv_transform.mScaling.y != 1.0f ||
                uv_transform.mTranslation.x != 0.0f || uv_transform.mTranslation.y != 0.0f ||
                uv_transform.mRotation != 0.0f)
            {
                out_texture.has_transform = true;
                out_texture.scale = {uv_transform.mScaling.x, uv_transform.mScaling.y};
                out_texture.offset = {uv_transform.mTranslation.x, uv_transform.mTranslation.y};
                out_texture.rotation = uv_transform.mRotation;
            }
        }
#endif

        return out_texture.image.IsValid();
    }
}