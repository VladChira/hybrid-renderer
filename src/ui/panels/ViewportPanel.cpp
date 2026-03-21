#include "ViewportPanel.h"

#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"
#include "ui/UiState.h"

#include <ImGuizmo.h>

#include <algorithm>
#include <cstdint>
#include <cmath>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace hybrid::ui
{
    namespace
    {
        uint64_t ResolveViewportTexture(const UiState &state)
        {
            switch (state.viewport_visualization)
            {
            case UiViewportVisualization::FinalColor:
                return state.viewport_color_texture;
            case UiViewportVisualization::GBufferRt0:
                return state.viewport_gbuffer_rt0_texture != 0
                           ? state.viewport_gbuffer_rt0_texture
                           : state.viewport_color_texture;
            case UiViewportVisualization::GBufferRt1:
                return state.viewport_gbuffer_rt1_texture != 0
                           ? state.viewport_gbuffer_rt1_texture
                           : state.viewport_color_texture;
            }

            return state.viewport_color_texture;
        }
    } // namespace

    ViewportPanel::ViewportPanel()
        : Panel("Viewport")
    {
    }

    ImGuiWindowFlags ViewportPanel::WindowFlags() const
    {
        return ImGuiWindowFlags_NoBackground;
    }

    void ViewportPanel::DrawContents(PanelContext &context)
    {
        const ImVec2 current_size = ImGui::GetContentRegionAvail();
        if (context.state != nullptr)
        {
            const uint64_t viewport_texture = ResolveViewportTexture(*context.state);
            if (viewport_texture == 0)
            {
                m_last_content_size = current_size;
                return;
            }

            const auto &render_extent = context.state->viewport_render_extent;
            const float render_width = static_cast<float>(std::max(render_extent.width, 1u));
            const float render_height = static_cast<float>(std::max(render_extent.height, 1u));
            const float render_aspect = render_width / render_height;
            const float viewport_width = std::max(current_size.x, 1.0f);
            const float viewport_height = std::max(current_size.y, 1.0f);
            const float viewport_aspect = viewport_width / viewport_height;

            ImVec2 image_size = current_size;
            if (viewport_aspect > render_aspect)
            {
                image_size.x = viewport_height * render_aspect;
                image_size.y = viewport_height;
            }
            else
            {
                image_size.x = viewport_width;
                image_size.y = viewport_width / render_aspect;
            }

            const float x_offset = (current_size.x - image_size.x) * 0.5f;
            const float y_offset = (current_size.y - image_size.y) * 0.5f;
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + x_offset,
                                       ImGui::GetCursorPosY() + y_offset));

            const ImVec2 image_min = ImGui::GetCursorScreenPos();
            const ImVec2 image_max(image_min.x + image_size.x, image_min.y + image_size.y);

            ImGui::Image(
                static_cast<ImTextureID>(static_cast<intptr_t>(viewport_texture)),
                image_size,
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f));

            if (context.state->viewport_render_view_valid &&
                context.selection != nullptr &&
                context.selection->type == UiSelection::Type::Entity &&
                context.state->scene_world != nullptr &&
                context.commands != nullptr)
            {
                const core::scene::SceneWorld &scene_world = *context.state->scene_world;
                const entt::entity entity = context.selection->entity;
                if (scene_world.IsValid(entity))
                {
                    const auto &registry = scene_world.Registry();
                    const auto *transform_component = registry.try_get<core::scene::TransformComponent>(entity);
                    if (transform_component != nullptr)
                    {
                        glm::mat4 entity_world = transform_component->world;

                        ImGuizmo::SetOrthographic(false);
                        ImGuizmo::SetDrawlist();
                        ImGuizmo::SetRect(image_min.x, image_min.y, image_max.x - image_min.x, image_max.y - image_min.y);
                        ImGuizmo::Enable(true);

                        const bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(context.state->viewport_render_view.view),
                                                                      glm::value_ptr(context.state->viewport_render_view.projection),
                                                                      ImGuizmo::TRANSLATE,
                                                                      ImGuizmo::LOCAL,
                                                                      glm::value_ptr(entity_world),
                                                                      nullptr,
                                                                      nullptr);

                        if (manipulated)
                        {
                            glm::mat4 local_matrix = entity_world;
                            if (const auto *hierarchy = registry.try_get<core::scene::HierarchyComponent>(entity);
                                hierarchy != nullptr && hierarchy->parent != entt::null && scene_world.IsValid(hierarchy->parent))
                            {
                                if (const auto *parent_transform = registry.try_get<core::scene::TransformComponent>(hierarchy->parent))
                                {
                                    local_matrix = glm::inverse(parent_transform->world) * entity_world;
                                }
                            }

                            float translation[3]{0.0f, 0.0f, 0.0f};
                            float rotation_degrees[3]{0.0f, 0.0f, 0.0f};
                            float scale[3]{1.0f, 1.0f, 1.0f};
                            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local_matrix),
                                                                  translation,
                                                                  rotation_degrees,
                                                                  scale);

                            core::scene::Transform updated_local{};
                            updated_local.translation = glm::vec3(translation[0], translation[1], translation[2]);
                            updated_local.rotation = glm::quat(glm::radians(glm::vec3(rotation_degrees[0],
                                                                                      rotation_degrees[1],
                                                                                      rotation_degrees[2])));
                            updated_local.scale = glm::vec3(scale[0], scale[1], scale[2]);

                            EnqueueCommand(*context.commands, EntitySetLocalTransformCommand{entity, updated_local});
                        }
                    }
                }
            }
        }

        m_last_content_size = current_size;
    }

} // namespace hybrid::ui
