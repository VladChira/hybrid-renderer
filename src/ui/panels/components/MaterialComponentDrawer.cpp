#include "MaterialComponentDrawer.h"

#include "core/scene/types/SceneAssets.h"

#include <ImGuiFileDialog.h>
#include <imgui.h>

#include <cfloat>
#include <string>
#include <vector>

namespace hybrid::ui
{
    namespace
    {
        constexpr char kMaterialTextureDialogKey[] = "MaterialComponentDrawerTextureDialog";
        constexpr char kMaterialTextureDialogTitle[] = "Pick Texture";
        constexpr char kAllFilesFilter[] = ".*";

        enum class MaterialTextureSlot
        {
            BaseColor,
            MetallicRoughness,
            Normal,
            Occlusion,
            Emissive
        };

        const char *ToString(core::scene::AlphaMode mode)
        {
            switch (mode)
            {
            case core::scene::AlphaMode::Opaque:
                return "Opaque";
            case core::scene::AlphaMode::Mask:
                return "Mask";
            case core::scene::AlphaMode::Blend:
                return "Blend";
            }

            return "Unknown";
        }

        const char *TextureSlotLabel(MaterialTextureSlot slot)
        {
            switch (slot)
            {
            case MaterialTextureSlot::BaseColor:
                return "Base color";
            case MaterialTextureSlot::MetallicRoughness:
                return "Metallic/Roughness";
            case MaterialTextureSlot::Normal:
                return "Normal";
            case MaterialTextureSlot::Occlusion:
                return "Occlusion";
            case MaterialTextureSlot::Emissive:
                return "Emissive";
            }

            return "Texture";
        }

        const core::scene::MaterialTexture &ResolveTextureSlot(const core::scene::MaterialAsset &material,
                                                               MaterialTextureSlot slot)
        {
            switch (slot)
            {
            case MaterialTextureSlot::BaseColor:
                return material.base_color_texture;
            case MaterialTextureSlot::MetallicRoughness:
                return material.metallic_roughness_texture;
            case MaterialTextureSlot::Normal:
                return material.normal_texture;
            case MaterialTextureSlot::Occlusion:
                return material.occlusion_texture;
            case MaterialTextureSlot::Emissive:
                return material.emissive_texture;
            }

            return material.base_color_texture;
        }

        std::string GetDefaultTexturesPath()
        {
            return std::string(HYBRID_PROJECT_ROOT) + "/assets";
        }

        void AppendTexturePath(std::string &output,
                               const char *label,
                               const core::scene::MaterialTexture &texture)
        {
            if (texture.name.empty())
            {
                return;
            }

            if (!output.empty())
            {
                output += '\n';
            }

            output += label;
            output += ": ";
            output += texture.name;
        }

        std::string BuildTexturePathList(const core::scene::MaterialAsset &material)
        {
            std::string texture_paths;
            AppendTexturePath(texture_paths, "Base color", material.base_color_texture);
            AppendTexturePath(texture_paths, "Metallic/Roughness", material.metallic_roughness_texture);
            AppendTexturePath(texture_paths, "Normal", material.normal_texture);
            AppendTexturePath(texture_paths, "Occlusion", material.occlusion_texture);
            AppendTexturePath(texture_paths, "Emissive", material.emissive_texture);

            if (texture_paths.empty())
            {
                texture_paths = "<none>";
            }

            return texture_paths;
        }

        void DrawTexturePickerRow(const core::scene::MaterialAsset &material,
                                  MaterialTextureSlot slot,
                                  ImGuiFileDialog &file_dialog,
                                  bool &dialog_initialized,
                                  std::string &current_path,
                                  MaterialTextureSlot &pending_slot,
                                  std::string &pending_preview_path)
        {
            const core::scene::MaterialTexture &texture = ResolveTextureSlot(material, slot);
            const std::string button_label = std::string("Pick##material_texture_") + TextureSlotLabel(slot);
            if (ImGui::Button(button_label.c_str()))
            {
                IGFD::FileDialogConfig config;
                config.path = current_path.empty() ? GetDefaultTexturesPath() : current_path;
                config.flags = ImGuiFileDialogFlags_NoDialog;
                file_dialog.OpenDialog(kMaterialTextureDialogKey, kMaterialTextureDialogTitle, kAllFilesFilter, config);
                dialog_initialized = true;
                pending_slot = slot;
            }

            ImGui::SameLine();
            ImGui::Text("%s: %s",
                        TextureSlotLabel(slot),
                        texture.name.empty() ? "<none>" : texture.name.c_str());

            if (!pending_preview_path.empty() && pending_slot == slot)
            {
                ImGui::TextWrapped("Pending pick: %s", pending_preview_path.c_str());
            }
        }
    } // namespace

