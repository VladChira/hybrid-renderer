#include "HdriLightComponentDrawer.h"

#include <ImGuiFileDialog.h>
#include <imgui.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace hybrid::ui
{
    namespace
    {
        constexpr char kHdriDialogKey[] = "HdriLightComponentDrawerDialog";
        constexpr char kHdriDialogTitle[] = "Pick HDRI";
        constexpr char kAllFilesFilter[] = ".*";

        std::string GetDefaultHdrisPath()
        {
            return std::string(HYBRID_PROJECT_ROOT) + "/assets/hdris";
        }

        bool IsHdriPath(const std::string &path)
        {
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(),
                           extension.end(),
                           extension.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return extension == ".hdr" || extension == ".exr";
        }
    } // namespace

    void DrawHdriLightComponent(entt::entity entity,
                                const core::scene::HdriLightComponent &component,
                                CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("HDRI Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        static ImGuiFileDialog file_dialog;
        static bool dialog_initialized = false;
        static std::string current_path;

        if (ImGui::Button("Pick HDRI"))
        {
            IGFD::FileDialogConfig config;
            config.path = current_path.empty() ? GetDefaultHdrisPath() : current_path;
            config.flags = ImGuiFileDialogFlags_NoDialog;
            file_dialog.OpenDialog(kHdriDialogKey, kHdriDialogTitle, kAllFilesFilter, config);
            dialog_initialized = true;
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("Texture path:");
        ImGui::TextWrapped("%s", component.texture_path.empty() ? "<none>" : component.texture_path.c_str());

        if (dialog_initialized && file_dialog.Display(kHdriDialogKey, ImGuiWindowFlags_NoCollapse, ImVec2(0.0f, 320.0f)))
        {
            current_path = file_dialog.GetCurrentPath();
            if (file_dialog.IsOk())
            {
                const std::string selected_path = file_dialog.GetFilePathName();
                if (commands != nullptr && IsHdriPath(selected_path))
                {
                    EditHdriLightCommand command{};
                    command.entity = entity;
                    command.yaw_radians = component.yaw_radians;
                    command.texture_path = selected_path;
                    EnqueueCommand(*commands, command);
                }
            }

            file_dialog.Close();
            dialog_initialized = false;
        }

        float yaw_radians = component.yaw_radians;
        if (ImGui::DragFloat("Yaw (rad)", &yaw_radians, 0.01f, -100.0f, 100.0f, "%.3f") && commands != nullptr)
        {
            EditHdriLightCommand command{};
            command.entity = entity;
            command.yaw_radians = yaw_radians;
            EnqueueCommand(*commands, command);
        }

        ImGui::Text("Yaw (deg): %.3f", glm::degrees(yaw_radians));
    }

} // namespace hybrid::ui
