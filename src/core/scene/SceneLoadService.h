#pragma once

#include "assets/AssetManager.h"
#include "core/scene/SceneWorld.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace hybrid::core::scene
{

    struct SceneLoadResult
    {
        std::string path;
        assets::AssetId scene_id{};
        bool success = false;
    };

    class SceneLoadService
    {
    public:
        enum class State
        {
            Idle,
            Loading,
            Loaded,
            Failed
        };

        explicit SceneLoadService(assets::AssetManager *assets);
        ~SceneLoadService();

        SceneLoadService(const SceneLoadService &) = delete;
        SceneLoadService &operator=(const SceneLoadService &) = delete;

        void RequestLoad(std::string path);
        bool TryConsumeResult(SceneLoadResult &out_result);
        State GetState() const;

    private:
        void WorkerLoop();

        assets::AssetManager *m_assets = nullptr;
        std::atomic<State> m_state{State::Idle};

        std::mutex m_mutex;
        std::condition_variable m_condition;
        std::optional<std::string> m_pending_path;
        std::optional<SceneLoadResult> m_completed;
        bool m_shutdown = false;

        std::thread m_worker;
    };

} // namespace hybrid::core::scene
