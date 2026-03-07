#pragma once

#include "ui/UiCommands.h"

#include <memory>
#include <string>
#include <vector>

namespace hybrid::ui
{

    struct PanelContext
    {
        float delta_seconds = 0.0f;
        CommandBuffer *commands = nullptr;
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
