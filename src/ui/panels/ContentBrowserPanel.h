#pragma once

#include "ui/panels/Panel.h"

#include <ImGuiFileDialog.h>

#include <string>

namespace hybrid::ui
{

    class ContentBrowserPanel final : public Panel
    {
    public:
        ContentBrowserPanel();

    private:
        void DrawContents(PanelContext &context) override;

        ImGuiFileDialog m_file_dialog;
        bool m_dialog_initialized = false;
        std::string m_current_path;
        std::string m_selected_path;
    };

} // namespace hybrid::ui
