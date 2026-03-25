#pragma once

#include "ui/panels/Panel.h"

#include <cstdint>
#include <string>

namespace hybrid::ui
{

    class ToolbarPanel final : public Panel
    {
    public:
        ToolbarPanel();

    private:
        struct IconTexture
        {
            uint32_t id = 0;
            int width = 0;
            int height = 0;
        };

        enum class TransformTool : uint8_t
        {
            Translate = 0,
            Rotate,
            Scale
        };

        static std::string BuildIconPath(const char *icon_file);
        static IconTexture LoadIconTexture(const char *icon_file);

        ImGuiWindowFlags WindowFlags() const override;
        ImGuiDockNodeFlags DockNodeFlags() const override;
        void DrawContents(PanelContext &context) override;

        IconTexture m_light_icon{};
        IconTexture m_translate_icon{};
        IconTexture m_rotate_icon{};
        IconTexture m_scale_icon{};
        IconTexture m_local_space_icon{};
        IconTexture m_global_space_icon{};

        TransformTool m_transform_tool = TransformTool::Translate;
        bool m_use_local_space = true;
    };

} // namespace hybrid::ui
