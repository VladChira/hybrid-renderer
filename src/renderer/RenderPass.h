#pragma once

#include "renderer/RendererTypes.h"

#include <memory>
#include <vector>

namespace hybrid::core::scene
{
    class SceneWorld;
}

namespace hybrid::renderer
{

    struct PassInputs
    {
        const RenderSettings *settings = nullptr;
    };

    struct PassTargets
    {
        uint32_t scene_framebuffer_id = 0;
        uint32_t gbuffer_framebuffer_id = 0;
        RendererOutputHandle scene_color{};
        RendererOutputHandle scene_depth{};
        RendererOutputHandle gbuffer_rt0{};
        RendererOutputHandle gbuffer_rt1{};
        RendererOutputHandle gbuffer_depth{};
    };

    struct PassContext
    {
        PassInputs inputs{};
        const FrameSceneData *scene_data = nullptr;
        const RenderView *effective_view = nullptr;
        RendererStats *stats = nullptr;
        PassTargets targets{};
        RendererOutputs *outputs = nullptr;
    };

    class IRenderPass
    {
    public:
        virtual ~IRenderPass() = default;
        virtual const char *Name() const = 0;
        virtual bool Execute(PassContext &context) = 0;
    };

    class LinearPassRunner
    {
    public:
        void Clear()
        {
            m_passes.clear();
        }

        void AddPass(std::unique_ptr<IRenderPass> pass)
        {
            if (pass)
            {
                m_passes.push_back(std::move(pass));
            }
        }

        bool Execute(PassContext &context) const
        {
            for (const auto &pass : m_passes)
            {
                if (!pass)
                {
                    continue;
                }
                if (!pass->Execute(context))
                {
                    return false;
                }
            }

            return true;
        }

    private:
        std::vector<std::unique_ptr<IRenderPass>> m_passes;
    };

} // namespace hybrid::renderer
