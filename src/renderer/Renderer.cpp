#include "renderer/Renderer.h"

#include "renderer/SceneWorldSnapshot.h"

#include <chrono>

namespace hybrid::renderer
{

    struct Renderer::Impl
    {
        RendererConfig config{};
        RenderExtent current_extent{};
        RendererStats stats{};
        RendererOutputs outputs{};
        bool initialized = false;
        std::chrono::steady_clock::time_point frame_start{};
    };

    Renderer::Renderer()
        : m_impl(std::make_unique<Impl>())
    {
    }

    Renderer::~Renderer()
    {
        Shutdown();
    }

    bool Renderer::Init(const RendererConfig &config)
    {
        m_impl->config = config;
        m_impl->current_extent = config.initial_extent;
        m_impl->initialized = true;
        return true;
    }

    void Renderer::Shutdown()
    {
        if (!m_impl->initialized)
        {
            return;
        }

        m_impl->initialized = false;
        m_impl->stats = {};
        m_impl->outputs = {};
        m_impl->current_extent = {};
    }

    void Renderer::Resize(const RenderExtent &extent)
    {
        m_impl->current_extent = extent;
    }

    bool Renderer::BeginFrame(const FrameContext &frame)
    {
        if (!m_impl->initialized)
        {
            return false;
        }

        m_impl->stats = {};
        m_impl->frame_start = std::chrono::steady_clock::now();
        m_impl->current_extent = frame.render_extent;
        return true;
    }

    void Renderer::SubmitScene(const core::scene::SceneWorld &scene_world,
                               const RenderView & /*view*/,
                               const RenderSettings & /*settings*/)
    {
        RenderSceneSnapshot scene = renderer::BuildRenderSceneSnapshot(scene_world);
        m_impl->stats.submitted_mesh_instances =
            static_cast<uint32_t>(scene.mesh_instances.size());

        for (const auto &instance : scene.mesh_instances)
        {
            const auto *mesh = instance.mesh.Get();
            if (!mesh)
            {
                continue;
            }

            for (const auto &primitive : mesh->primitives)
            {
                m_impl->stats.submitted_primitives++;
                m_impl->stats.submitted_vertices += primitive.vertices.size();
                m_impl->stats.submitted_triangles += primitive.indices.size() / 3;
            }
        }
    }

    RendererOutputs Renderer::EndFrame()
    {
        if (!m_impl->initialized)
        {
            return {};
        }

        const auto frame_end = std::chrono::steady_clock::now();
        m_impl->stats.cpu_frame_ms =
            std::chrono::duration<double, std::milli>(frame_end - m_impl->frame_start).count();
        return m_impl->outputs;
    }

    const RendererStats &Renderer::GetStats() const
    {
        return m_impl->stats;
    }

} // namespace hybrid::renderer
