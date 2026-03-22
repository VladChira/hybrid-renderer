#include "ViewportPanel.h"

#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"
#include "ui/UiState.h"

#include <ImGuizmo.h>
#include <glad.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace hybrid::ui
{
    namespace
    {
        constexpr uint32_t kNoEntityId = std::numeric_limits<uint32_t>::max();

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

        bool ReadEntityIdPixel(uint64_t texture_id, int32_t pixel_x, int32_t pixel_y, uint32_t &out_entity_id)
        {
            if (texture_id == 0)
            {
                return false;
            }

            static GLuint readback_framebuffer = 0;
            if (readback_framebuffer == 0)
            {
                glGenFramebuffers(1, &readback_framebuffer);
                if (readback_framebuffer == 0)
                {
                    return false;
                }
            }

            GLint previous_read_framebuffer = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_framebuffer);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, readback_framebuffer);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D,
                                   static_cast<GLuint>(texture_id),
                                   0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_framebuffer));
                return false;
            }

            glReadBuffer(GL_COLOR_ATTACHMENT0);
            uint32_t entity_id = kNoEntityId;
            glReadPixels(pixel_x, pixel_y, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &entity_id);
            out_entity_id = entity_id;

            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_framebuffer));
            return glGetError() == GL_NO_ERROR;
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

            if (context.selection != nullptr &&
                context.state->scene_world != nullptr &&
                ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGuizmo::IsOver() &&
                !ImGuizmo::IsUsing())
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const float normalized_x = std::clamp((mouse.x - image_min.x) / std::max(image_size.x, 1.0f), 0.0f, 0.999999f);
                const float normalized_y = std::clamp((mouse.y - image_min.y) / std::max(image_size.y, 1.0f), 0.0f, 0.999999f);

                const auto pixel_x = static_cast<int32_t>(normalized_x * render_width);
                const auto pixel_from_top = static_cast<int32_t>(normalized_y * render_height);
                const int32_t pixel_y = static_cast<int32_t>(render_extent.height) - 1 - pixel_from_top;

                uint32_t entity_id = kNoEntityId;
                if (ReadEntityIdPixel(context.state->viewport_entity_id_texture, pixel_x, pixel_y, entity_id) &&
                    entity_id != kNoEntityId)
                {
                    const auto entity = static_cast<entt::entity>(entity_id);
                    if (context.state->scene_world->IsValid(entity))
                    {
                        context.selection->type = UiSelection::Type::Entity;
                        context.selection->entity = entity;
                        context.selection->material_asset_id = 0;
                    }
                    else
                    {
                        context.selection->Clear();
                    }
                }
                else
                {
                    context.selection->Clear();
                }
            }

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
