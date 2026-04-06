#pragma once

#include "core/scene/SceneWorld.h"
#include "renderer/RendererTypes.h"

#include <memory>

namespace hybrid::renderer
{
    class SceneFrameCache
    {
    public:
        struct Impl;

        SceneFrameCache();
        ~SceneFrameCache();

        SceneFrameCache(const SceneFrameCache &) = delete;
        SceneFrameCache &operator=(const SceneFrameCache &) = delete;
        SceneFrameCache(SceneFrameCache &&) noexcept;
        SceneFrameCache &operator=(SceneFrameCache &&) noexcept;

        void Reset();
        void Sync(core::scene::SceneWorld &scene_world);
        const FrameSceneData &GetFrameData() const;

    private:
        std::unique_ptr<Impl> m_impl;
    };

    FrameSceneData BuildFrameSceneData(const core::scene::SceneWorld &scene_world);

} // namespace hybrid::renderer
