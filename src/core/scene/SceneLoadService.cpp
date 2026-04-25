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
            std::lock_guard lock(m_mutex);
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
            std::lock_guard lock(m_mutex);
            ++m_latest_request_id;

            if (m_completed.has_value() && m_completed->request_id < m_latest_request_id)
            {
                if (m_completed->success && m_completed->scene_id.IsValid() && m_assets != nullptr)
                {
                    m_assets->Unload(m_completed->scene_id);
                }
                m_completed.reset();
            }

            m_pending_request = SceneLoadRequest{m_latest_request_id, std::move(path)};
            m_state.store(State::Loading, std::memory_order_relaxed);
        }
        m_condition.notify_one();
    }

    bool SceneLoadService::TryConsumeResult(SceneLoadResult &out_result)
    {
        std::lock_guard lock(m_mutex);
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
            SceneLoadRequest request{};
            {
                std::unique_lock lock(m_mutex);
                m_condition.wait(lock, [&]
                                 { return m_shutdown || m_pending_request.has_value(); });
                if (m_shutdown)
                {
                    return;
                }
                request = std::move(*m_pending_request);
                m_pending_request.reset();
                m_state.store(State::Loading, std::memory_order_relaxed);
            }

            if (!m_assets)
            {
                LOG_ERROR("[SceneLoadService] No asset manager available");
                std::lock_guard lock(m_mutex);
                m_completed = SceneLoadResult{request.request_id, std::move(request.path), {}, false};
                m_state.store(State::Failed, std::memory_order_relaxed);
                continue;
            }

            LOG_INFO("[SceneLoadService] Loading scene: " + request.path);
            assets::AssetId id = m_assets->Load<SceneWorld>(request.path);
            const bool success = id.IsValid();

            {
                std::lock_guard lock(m_mutex);
                if (request.request_id != m_latest_request_id)
                {
                    if (success)
                    {
                        m_assets->Unload(id);
                    }

                    LOG_INFO("[SceneLoadService] Ignoring stale scene load result: " + request.path);
                    if (!m_pending_request.has_value())
                    {
                        m_state.store(State::Idle, std::memory_order_relaxed);
                    }
                    continue;
                }

                m_completed = SceneLoadResult{request.request_id, std::move(request.path), id, success};
                m_state.store(success ? State::Loaded : State::Failed, std::memory_order_relaxed);
            }
        }
    }

} // namespace hybrid::core::scene
