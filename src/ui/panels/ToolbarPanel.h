#pragma once

#include "ui/panels/Panel.h"

#include "ui/UiState.h"

#include <cstdint>
#include <string>

namespace hybrid::ui
{

    class Ui;  // forward — used to dispatch icon uploads on the Vulkan path

    class ToolbarPanel final : public Panel
    {
    public:
        // `ui` is consulted to upload icon textures on the Vulkan path. May
        // be nullptr in OpenGL mode (icons are uploaded inline via glad).
        explicit ToolbarPanel(Ui *ui = nullptr);

    private:
        struct IconTexture
        {
            // GLuint (uint32) on the GL path or VkDescriptorSet (cast to
            // uint64) on the Vulkan path. ImGui's ImTextureID accepts both
            // via the same intptr_t cast in DrawIconButton.
            uint64_t id = 0;
            int width = 0;
            int height = 0;
        };

        static std::string BuildIconPath(const char *icon_file);
        IconTexture LoadIconTexture(const char *icon_file);

        ImGuiWindowFlags WindowFlags() const override;
        ImGuiDockNodeFlags DockNodeFlags() const override;
        void DrawContents(PanelContext &context) override;

        Ui *m_ui = nullptr;

        IconTexture m_light_icon{};
        IconTexture m_camera_icon{};
        IconTexture m_translate_icon{};
        IconTexture m_rotate_icon{};
        IconTexture m_scale_icon{};
        IconTexture m_local_space_icon{};
        IconTexture m_global_space_icon{};

        bool m_use_local_space = true;
    };

} // namespace hybrid::ui
