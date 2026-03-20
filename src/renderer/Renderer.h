#pragma once

#include "renderer/RendererTypes.h"
#include "core/scene/SceneWorld.h"

#include <memory>

namespace hybrid::renderer
{

    class Renderer
    {
    public:
        struct Impl;

        Renderer();
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;
        Renderer(Renderer &&) noexcept = default;
        Renderer &operator=(Renderer &&) noexcept = default;

        bool Init();
        void Shutdown();

        void Resize(const RenderExtent &extent);

        bool BeginFrame(const FrameContext &frame);
        void SubmitScene(const core::scene::SceneWorld &scene_world,
                         const RenderView &view,
                         const RenderSettings &settings);
        RendererOutputs EndFrame();

        const RendererStats &GetStats() const;

    private:
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer
