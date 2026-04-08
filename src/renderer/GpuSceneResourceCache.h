#pragma once

#include "core/scene/types/SceneAssets.h"
#include "renderer/opengl/GLBuffer.h"
#include "renderer/opengl/GLTexture.h"
#include "renderer/opengl/GLVertexArray.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace hybrid::renderer
{

    class GpuSceneResourceCache
    {
    public:
        struct PrimitiveCacheKey
        {
            uint64_t mesh_id = 0;
            uint32_t primitive_index = 0;

            bool operator==(const PrimitiveCacheKey &other) const
            {
                return mesh_id == other.mesh_id && primitive_index == other.primitive_index;
            }
        };

        struct PrimitiveCacheKeyHash
        {
            size_t operator()(const PrimitiveCacheKey &key) const noexcept;
        };

        struct CachedPrimitiveGpu
        {
            GLVertexArray vao{};
            GLBuffer vertex_buffer{GL_ARRAY_BUFFER};
            GLBuffer index_buffer{GL_ELEMENT_ARRAY_BUFFER};
            GLsizei index_count = 0;
        };

        struct CachedTextureGpu
        {
            GLTexture texture{GL_TEXTURE_2D};
        };

        bool GetOrUploadPrimitive(uint64_t mesh_id,
                                  uint32_t primitive_index,
                                  const core::scene::MeshPrimitive &primitive,
                                  CachedPrimitiveGpu *&out_gpu,
                                  bool &out_cache_miss,
                                  bool &out_uploaded);

        bool GetOrUploadTexture(const core::scene::MaterialTexture &texture,
                                CachedTextureGpu *&out_gpu,
                                bool &out_cache_miss,
                                bool &out_uploaded);

        const std::unordered_map<PrimitiveCacheKey, CachedPrimitiveGpu, PrimitiveCacheKeyHash> &PrimitiveCache() const
        {
            return m_primitive_cache;
        }

        const std::unordered_map<uint64_t, CachedTextureGpu> &TextureCache() const
        {
            return m_texture_cache;
        }

        void Clear();

    private:
        std::unordered_map<PrimitiveCacheKey, CachedPrimitiveGpu, PrimitiveCacheKeyHash> m_primitive_cache;
        std::unordered_map<uint64_t, CachedTextureGpu> m_texture_cache;
    };

} // namespace hybrid::renderer
