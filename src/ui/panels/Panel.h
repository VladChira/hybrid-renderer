#pragma once

#include "ui/UiCommands.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hybrid::ui
{

    struct ThemePalette;
    struct UiState;
    struct UiSelection;
    enum class UiViewportVisualization : uint8_t;
    enum class TransformTool : uint8_t;

    struct PanelContext
    {
        float delta_seconds = 0.0f;
        CommandBuffer *commands = nullptr;
        const ThemePalette *theme = nullptr;
        const UiState *state = nullptr;
        UiSelection *selection = nullptr;
        UiViewportVisualization *viewport_visualization = nullptr;
        TransformTool *transform_tool = nullptr;
    };

    class Panel
    {
    public:
        explicit Panel(std::string title);
        virtual ~Panel() = default;

        const std::string &Title() const noexcept;
        bool IsOpen() const noexcept;
        void SetOpen(bool open) noexcept;

        void Render(PanelContext &context);

    protected:
        virtual ImGuiWindowFlags WindowFlags() const;
        virtual ImGuiDockNodeFlags DockNodeFlags() const;
        virtual void DrawContents(PanelContext &context) = 0;

    private:
        std::string m_title;
        bool m_is_open = true;
    };

    class PanelRegistry
    {
    public:
        void Register(std::unique_ptr<Panel> panel);
        void Clear();
        bool Empty() const noexcept;

        void DrawAll(PanelContext &context) const;

    private:
        std::vector<std::unique_ptr<Panel>> m_panels;
    };

} // namespace hybrid::ui
