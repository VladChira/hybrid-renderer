#include "ContentBrowserPanel.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace hybrid::ui
{
    namespace
    {
        constexpr char kDialogKey[] = "ContentBrowserPanelDialog";
        constexpr char kDialogTitle[] = "Content Browser";
        constexpr char kAllFilesFilter[] = ".*";

        std::string GetDefaultScenesPath()
        {
            return std::string(HYBRID_PROJECT_ROOT) + "/assets/scenes";
        }
    } // namespace

    ContentBrowserPanel::ContentBrowserPanel()
        : Panel("Content Browser")
    {
    }

    void ContentBrowserPanel::DrawContents(PanelContext &context)
    {
        (void)context;

        if (!m_dialog_initialized)
        {
            IGFD::FileDialogConfig config;
            config.path = m_current_path.empty() ? GetDefaultScenesPath() : m_current_path;
            config.flags = ImGuiFileDialogFlags_NoDialog;
            m_file_dialog.OpenDialog(kDialogKey, kDialogTitle, kAllFilesFilter, config);
            m_dialog_initialized = true;
        }

        const float dialog_height = std::max(320.0f, ImGui::GetContentRegionAvail().y);
        if (m_file_dialog.Display(kDialogKey, ImGuiWindowFlags_NoCollapse, ImVec2(0.0f, dialog_height)))
        {
            m_current_path = m_file_dialog.GetCurrentPath();
            if (m_file_dialog.IsOk())
            {
                m_selected_path = m_file_dialog.GetFilePathName();
            }

            m_file_dialog.Close();
            m_dialog_initialized = false;
        }

        if (!m_selected_path.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Selected Path");
            ImGui::TextWrapped("%s", m_selected_path.c_str());
        }
    }

} // namespace hybrid::ui
