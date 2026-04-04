#pragma once

#include "ui/panels/Panel.h"
#include "renderer/RendererTypes.h"

namespace hybrid::ui
{

    class SettingsPanel final : public Panel
    {
    public:
        SettingsPanel();

    private:
        void DrawContents(PanelContext &context) override;

        renderer::RenderSettings m_pending_render_settings{};
        bool m_has_pending_render_settings = false;
        bool m_render_settings_dirty = false;
        bool m_commit_requested = false;
        double m_last_edit_time_seconds = 0.0;
    };

} // namespace hybrid::ui
