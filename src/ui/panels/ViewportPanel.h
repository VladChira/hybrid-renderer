#pragma once

#include "Panel.h"

#include <imgui.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace hybrid::ui
{

    class ViewportPanel final : public Panel
    {
    public:
        ViewportPanel();

    private:
        ImGuiWindowFlags WindowFlags() const override;
        void DrawContents(PanelContext &context) override;

        struct OrbitState
        {
            bool active = false;
            entt::entity camera_entity = entt::null;
            glm::vec3 pivot{0.0f};
            ImVec2 last_mouse{0.0f, 0.0f};
        };

        struct PanState
        {
            bool active = false;
            entt::entity camera_entity = entt::null;
            ImVec2 last_mouse{0.0f, 0.0f};
        };

        OrbitState m_orbit{};
        PanState m_pan{};
        ImVec2 m_last_content_size{0.0f, 0.0f};
    };

} // namespace hybrid::ui
