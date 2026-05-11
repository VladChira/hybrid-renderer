#include "ToolbarPanel.h"

#include "core/Log.h"

#include <glad.h>
#include <imgui.h>
#include <stb_image.h>

#include <string>

namespace hybrid::ui
{

    namespace
    {
        constexpr ImVec2 kIconButtonSize{32.0f, 32.0f};

        bool DrawIconButton(const char *id, const char *tooltip, uint32_t texture_id)
        {
            bool pressed = false;
            if (texture_id != 0)
            {
                pressed = ImGui::ImageButton(id,
                                             static_cast<ImTextureID>(static_cast<intptr_t>(texture_id)),
                                             kIconButtonSize,
                                             ImVec2(0.0f, 0.0f),
                                             ImVec2(1.0f, 1.0f));
            }
            else
            {
                pressed = ImGui::Button(id, kIconButtonSize);
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", tooltip);
            }
            return pressed;
        }

        bool DrawToggleIconButton(const char *id, const char *tooltip, bool active, uint32_t texture_id)
        {
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }

            const bool pressed = DrawIconButton(id, tooltip, texture_id);

            if (active)
            {
                ImGui::PopStyleColor();
            }

            return pressed;
        }
    } // namespace

    ToolbarPanel::ToolbarPanel()
        : Panel("Toolbar")
    {
        m_light_icon = LoadIconTexture("point_dark.png");
        m_camera_icon = LoadIconTexture("camera_dark.png");
        m_translate_icon = LoadIconTexture("translate_dark.png");
        m_rotate_icon = LoadIconTexture("rotate_dark.png");
        m_scale_icon = LoadIconTexture("scale_dark.png");
        m_local_space_icon = LoadIconTexture("coordinate_local_dark.png");
        m_global_space_icon = LoadIconTexture("coordinate_global_dark.png");
    }

    std::string ToolbarPanel::BuildIconPath(const char *icon_file)
    {
        return std::string(HYBRID_PROJECT_ROOT) + "/assets/icons/" + icon_file;
    }

    ToolbarPanel::IconTexture ToolbarPanel::LoadIconTexture(const char *icon_file)
    {
        const std::string path = BuildIconPath(icon_file);

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char *pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            LOG_WARN(std::string("Toolbar icon failed to load: ") + path);
            return {};
        }

        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     width,
                     height,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(pixels);

        return IconTexture{texture, width, height};
    }

    ImGuiWindowFlags ToolbarPanel::WindowFlags() const
    {
        return ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }

    ImGuiDockNodeFlags ToolbarPanel::DockNodeFlags() const
    {
        return ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoTabBar;
    }

    void ToolbarPanel::DrawContents(PanelContext &context)
    {
        TransformTool active_tool = (context.transform_tool != nullptr)
                                        ? *context.transform_tool
                                        : TransformTool::Translate;

        if (DrawIconButton("##toolbar_add_point_light", "Add Point Light", m_light_icon.id))
        {
            // Hooked into command flow; scene mutation is processed by core.
            if (context.commands != nullptr)
            {
                EnqueueCommand(*context.commands, AddPointLightCommand{});
            }
        }
        ImGui::SameLine();
        if (DrawIconButton("##toolbar_add_area_light", "Add Area Light", m_light_icon.id))
        {
            if (context.commands != nullptr)
            {
                EnqueueCommand(*context.commands, AddAreaLightCommand{});
            }
        }
        ImGui::SameLine();
        if (DrawIconButton("##toolbar_add_directional_light", "Add Directional Light", m_light_icon.id))
        {
            if (context.commands != nullptr)
            {
                EnqueueCommand(*context.commands, AddDirectionalLightCommand{});
            }
        }
        ImGui::SameLine();
        if (DrawIconButton("##toolbar_add_camera", "Add Camera", m_camera_icon.id))
        {
            if (context.commands != nullptr)
            {
                EnqueueCommand(*context.commands, AddCameraCommand{});
            }
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::Button("Save"))
        {
            if (context.commands != nullptr)
            {
                EnqueueCommand(*context.commands, SaveFrameCaptureCommand{});
            }
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (DrawToggleIconButton("##toolbar_tool_translate",
                                 "Translate Tool",
                                 active_tool == TransformTool::Translate,
                                 m_translate_icon.id))
        {
            if (context.transform_tool != nullptr)
            {
                *context.transform_tool = TransformTool::Translate;
            }
            active_tool = TransformTool::Translate;
        }
        ImGui::SameLine();
        if (DrawToggleIconButton("##toolbar_tool_rotate",
                                 "Rotate Tool",
                                 active_tool == TransformTool::Rotate,
                                 m_rotate_icon.id))
        {
            if (context.transform_tool != nullptr)
            {
                *context.transform_tool = TransformTool::Rotate;
            }
            active_tool = TransformTool::Rotate;
        }
        ImGui::SameLine();
        if (DrawToggleIconButton("##toolbar_tool_scale",
                                 "Scale Tool",
                                 active_tool == TransformTool::Scale,
                                 m_scale_icon.id))
        {
            if (context.transform_tool != nullptr)
            {
                *context.transform_tool = TransformTool::Scale;
            }
            active_tool = TransformTool::Scale;
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (DrawToggleIconButton("##toolbar_space_local", "Local Space", m_use_local_space, m_local_space_icon.id))
        {
            m_use_local_space = true;
        }
        ImGui::SameLine();
        if (DrawToggleIconButton("##toolbar_space_global", "Global Space", !m_use_local_space, m_global_space_icon.id))
        {
            m_use_local_space = false;
        }

        if (context.transform_tool != nullptr)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_T))
            {
                *context.transform_tool = TransformTool::Translate;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_R))
            {
                *context.transform_tool = TransformTool::Rotate;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_S))
            {
                *context.transform_tool = TransformTool::Scale;
            }
        }
    }

} // namespace hybrid::ui