    void DrawMaterialComponent(const core::scene::MaterialAsset &material,
                               uint64_t material_id,
                               CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        static ImGuiFileDialog file_dialog;
        static bool dialog_initialized = false;
        static std::string current_path;
        static MaterialTextureSlot pending_slot = MaterialTextureSlot::BaseColor;
        static std::string pending_preview_path;

        const char *name = material.name.empty() ? "<unnamed>" : material.name.c_str();
        ImGui::Text("Name: %s", name);
        ImGui::Separator();

        float base_color[4] = {material.base_color_factor.r,
                               material.base_color_factor.g,
                               material.base_color_factor.b,
                               material.base_color_factor.a};
        float metallic_factor = material.metallic_factor;
        float roughness_factor = material.roughness_factor;

        if (ImGui::ColorEdit4("Base Color", base_color))
        {
            EnqueueCommand(*commands, MaterialSetVec4Command{material_id, MaterialVec4Property::BaseColorFactor,
                                                             glm::vec4(base_color[0], base_color[1],
                                                                       base_color[2], base_color[3])});
        }
        ImGui::DragFloat("Metallic", &metallic_factor, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Roughness", &roughness_factor, 0.01f, 0.0f, 1.0f);

        ImGui::Text("Emissive: %.3f %.3f %.3f",
                    material.emissive_factor.r,
                    material.emissive_factor.g,
                    material.emissive_factor.b);
        ImGui::Text("Alpha Mode: %s", ToString(material.alpha_mode));
        ImGui::Text("Alpha Cutoff: %.3f", material.alpha_cutoff);
        ImGui::Text("Double Sided: %s", material.double_sided ? "Yes" : "No");
        ImGui::Separator();

        ImGui::TextUnformatted("Texture pickers:");
        DrawTexturePickerRow(material, MaterialTextureSlot::BaseColor, file_dialog, dialog_initialized, current_path, pending_slot, pending_preview_path);
        DrawTexturePickerRow(material, MaterialTextureSlot::MetallicRoughness, file_dialog, dialog_initialized, current_path, pending_slot, pending_preview_path);
        DrawTexturePickerRow(material, MaterialTextureSlot::Normal, file_dialog, dialog_initialized, current_path, pending_slot, pending_preview_path);
        DrawTexturePickerRow(material, MaterialTextureSlot::Occlusion, file_dialog, dialog_initialized, current_path, pending_slot, pending_preview_path);
        DrawTexturePickerRow(material, MaterialTextureSlot::Emissive, file_dialog, dialog_initialized, current_path, pending_slot, pending_preview_path);

        if (dialog_initialized && file_dialog.Display(kMaterialTextureDialogKey, ImGuiWindowFlags_NoCollapse, ImVec2(0.0f, 320.0f)))
        {
            current_path = file_dialog.GetCurrentPath();
            if (file_dialog.IsOk())
            {
                pending_preview_path = file_dialog.GetFilePathName();
            }

            file_dialog.Close();
            dialog_initialized = false;
        }

        if (!pending_preview_path.empty())
        {
            ImGui::TextWrapped("Selected texture for %s: %s",
                               TextureSlotLabel(pending_slot),
                               pending_preview_path.c_str());
            ImGui::TextDisabled("Picker UI only for now; swap is not wired yet.");
        }
        ImGui::Separator();

        const std::string texture_paths = BuildTexturePathList(material);
        std::vector<char> texture_path_buffer(texture_paths.begin(), texture_paths.end());
        texture_path_buffer.push_back('\0');

        ImGui::TextUnformatted("Texture paths:");
        ImGui::InputTextMultiline("##material_texture_paths",
                                  texture_path_buffer.data(),
                                  texture_path_buffer.size(),
                                  ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 6.0f),
                                  ImGuiInputTextFlags_ReadOnly);
    }

} // namespace hybrid::ui
