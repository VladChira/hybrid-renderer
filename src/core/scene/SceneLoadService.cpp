#include "core/scene/SceneLoadService.h"

#include "core/Log.h"

#include <utility>

namespace hybrid::core::scene
{

    SceneLoadService::SceneLoadService(assets::AssetManager *assets)
        : m_assets(assets)
    {
        m_worker = std::thread(&SceneLoadService::WorkerLoop, this);
    }

    SceneLoadService::~SceneLoadService()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_shutdown = true;
        }
        m_condition.notify_all();
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }

    void SceneLoadService::RequestLoad(std::string path)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending_path = std::move(path);
        }
        m_condition.notify_one();
    }

    bool SceneLoadService::TryConsumeResult(SceneLoadResult &out_result)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_completed.has_value())
        {
            return false;
        }
        out_result = std::move(*m_completed);
        m_completed.reset();
        return true;
    }

    SceneLoadService::State SceneLoadService::GetState() const
    {
        return m_state.load(std::memory_order_relaxed);
    }

    void SceneLoadService::WorkerLoop()
    {
        for (;;)
        {
            std::string path;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [&]
                                 { return m_shutdown || m_pending_path.has_value(); });
                if (m_shutdown)
                {
                    return;
                }
                path = std::move(*m_pending_path);
                m_pending_path.reset();
                m_state.store(State::Loading, std::memory_order_relaxed);
            }

            if (!m_assets)
            {
                LOG_ERROR("[SceneLoadService] No asset manager available");
                std::lock_guard<std::mutex> lock(m_mutex);
                m_completed = SceneLoadResult{std::move(path), {}, false};
                m_state.store(State::Failed, std::memory_order_relaxed);
                continue;
            }

            LOG_INFO("[SceneLoadService] Loading scene: " + path);
            assets::AssetId id = m_assets->Load<SceneWorld>(path);
            const bool success = id.IsValid();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_completed = SceneLoadResult{std::move(path), id, success};
                m_state.store(success ? State::Loaded : State::Failed, std::memory_order_relaxed);
            }
        }
    }

} // namespace hybrid::core::scene
